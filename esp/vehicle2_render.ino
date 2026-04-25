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
const int minStartPwm = 110;
bool motorRunning = false;
unsigned long lastPushMs = 0;
unsigned long lastControlPullMs = 0;
unsigned long lastReconnectAttemptMs = 0;
float lastDistanceCm = -1;
bool obstacleDetected = false;
bool previousObstacleDetected = false;
bool wasWifiConnected = false;
String currentDirection = "stop";
bool vehicle1ObstacleAlert = false;
int vehicle1SpeedPwm = 0;

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

String extractVehicleObject(String payload, String vehicleId) {
    String marker = "\"id\":\"" + vehicleId + "\"";
    int idIndex = payload.indexOf(marker);
    if (idIndex < 0) return "";

    int objectStart = payload.lastIndexOf('{', idIndex);
    if (objectStart < 0) return "";

    int depth = 0;
    for (int i = objectStart; i < payload.length(); i++) {
        if (payload[i] == '{') depth++;
        if (payload[i] == '}') {
            depth--;
            if (depth == 0) {
                return payload.substring(objectStart, i + 1);
            }
        }
    }

    return "";
}

bool containsObstacleAlert(String eventText) {
    String lower = eventText;
    lower.toLowerCase();
    return lower.indexOf("collision warning") >= 0 || lower.indexOf("obstacle") >= 0;
}

void pullVehicle1Status() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = String(serverBaseUrl) + "/api/status";
    http.begin(url);
    int statusCode = http.GET();
    if (statusCode != 200) {
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    String vehicle1Object = extractVehicleObject(payload, "vehicle1");
    if (vehicle1Object.length() == 0) return;

    String speedValue = readJsonValue(vehicle1Object, "speedPwm");
    if (speedValue.length() > 0) {
        vehicle1SpeedPwm = speedValue.toInt();
    }

    String emergencyValue = readJsonValue(vehicle1Object, "emergencyMode");
    String eventValue = readJsonValue(vehicle1Object, "lastEvent");
    bool emergencyMode = emergencyValue == "true";
    vehicle1ObstacleAlert = emergencyMode || containsObstacleAlert(eventValue);
}

void updateLcdDisplay() {
    lcd.clear();
    lcd.setCursor(0, 0);

    if (vehicle1ObstacleAlert) {
        lcd.print("ALERT: V1 OBS");
        lcd.setCursor(0, 1);
        lcd.print("STOP / WAIT");
        return;
    }

    lcd.print("V1 Speed PWM");
    lcd.setCursor(0, 1);
    lcd.print("PWM: ");
    lcd.print(vehicle1SpeedPwm);
}

void applyControlCommand(String direction, int speedPwm) {
    dutyCycle = constrain(speedPwm, 0, 255);

    if (WiFi.status() != WL_CONNECTED) {
        stopMotor();
        return;
    }

    if (direction != "stop" && dutyCycle > 0 && dutyCycle < minStartPwm) {
        dutyCycle = minStartPwm;
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
        Serial.print("Vehicle 2 control GET failed: ");
        Serial.println(statusCode);
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    String direction = readJsonValue(payload, "direction");
    String speedValue = readJsonValue(payload, "speedPwm");
    if (direction.length() == 0 || speedValue.length() == 0) return;

    int speedPwm = speedValue.toInt();
    Serial.print("Vehicle 2 cmd -> dir: ");
    Serial.print(direction);
    Serial.print(", pwm: ");
    Serial.println(speedPwm);
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
        pullVehicle1Status();
        if (WiFi.status() == WL_CONNECTED) {
            updateLcdDisplay();
        }
        lastControlPullMs = millis();
    }

    if (millis() - lastPushMs > 2500) {
        sendHeartbeat();
        if (WiFi.status() == WL_CONNECTED) {
            updateLcdDisplay();
        }

        lastPushMs = millis();
    }

    delay(30);
}
