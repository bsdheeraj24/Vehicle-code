#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp32-hal-ledc.h>

#define motor1Pin1 27
#define motor1Pin2 26
#define enable1Pin 14

const char* ssid = "Dheeraj";
const char* password = "dheerubs";
const char* serverBaseUrl = "https://your-render-service.onrender.com";

LiquidCrystal_I2C lcd(0x27, 16, 2);
const int freq = 30000;
const int pwmChannel = 0;
const int resolution = 8;
int dutyCycle = 200;
bool motorRunning = false;
unsigned long lastPushMs = 0;
float lastDistanceCm = -1;
bool obstacleDetected = false;
bool previousObstacleDetected = false;

const float obstacleThresholdCm = 30.0;

void moveForward() {
    digitalWrite(motor1Pin1, HIGH);
    digitalWrite(motor1Pin2, LOW);
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
    payload += "\"motorRunning\":" + String(motorRunning ? "true" : "false") + ",";
    payload += "\"emergencyMode\":false";
    payload += "}";

    http.POST(payload);
    http.end();
}

void setup() {
    Serial.begin(115200);

    pinMode(motor1Pin1, OUTPUT);
    pinMode(motor1Pin2, OUTPUT);
    pinMode(enable1Pin, OUTPUT);
    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(enable1Pin, pwmChannel);

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

    moveForward();
    postEvent("Vehicle 2 online");
}

void loop() {
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
