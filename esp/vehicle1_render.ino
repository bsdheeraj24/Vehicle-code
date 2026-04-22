#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <esp32-hal-ledc.h>

#define TRIG_PIN 12
#define ECHO_PIN 13
#define motor1Pin1 25
#define motor1Pin2 33
#define enable1Pin 26

const char* ssid = "Dheeraj";
const char* password = "dheerubs";
const char* serverBaseUrl = "https://your-render-service.onrender.com";

const int freq = 2000;
const int pwmChannel = 0;
const int resolution = 8;
int dutyCycle = 150;

WebServer server(80);
Adafruit_MPU6050 mpu;
bool motorRunning = false;
bool emergencyMode = false;
unsigned long lastPushMs = 0;

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Vehicle 1</title>
    <style>
        body { text-align:center; font-family:Arial; margin-top:30px; }
        button { padding:10px 20px; font-size:18px; margin:8px; }
    </style>
</head>
<body>
    <h2>Vehicle 1 Local Control</h2>
    <button onclick="fetch('/start')">Start</button>
    <button onclick="fetch('/stop')">Stop</button>
    <button onclick="fetch('/emergency')">Emergency</button>
</body>
</html>
)rawliteral";

float getDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    if (duration == 0) return -1;
    return duration * 0.034 / 2;
}

void moveForward() {
    digitalWrite(motor1Pin1, LOW);
    digitalWrite(motor1Pin2, HIGH);
    ledcWrite(pwmChannel, dutyCycle);
    motorRunning = true;
}

void stopMotor() {
    digitalWrite(motor1Pin1, LOW);
    digitalWrite(motor1Pin2, LOW);
    ledcWrite(pwmChannel, 0);
    motorRunning = false;
}

void postEvent(String eventName) {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = String(serverBaseUrl) + "/api/vehicle/vehicle1/event";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"event\":\"" + eventName + "\"}";
    http.POST(payload);
    http.end();
}

void sendHeartbeat(float distanceCm, float accel, float gyro) {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = String(serverBaseUrl) + "/api/vehicle/vehicle1/heartbeat";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String ip = WiFi.localIP().toString();
    String payload = "{";
    payload += "\"ip\":\"" + ip + "\",";
    payload += "\"distanceCm\":" + String(distanceCm, 2) + ",";
    payload += "\"accel\":" + String(accel, 3) + ",";
    payload += "\"gyro\":" + String(gyro, 3) + ",";
    payload += "\"speedPwm\":" + String(dutyCycle) + ",";
    payload += "\"motorRunning\":" + String(motorRunning ? "true" : "false") + ",";
    payload += "\"emergencyMode\":" + String(emergencyMode ? "true" : "false");
    payload += "}";

    http.POST(payload);
    http.end();
}

void setup() {
    Serial.begin(115200);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(motor1Pin1, OUTPUT);
    pinMode(motor1Pin2, OUTPUT);
    pinMode(enable1Pin, OUTPUT);
    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(enable1Pin, pwmChannel);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }

    if (!mpu.begin()) {
        while (1) {
            delay(100);
        }
    }

    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

    server.on("/", []() {
        server.send(200, "text/html", htmlPage);
    });

    server.on("/start", []() {
        moveForward();
        postEvent("Motor started");
        server.send(200, "text/plain", "Motor Started");
    });

    server.on("/stop", []() {
        stopMotor();
        postEvent("Motor stopped");
        server.send(200, "text/plain", "Motor Stopped");
    });

    server.on("/emergency", []() {
        emergencyMode = !emergencyMode;
        postEvent(emergencyMode ? "Emergency ON" : "Emergency OFF");
        server.send(200, "text/plain", "Emergency Toggled");
    });

    server.begin();
}

void loop() {
    server.handleClient();

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float avgAccel = (fabs(a.acceleration.x) + fabs(a.acceleration.y) + fabs(a.acceleration.z)) / 3.0;
    float avgGyro = (fabs(g.gyro.x) + fabs(g.gyro.y) + fabs(g.gyro.z)) / 3.0;
    float distance = getDistance();

    if (distance > 0 && distance < 10.0) {
        stopMotor();
        postEvent("Collision warning");
    }

    if (millis() - lastPushMs > 2500) {
        sendHeartbeat(distance, avgAccel, avgGyro);
        lastPushMs = millis();
    }

    delay(30);
}
