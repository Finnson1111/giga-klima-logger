# GIGA Klima Logger Server v2

Render:
- Web Service
- Repository: Finnson1111/Giga-Klima-Logger
- Branch: main
- Root Directory: Server
- Runtime: Node
- Region: Frankfurt
- Build Command: npm install
- Start Command: npm start
- Environment: LOGGER_API_KEY = derselbe Schlüssel wie im Arduino

API: /api/health, /api/measure, /api/latest, /api/history?limit=200, /api/history.csv

Hinweis: Render Free kann das lokale Dateisystem bei Neustarts/Deployments zurücksetzen. Für dauerhaft gespeicherte Messdaten später Datenbank oder Persistent Disk verwenden.
