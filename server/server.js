const express = require("express");
const fs = require("fs");
const path = require("path");

const app = express();
const PORT = process.env.PORT || 3000;
const API_KEY = process.env.LOGGER_API_KEY || "";

const dataDir = path.join(__dirname, "data");
const dataFile = path.join(dataDir, "measurements.json");
const publicDir = path.join(__dirname, "public");

fs.mkdirSync(dataDir, { recursive: true });

function loadMeasurements() {
  if (!fs.existsSync(dataFile)) return [];
  try {
    const raw = fs.readFileSync(dataFile, "utf8");
    const data = JSON.parse(raw);
    return Array.isArray(data) ? data : [];
  } catch {
    return [];
  }
}

function saveMeasurements(data) {
  const tmp = dataFile + ".tmp";
  fs.writeFileSync(tmp, JSON.stringify(data));
  fs.renameSync(tmp, dataFile);
}

function requireApiKey(req, res, next) {
  if (!API_KEY) {
    return res.status(500).json({
      error: "LOGGER_API_KEY ist auf dem Server nicht gesetzt."
    });
  }

  if (req.get("X-API-Key") !== API_KEY) {
    return res.status(401).json({ error: "Ungültiger API-Key." });
  }

  next();
}

app.use(express.json({ limit: "10kb" }));

app.get("/api/health", (req, res) => {
  res.json({ ok: true, service: "GIGA Klima Logger API" });
});

app.post("/api/measure", requireApiKey, (req, res) => {
  const { timestamp, temperature, humidity } = req.body;

  const epoch = Number(timestamp);
  const temp = Number(temperature);
  const hum = Number(humidity);

  if (!Number.isFinite(epoch) ||
      !Number.isFinite(temp) ||
      !Number.isFinite(hum)) {
    return res.status(400).json({
      error: "timestamp, temperature und humidity müssen Zahlen sein."
    });
  }

  if (epoch < 946684800 || epoch > 4102444800) {
    return res.status(400).json({ error: "Ungültiger Zeitstempel." });
  }

  if (temp < -80 || temp > 100 || hum < 0 || hum > 100) {
    return res.status(400).json({ error: "Messwerte außerhalb des erlaubten Bereichs." });
  }

  const measurements = loadMeasurements();

  // Zeitstempel ist die eindeutige ID. Dadurch entstehen beim erneuten
  // Upload aus pending.csv keine doppelten Datensätze.
  const duplicate = measurements.some(m => m.timestamp === Math.trunc(epoch));

  if (duplicate) {
    return res.json({ ok: true, duplicate: true });
  }

  measurements.push({
    timestamp: Math.trunc(epoch),
    iso: new Date(Math.trunc(epoch) * 1000).toISOString(),
    temperature: Math.round(temp * 100) / 100,
    humidity: Math.round(hum * 100) / 100
  });

  // Maximal 100.000 Messungen im JSON-Speicher.
  if (measurements.length > 100000) {
    measurements.splice(0, measurements.length - 100000);
  }

  saveMeasurements(measurements);

  res.status(201).json({ ok: true, duplicate: false });
});

app.get("/api/latest", (req, res) => {
  const measurements = loadMeasurements();
  const latest = measurements.length
    ? measurements[measurements.length - 1]
    : null;

  res.json(latest);
});

app.get("/api/history", (req, res) => {
  const measurements = loadMeasurements();
  let limit = Number(req.query.limit || 1440);

  if (!Number.isFinite(limit)) limit = 1440;
  limit = Math.max(1, Math.min(10000, Math.floor(limit)));

  res.json(measurements.slice(-limit));
});

app.get("/api/history.csv", (req, res) => {
  const measurements = loadMeasurements();

  const rows = [
    "timestamp,iso,temperature_C,humidity_percent"
  ];

  for (const m of measurements) {
    rows.push([
      m.timestamp,
      m.iso,
      m.temperature,
      m.humidity
    ].join(","));
  }

  res.setHeader("Content-Type", "text/csv; charset=utf-8");
  res.setHeader(
    "Content-Disposition",
    'attachment; filename="klima_history.csv"'
  );
  res.send(rows.join("\n"));
});

app.use(express.static(publicDir));

app.listen(PORT, () => {
  console.log(`GIGA Klima Logger läuft auf Port ${PORT}`);
});
