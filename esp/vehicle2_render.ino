#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp32-hal-ledc.h>

// Motor pins (L298N) based on your working reference code
const int motor1Pin1 = 27;
const int motor1Pin2 = 26;
const int enable1Pin = 14;

const char* ssid = "Dheeraj";
const char* password = "dheerubs";
// IMPORTANT: Replace with your actual Render URL.
const char* serverBaseUrl = "https://your-render-service.onrender.com";

LiquidCrystal_I2C lcd(0x27, 16, 2);
// PWM properties based on your working reference code
const int freq = 30000;
const int pwmChannel = 0;
const int resolution = 8;
int dutyCycle = 200;
bool motorRunning = false;
unsigned long lastPushMs = 0;
unsigned long lastControlPullMs = 0;
unsigned long lastReconnectAttemptMs = 0;
float lastDistanceCm = -1;
bool obstacleDetected = false;
bool previousObstacleDetected = false;
bool wasWifiConnected = false;
String currentDirection = "stop";

const float obstacleThresholdCm = 30.0;

void moveForward() {
    digitalWrite(motor1Pin1, HIGH);
    digitalWrite(motor1Pin2, LOW);
    ledcWrite(pwmChannel, dutyCycle);
    motorRunning = true;
    currentDirection = "forward";
}

void moveReverse() {
    digitalWrite(motor1Pin1, LOW);
    digitalWrite(motor1Pin2, HIGH);
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
    String url = String(serverBaseUrl) + "/api/vehicle/vehicle2/control";
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
    String url = String(serverBaseUrl) + "/api/vehicle/vehicle2/event";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"event\":\"" + eventName + "\"}";
    http.POST(payload);
    http.end();
}

void sendHeartbeat() {
    lastDistanceCm = random(20, 120);
    float simulatedAccel = random(10, 40) / 10.0;
    float simulatedGyro = random(5, 25) / 10.0;

    obstacleDetected = lastDistanceCm > 0 && lastDistanceCm < obstacleThresholdCm;
    if (obstacleDetected && !previousObstacleDetected) {
        postEvent("Obstacle avoidance triggered");
    }
    previousObstacleDetected = obstacleDetected;

    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = String(serverBaseUrl) + "/api/vehicle/vehicle2/heartbeat";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String ip = WiFi.localIP().toString();

    String payload = "{";
    payload += "\"ip\":\"" + ip + "\",";
    payload += "\"distanceCm\":" + String(lastDistanceCm, 1) + ",";
    payload += "\"accel\":" + String(simulatedAccel, 2) + ",";
    payload += "\"gyro\":" + String(simulatedGyro, 2) + ",";
    payload += "\"speedPwm\":" + String(dutyCycle) + ",";
    payload += "\"direction\":\"" + currentDirection + "\",";
    payload += "\"motorRunning\":" + String(motorRunning ? "true" : "false") + ",";
    payload += "\"emergencyMode\":false";
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

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("No Internet");
        lcd.setCursor(0, 1);
        lcd.print("Motor Stopped");
    }

    if (connected && !wasWifiConnected) {
        Serial.println("WiFi reconnected");
        postEvent("Vehicle 2 reconnected");
    }

    wasWifiConnected = connected;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Vehicle 2 booting...");

    pinMode(motor1Pin1, OUTPUT);
    pinMode(motor1Pin2, OUTPUT);
    pinMode(enable1Pin, OUTPUT);
    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(enable1Pin, pwmChannel);
    ledcWrite(pwmChannel, dutyCycle);

    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Connecting...");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(800);
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connected");
    Serial.println("WiFi connected");
    wasWifiConnected = true;

    stopMotor();
    postEvent("Vehicle 2 online");
}

void loop() {
    enforceInternetSafety();

    if (millis() - lastControlPullMs > 700) {
        pullControlCommand();
        lastControlPullMs = millis();
    }

    if (millis() - lastPushMs > 2500) {
        sendHeartbeat();

        lcd.clear();
        lcd.setCursor(0, 0);
        if (obstacleDetected) {
            lcd.print("Obstacle Avoid");
            lcd.setCursor(0, 1);
            lcd.print("Dist: ");
            lcd.print(lastDistanceCm, 1);
            lcd.print(" cm");
        } else {
            lcd.print("Vehicle 2 Ready");
            lcd.setCursor(0, 1);
            lcd.print("LCD Display");
        }

        lastPushMs = millis();
    }

    delay(30);
}
