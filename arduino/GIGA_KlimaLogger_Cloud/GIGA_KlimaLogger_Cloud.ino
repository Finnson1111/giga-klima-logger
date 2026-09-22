#include <Wire.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_AHTX0.h>
#include <RTClib.h>
#include <Arduino_GigaDisplay_GFX.h>
#include <WiFi.h>
#include <WiFiSSLClient.h>

// =========================
// WLAN / Cloud API
// =========================
const char WIFI_SSID[]     = "DEIN_WLAN";
const char WIFI_PASSWORD[] = "DEIN_WLAN_PASSWORT";

// Beispiel: "logger.example.de" – OHNE https:// und OHNE /api/...
const char API_HOST[] = "DEINE-DOMAIN.DE";
const uint16_t API_PORT = 443;
const char API_PATH[] = "/api/measure";
const char API_KEY[] = "DEIN_LANGER_API_KEY";

// =========================
// Hardware
// =========================
const uint8_t SD_CS_PIN = 10;
const uint32_t LOG_INTERVAL_MS = 60000UL;

// DS1307 beim ersten Start einmal auf die PC-Kompilierzeit setzen.
// Danach auf false stellen und erneut hochladen.
const bool SET_RTC_ONCE = true;

Adafruit_AHTX0 aht;
RTC_DS1307 rtc;
SdFat sd;
GigaDisplayRGB display;
WiFiSSLClient sslClient;

uint32_t lastLogMs = 0;

// ---------- Hilfsfunktionen ----------
String twoDigits(int v) {
  return (v < 10 ? "0" : "") + String(v);
}

String isoTimestamp(const DateTime& dt) {
  char buf[25];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           dt.year(), dt.month(), dt.day(),
           dt.hour(), dt.minute(), dt.second());
  return String(buf);
}

