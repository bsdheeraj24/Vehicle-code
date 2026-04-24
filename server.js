         const express = require("express");
const cors = require("cors");
const path = require("path");

const app = express();
const PORT = process.env.PORT || 10000;

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, "public")));

const OFFLINE_TIMEOUT_MS = 15000;

const VALID_DIRECTIONS = new Set(["forward", "reverse", "stop"]);

function clampPwm(value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) return null;
  return Math.max(0, Math.min(255, Math.round(numeric)));
}

function normalizeDirection(value) {
  if (typeof value !== "string") return null;
  const parsed = value.trim().toLowerCase();
  return VALID_DIRECTIONS.has(parsed) ? parsed : null;
}

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
    direction: "stop",
    motorRunning: false,
    emergencyMode: false,
    lastEvent: "No events yet",
    desiredDirection: "stop",
    desiredSpeedPwm: 0,
    commandUpdatedAt: null
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
    direction: "stop",
    motorRunning: false,
    emergencyMode: false,
    lastEvent: "No events yet",
    desiredDirection: "stop",
    desiredSpeedPwm: 0,
    commandUpdatedAt: null
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
  target.speedPwm = typeof body.speedPwm === "number" ? body.speedPwm : target.speedPwm;
  target.direction = typeof body.direction === "string" ? body.direction : target.direction;
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

app.get("/api/vehicle/:id/control", (req, res) => {
  const id = safeVehicleId(req.params.id);
  if (!id) {
    return res.status(400).json({ error: "Invalid vehicle id. Use vehicle1 or vehicle2." });
  }

  const target = state[id];
  return res.json({
    ok: true,
    id,
    direction: target.desiredDirection,
    speedPwm: target.desiredSpeedPwm,
    updatedAt: target.commandUpdatedAt
  });
});

app.post("/api/vehicle/:id/control", (req, res) => {
  const id = safeVehicleId(req.params.id);
  if (!id) {
    return res.status(400).json({ error: "Invalid vehicle id. Use vehicle1 or vehicle2." });
  }

  const nextDirection = normalizeDirection(req.body?.direction);
  const nextSpeed = clampPwm(req.body?.speedPwm);

  if (!nextDirection) {
    return res.status(400).json({ error: "direction is required and must be forward, reverse, or stop." });
  }
  if (nextSpeed === null) {
    return res.status(400).json({ error: "speedPwm is required and must be a number between 0 and 255." });
  }

  const target = state[id];
  target.desiredDirection = nextDirection;
  target.desiredSpeedPwm = nextSpeed;
  target.commandUpdatedAt = Date.now();
  target.lastEvent = `${new Date().toLocaleTimeString("en-IN", { timeZone: "Asia/Kolkata" })} - Dashboard command: ${nextDirection.toUpperCase()} @ ${nextSpeed}`;

  return res.json({
    ok: true,
    id,
    direction: target.desiredDirection,
    speedPwm: target.desiredSpeedPwm,
    updatedAt: target.commandUpdatedAt
  });
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
