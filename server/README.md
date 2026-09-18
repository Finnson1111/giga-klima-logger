# GIGA Klima Logger – externe Website

## 1. Aufbau

```text
AHT10 ─┐
       ├─ SDA1 D20 / SCL1 D21 ── GIGA R1 WiFi
DS1307 ┘

SD-Karte ─ SPI1, CS D10 ───────── GIGA

GIGA ── WLAN ── HTTPS ──> Node.js API ──> measurements.json
                                      └──> index.html
```

Der GIGA speichert jede Messung auf der SD-Karte und versucht zusätzlich,
sie über HTTPS an die externe API zu senden. Bei einem Internetausfall
landet die Messung in `pending.csv` und wird später nachgesendet.

## 2. Server vorbereiten

Du brauchst einen Node.js-Webserver mit persistentem Speicher.

Im Ordner `server`:

```bash
npm install
```

Dann den API-Key setzen. Unter Linux/macOS zum Beispiel:

```bash
export LOGGER_API_KEY="EIN_LANGER_ZUFALLSKEY"
npm start
```

Unter Windows PowerShell:

```powershell
$env:LOGGER_API_KEY="EIN-LANGER-ZUFALLSKEY"
npm start
```

Der Server startet standardmäßig auf Port 3000.

Wichtig: Für den öffentlichen Betrieb muss der Server über HTTPS erreichbar
sein. Bei einem Hosting-Anbieter wird HTTPS normalerweise vom Anbieter
bereitgestellt. Wenn du einen eigenen Server benutzt, brauchst du einen
Reverse Proxy/TLS davor.

## 3. Arduino anpassen

In `GIGA_KlimaLogger_Cloud.ino` eintragen:

```cpp
const char WIFI_SSID[]     = "DEIN_WLAN";
const char WIFI_PASSWORD[] = "DEIN_WLAN_PASSWORT";

const char API_HOST[] = "DEINE-DOMAIN.DE";
const char API_PATH[] = "/api/measure";
const char API_KEY[]  = "DEIN_LANGER_API_KEY";
```

`API_HOST` enthält nur den Hostnamen, also z. B.:

```text
logger.example.de
```

nicht:

```text
https://logger.example.de
```

### RTC

Beim ersten Upload:

```cpp
const bool SET_RTC_ONCE = true;
```

Danach auf:

```cpp
const bool SET_RTC_ONCE = false;
```

ändern und nochmals hochladen. Sonst wird die Uhr bei jedem Neustart auf
die Kompilierzeit gesetzt.

## 4. Bibliotheken

In der Arduino IDE installieren:

- Arduino_GigaDisplay_GFX
- Adafruit AHTX0
- Adafruit Unified Sensor
- RTClib
- SdFat

`WiFi` und `WiFiSSLClient` kommen mit der GIGA-Unterstützung.

## 5. Daten

Auf der SD-Karte:

- `messung.csv` = alle lokalen Messungen
- `pending.csv` = Messungen, die noch nicht an die API übertragen wurden

Auf dem Server:

- `data/measurements.json` = Cloud-Historie

Die Website bietet zusätzlich:

```text
/api/latest
/api/history?limit=500
/api/history.csv
```

## 6. Sicherheit

Der API-Key gehört nur in:

- den Arduino-Sketch
- die Server-Umgebungsvariable

Er gehört NICHT in `index.html`.

Die Website kann die Messwerte öffentlich anzeigen, ohne den API-Key
preiszugeben.

## 7. HTTPS-Probleme

Falls der GIGA die HTTPS-Verbindung nicht aufbauen kann:

1. Arduino IDE und GIGA Board-Unterstützung aktualisieren.
2. WiFi-/Firmware-Updates prüfen.
3. Seriellen Monitor mit 115200 Baud öffnen.
4. Nach `HTTPS-Verbindung fehlgeschlagen.` suchen.

Die lokale SD-Aufzeichnung funktioniert unabhängig davon weiter.

## 8. Wichtiger Hinweis zur Speicherung

`measurements.json` benötigt persistenten Speicher. Bei einem Hosting,
dessen Dateisystem bei Neustarts gelöscht wird, gehen die historischen
Cloud-Daten verloren. Für ein dauerhaftes Projekt sollte deshalb ein
Hosting mit persistentem Datenträger oder später eine richtige Datenbank
verwendet werden.

## 9. Arduino Cloud als Alternative

Der GIGA R1 WiFi ist offiziell mit Arduino Cloud kompatibel. Arduino Cloud
kann Sensorwerte protokollieren, grafisch darstellen und historische Daten
bereitstellen. Für dieses Projekt wurde trotzdem eine eigene Node.js-API
verwendet, weil du eine eigene Website und eine eigene Daten-API wolltest.
