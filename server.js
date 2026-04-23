         const express = require("express");
const cors = require("cors");
const path = require("path");

const app = express();
const PORT = process.env.PORT || 10000;

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, "public")));

const OFFLINE_TIMEOUT_MS = 15000;

const state = {
  vehicle1: {
    id: "vehicle1",
    name: "Vehicle 1",
    lastSeen: null,
    isOnline: false,
    ip: null,
    battery: null,
    distanceCm: null,
    accel: null,
    gyro: null,
    speedPwm: null,
    motorRunning: false,
    emergencyMode: false,
    lastEvent: "No events yet"
  },
  vehicle2: {
    id: "vehicle2",
    name: "Vehicle 2",
    lastSeen: null,
    isOnline: false,
    ip: null,
    battery: null,
    distanceCm: null,
    accel: null,
    gyro: null,
    speedPwm: null,
    motorRunning: false,
    emergencyMode: false,
    lastEvent: "No events yet"
  }
};

function refreshOnlineFlags() {
  const now = Date.now();
  Object.values(state).forEach((v) => {
    v.isOnline = !!v.lastSeen && now - v.lastSeen <= OFFLINE_TIMEOUT_MS;
  });
}

function safeVehicleId(id) {
  if (id !== "vehicle1" && id !== "vehicle2") return null;
  return id;
}

app.get("/health", (req, res) => {
  refreshOnlineFlags();
  res.json({ ok: true, time: new Date().toISOString() });
});

app.post("/api/vehicle/:id/heartbeat", (req, res) => {
  const id = safeVehicleId(req.params.id);
  if (!id) {
    return res.status(400).json({ error: "Invalid vehicle id. Use vehicle1 or vehicle2." });
  }

  const target = state[id];
  const body = req.body || {};

  target.lastSeen = Date.now();
  target.ip = body.ip || req.ip || target.ip;
  target.battery = typeof body.battery === "number" ? body.battery : target.battery;
  target.distanceCm = typeof body.distanceCm === "number" ? body.distanceCm : target.distanceCm;
  target.accel = typeof body.accel === "number" ? body.accel : target.accel;
  target.gyro = typeof body.gyro === "number" ? body.gyro : target.gyro;
  if (id === "vehicle1") {
    target.speedPwm = typeof body.speedPwm === "number" ? body.speedPwm : target.speedPwm;
  } else {
    target.speedPwm = null;
  }
  target.motorRunning = typeof body.motorRunning === "boolean" ? body.motorRunning : target.motorRunning;
  target.emergencyMode = typeof body.emergencyMode === "boolean" ? body.emergencyMode : target.emergencyMode;

  refreshOnlineFlags();
  return res.json({ ok: true, vehicle: target });
});

app.post("/api/vehicle/:id/event", (req, res) => {
  const id = safeVehicleId(req.params.id);
  if (!id) {
    return res.status(400).json({ error: "Invalid vehicle id. Use vehicle1 or vehicle2." });
  }

  const target = state[id];
  const eventText = typeof req.body?.event === "string" ? req.body.event : "Unknown event";

  target.lastSeen = Date.now();
  target.lastEvent = `${new Date().toLocaleTimeString("en-IN", { timeZone: "Asia/Kolkata" })} - ${eventText}`;

  refreshOnlineFlags();
  return res.json({ ok: true, vehicle: target });
});

app.get("/api/status", (req, res) => {
  refreshOnlineFlags();
  res.json({
    ok: true,
    serverTime: new Date().toISOString(),
    vehicles: [state.vehicle1, state.vehicle2]
  });
});

app.get("*", (req, res) => {
  res.sendFile(path.join(__dirname, "public", "index.html"));
});

app.listen(PORT, () => {
  console.log(`Vehicle monitor server running on port ${PORT}`);
});
