# Vehicle Monitor Server (Render Deploy)

This project gives you:
- A deployable Node.js server for Render.
- A modern dashboard webpage to show Vehicle 1 and Vehicle 2 as ONLINE/OFFLINE.
- API endpoints for ESP32 vehicles to send heartbeat and events.
- Dashboard-only speed and direction control for both vehicles.
- Updated ESP32 sketches without traffic-light integration.

## Why HTTP Instead of UDP on Render
Render web services are HTTP/TCP based. Public inbound UDP is not suitable for standard Render web deployment. So this version uses HTTP POST from ESP32 to server APIs.

## Project Structure
- server.js
- public/index.html
- esp/vehicle1_render.ino
- esp/vehicle2_render.ino
- render.yaml
- package.json

## Local Run
1. Install Node.js 18+.
2. In project folder:
   npm install
3. Start server:
   npm start
4. Open:
   http://localhost:10000

## Render Deployment
1. Push this folder to GitHub.
2. In Render: New -> Web Service.
3. Connect your GitHub repo.
4. Render should auto-detect settings from render.yaml.
5. Deploy.
6. Copy deployed URL, example:
   https://vehicle-monitor-server.onrender.com
7. Update both ESP files:
   const char* serverBaseUrl = "https://vehicle-monitor-server.onrender.com";

## API
- POST /api/vehicle/vehicle1/heartbeat
- POST /api/vehicle/vehicle2/heartbeat
- POST /api/vehicle/vehicle1/event
- POST /api/vehicle/vehicle2/event
- GET /api/vehicle/vehicle1/control
- GET /api/vehicle/vehicle2/control
- POST /api/vehicle/vehicle1/control
- POST /api/vehicle/vehicle2/control
- GET /api/status

### Dashboard Control Request Body
Use `POST /api/vehicle/:id/control` with:

```json
{
   "direction": "forward",
   "speedPwm": 180
}
```

- `direction` must be one of: `forward`, `reverse`, `stop`
- `speedPwm` must be `0` to `255`

## GitHub Push Commands
Run these commands in terminal inside this folder:

```bash
git init
git add .
git commit -m "Initial vehicle monitor server and ESP integration"
git branch -M main
git remote add origin <YOUR_GITHUB_REPO_URL>
git push -u origin main
```

If your repo already exists and is initialized, skip git init and remote add.