void showStatus(const String& line1, const String& line2 = "") {
  display.fillScreen(0);
  display.setTextColor(0xFFFF);
  display.setTextSize(2);
  display.setCursor(20, 30);
  display.println(line1);
  display.setCursor(20, 65);
  display.println(line2);
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  showStatus("WLAN wird verbunden...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WLAN OK, IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("WLAN Verbindung fehlgeschlagen.");
  return false;
}

bool appendCsvLine(const char* filename, uint32_t epoch,
                   float temperature, float humidity) {
  FsFile f = sd.open(filename, FILE_WRITE);
  if (!f) {
    Serial.print("Kann Datei nicht öffnen: ");
    Serial.println(filename);
    return false;
  }

  f.print(epoch);
  f.print(",");
  f.print(temperature, 2);
  f.print(",");
  f.println(humidity, 2);
  f.close();
  return true;
}

bool postMeasurement(uint32_t epoch, float temperature, float humidity) {
  if (!connectWiFi()) return false;

  String body = "{\"timestamp\":" + String(epoch) +
                ",\"temperature\":" + String(temperature, 2) +
                ",\"humidity\":" + String(humidity, 2) + "}";

  Serial.print("Cloud POST: ");
  Serial.println(body);

  if (!sslClient.connect(API_HOST, API_PORT)) {
    Serial.println("HTTPS-Verbindung fehlgeschlagen.");
    sslClient.stop();
    return false;
  }

  sslClient.print("POST ");
  sslClient.print(API_PATH);
  sslClient.println(" HTTP/1.1");
  sslClient.print("Host: ");
  sslClient.println(API_HOST);
  sslClient.println("Content-Type: application/json");
  sslClient.print("X-API-Key: ");
  sslClient.println(API_KEY);
  sslClient.print("Content-Length: ");
  sslClient.println(body.length());
  sslClient.println("Connection: close");
  sslClient.println();
  sslClient.print(body);

  uint32_t start = millis();
  while (!sslClient.available() && millis() - start < 8000UL) {
    delay(10);
  }

  if (!sslClient.available()) {
    Serial.println("Keine API-Antwort.");
    sslClient.stop();
    return false;
  }

  String statusLine = sslClient.readStringUntil('\n');
  statusLine.trim();
  Serial.print("API: ");
  Serial.println(statusLine);

  bool ok = statusLine.startsWith("HTTP/1.1 200") ||
            statusLine.startsWith("HTTP/1.1 201");

  sslClient.stop();
  return ok;
}

bool parsePendingLine(const String& line, uint32_t& epoch,
                      float& temperature, float& humidity) {
  int p1 = line.indexOf(',');
  int p2 = line.indexOf(',', p1 + 1);
  if (p1 < 0 || p2 < 0) return false;

  epoch = (uint32_t)line.substring(0, p1).toInt();
  temperature = line.substring(p1 + 1, p2).toFloat();
  humidity = line.substring(p2 + 1).toFloat();
  return epoch > 0;
}

// Versucht alle offline gespeicherten Messungen hochzuladen.
// Der Server erkennt doppelte Zeitstempel und speichert sie nicht doppelt.
void syncPending() {
  if (!sd.exists("pending.csv")) return;

  FsFile in = sd.open("pending.csv", FILE_READ);
  if (!in) return;

  // Neue Datei für den Rest, falls ein Upload fehlschlägt.
  sd.remove("pending.tmp");
  FsFile rest = sd.open("pending.tmp", FILE_WRITE);
  if (!rest) {
    in.close();
    return;
  }

  bool allUploaded = true;

  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    uint32_t epoch;
    float t, h;
    if (!parsePendingLine(line, epoch, t, h)) {
      // Ungültige Zeile nicht erneut versuchen.
      continue;
    }

    if (!postMeasurement(epoch, t, h)) {
      rest.println(line);
      allUploaded = false;

      // Restliche Zeilen unverändert übernehmen.
      while (in.available()) {
        String remaining = in.readStringUntil('\n');
        remaining.trim();
        if (remaining.length()) rest.println(remaining);
      }
      break;
    }
  }

  in.close();
  rest.close();

  sd.remove("pending.csv");

  if (!allUploaded) {
    sd.rename("pending.tmp", "pending.csv");
    Serial.println("Pending-Datei bleibt für später erhalten.");
  } else {
    sd.remove("pending.tmp");
    Serial.println("Pending-Datei vollständig synchronisiert.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  display.begin();
  display.fillScreen(0);

  showStatus("GIGA Klima Logger", "Starte...");

  Wire.begin();

  if (!aht.begin(&Wire)) {
    showStatus("FEHLER", "AHT10 nicht gefunden");
    while (true) delay(1000);
  }

  if (!rtc.begin(&Wire)) {
    showStatus("FEHLER", "DS1307 nicht gefunden");
    while (true) delay(1000);
  }

  if (SET_RTC_ONCE) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("RTC wurde auf Kompilierzeit gesetzt.");
  }

  if (!sd.begin(SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(4), &SPI1))) {
    showStatus("FEHLER", "SD-Karte nicht gefunden");
    while (true) delay(1000);
  }

  // Haupt-CSV mit Zeitstempel in deutscher Spaltenaufteilung.
  if (!sd.exists("messung.csv")) {
    FsFile f = sd.open("messung.csv", FILE_WRITE);
    if (f) {
      f.println("Jahr,Monat,Tag,Stunde,Minute,Sekunde,Temperatur_C,Luftfeuchtigkeit_Prozent");
      f.close();
    }
  }

  connectWiFi();
  syncPending();

  showStatus("System bereit",
             WiFi.status() == WL_CONNECTED ? "Cloud verbunden" : "Nur SD-Modus");

  Serial.println("Setup fertig.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (millis() - lastLogMs < LOG_INTERVAL_MS) {
    delay(50);
    return;
  }
  lastLogMs = millis();

  sensors_event_t humidityEvent, tempEvent;
  aht.getEvent(&humidityEvent, &tempEvent);

  DateTime now = rtc.now();
  uint32_t epoch = now.unixtime();

  float temperature = tempEvent.temperature;
  float humidity = humidityEvent.relative_humidity;

  // Hauptdatei: vollständiger deutscher CSV-Datensatz.
  FsFile f = sd.open("messung.csv", FILE_WRITE);
  if (f) {
    f.print(now.year()); f.print(",");
    f.print(now.month()); f.print(",");
    f.print(now.day()); f.print(",");
    f.print(now.hour()); f.print(",");
    f.print(now.minute()); f.print(",");
    f.print(now.second()); f.print(",");
    f.print(temperature, 2); f.print(",");
    f.println(humidity, 2);
    f.close();
  }

  bool uploaded = postMeasurement(epoch, temperature, humidity);

  if (!uploaded) {
    appendCsvLine("pending.csv", epoch, temperature, humidity);
    Serial.println("Internet/API nicht erreichbar -> in pending.csv gespeichert.");
  }

  // Nach einer erfolgreichen Verbindung werden ältere Offline-Messungen
  // nachgeschoben.
  if (uploaded) {
    syncPending();
  }

  // Display
  display.fillScreen(0);
  display.setTextColor(0xFFFF);
  display.setTextSize(2);

  display.setCursor(20, 20);
  display.println("Klima Logger");

  display.setCursor(20, 70);
  display.print(now.day()); display.print(".");
  display.print(now.month()); display.print(".");
  display.println(now.year());

  display.setCursor(20, 110);
  display.print(twoDigits(now.hour())); display.print(":");
  display.print(twoDigits(now.minute())); display.print(":");
  display.println(twoDigits(now.second()));

  display.setCursor(20, 165);
  display.print("Temperatur: ");
  display.print(temperature, 1);
  display.println(" C");

  display.setCursor(20, 205);
  display.print("Luftfeuchte: ");
  display.print(humidity, 1);
  display.println(" %");

  display.setCursor(20, 255);
  display.print("WLAN: ");
  display.println(WiFi.status() == WL_CONNECTED ? "OK" : "OFF");

  display.setCursor(20, 295);
  display.print("Cloud: ");
  display.println(uploaded ? "OK" : "OFFLINE");

  delay(50);
}
