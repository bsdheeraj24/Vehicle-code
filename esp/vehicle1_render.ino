#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <esp32-hal-ledc.h>

#define TRIG_PIN 12
#define ECHO_PIN 13

// Motor pins (L298N) based on your working reference code
const int motor1Pin1 = 27;
const int motor1Pin2 = 26;
const int enable1Pin = 14;

const char* ssid = "Dheeraj";
const char* password = "dheerubs";
// IMPORTANT: Replace with your actual Render URL.
const char* serverBaseUrl = "https://your-render-service.onrender.com";

// PWM properties based on your working reference code
const int freq = 30000;
const int pwmChannel = 0;
const int resolution = 8;

int dutyCycle = 200;

Adafruit_MPU6050 mpu;
bool motorRunning = false;
bool emergencyMode = false;
bool previousEmergencyMode = false;
unsigned long lastPushMs = 0;
unsigned long lastControlPullMs = 0;
unsigned long lastReconnectAttemptMs = 0;
bool wasWifiConnected = false;
String currentDirection = "stop";

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
    currentDirection = "forward";
}

void moveReverse() {
    digitalWrite(motor1Pin1, HIGH);
    digitalWrite(motor1Pin2, LOW);
    ledcWrite(pwmChannel, dutyCycle);
    motorRunning = true;
    currentDirection = "reverse";
}

void stopMotor() {
    digitalWrite(motor1Pin1, LOW);
    digitalWrite(motor1Pin2, LOW);
    ledcWrite(pwmChannel, 0);
    motorRunning = false;
    currentDirection = "stop";
}

String readJsonValue(String payload, String key) {
    String token = "\"" + key + "\"";
    int keyIndex = payload.indexOf(token);
    if (keyIndex < 0) return "";

    int colonIndex = payload.indexOf(':', keyIndex + token.length());
    if (colonIndex < 0) return "";

    int valueStart = colonIndex + 1;
    while (valueStart < payload.length() && payload[valueStart] == ' ') {
        valueStart++;
    }

    if (valueStart >= payload.length()) return "";

    if (payload[valueStart] == '"') {
        int valueEnd = payload.indexOf('"', valueStart + 1);
        if (valueEnd < 0) return "";
        return payload.substring(valueStart + 1, valueEnd);
    }

    int valueEnd = valueStart;
    while (valueEnd < payload.length() && payload[valueEnd] != ',' && payload[valueEnd] != '}') {
        valueEnd++;
    }

    return payload.substring(valueStart, valueEnd);
}

void applyControlCommand(String direction, int speedPwm) {
    dutyCycle = constrain(speedPwm, 0, 255);

    if (WiFi.status() != WL_CONNECTED) {
        stopMotor();
        return;
    }

    if (emergencyMode) {
        stopMotor();
        return;
    }

    if (direction == "forward") {
        moveForward();
        return;
    }

    if (direction == "reverse") {
        moveReverse();
        return;
    }

    stopMotor();
}

void pullControlCommand() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = String(serverBaseUrl) + "/api/vehicle/vehicle1/control";
    http.begin(url);
    int statusCode = http.GET();
    if (statusCode != 200) {
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    String direction = readJsonValue(payload, "direction");
    String speedValue = readJsonValue(payload, "speedPwm");
    if (direction.length() == 0 || speedValue.length() == 0) return;

    int speedPwm = speedValue.toInt();
    applyControlCommand(direction, speedPwm);
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
    payload += "\"direction\":\"" + currentDirection + "\",";
    payload += "\"motorRunning\":" + String(motorRunning ? "true" : "false") + ",";
    payload += "\"emergencyMode\":" + String(emergencyMode ? "true" : "false");
    payload += "}";

    http.POST(payload);
    http.end();
}

void enforceInternetSafety() {
    bool connected = WiFi.status() == WL_CONNECTED;

    if (!connected) {
        if (motorRunning) {
            stopMotor();
            Serial.println("WiFi lost: motor stopped");
        }

        if (millis() - lastReconnectAttemptMs > 3000) {
            WiFi.reconnect();
            lastReconnectAttemptMs = millis();
            Serial.println("Trying WiFi reconnect...");
        }
    }

    if (connected && !wasWifiConnected) {
        Serial.println("WiFi reconnected");
        postEvent("Vehicle 1 reconnected");
    }

    wasWifiConnected = connected;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Vehicle 1 booting...");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    pinMode(motor1Pin1, OUTPUT);
    pinMode(motor1Pin2, OUTPUT);
    pinMode(enable1Pin, OUTPUT);

    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(enable1Pin, pwmChannel);
    ledcWrite(pwmChannel, dutyCycle);

    stopMotor();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("WiFi connected");
    wasWifiConnected = true;

    if (!mpu.begin()) {
        Serial.println("MPU6050 not found");
        while (1) {
            delay(100);
        }
    }

    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

    postEvent("Vehicle 1 online");
}

void loop() {
    enforceInternetSafety();

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float avgAccel = (fabs(a.acceleration.x) + fabs(a.acceleration.y) + fabs(a.acceleration.z)) / 3.0;
    float avgGyro = (fabs(g.gyro.x) + fabs(g.gyro.y) + fabs(g.gyro.z)) / 3.0;
    float distance = getDistance();

    emergencyMode = (distance > 0 && distance < 10.0);
    if (emergencyMode) {
        stopMotor();
    }

    if (emergencyMode != previousEmergencyMode) {
        if (emergencyMode) {
            postEvent("Collision warning");
        } else {
            postEvent("Path clear");
        }
        previousEmergencyMode = emergencyMode;
    }

    if (millis() - lastControlPullMs > 700) {
        pullControlCommand();
        lastControlPullMs = millis();
    }

    if (millis() - lastPushMs > 2500) {
        sendHeartbeat(distance, avgAccel, avgGyro);
        lastPushMs = millis();
    }

    delay(30);
}
