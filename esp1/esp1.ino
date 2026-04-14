#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <MPU6050_tockn.h>
int buzzer = 13;
// const char* ssid = "Dany";
// const char* password = "123456789";
const char* ssid = "...";
const char* password = "12345678";

const char* mqtt_server = "broker.hivemq.com";
WiFiClient espClient;
PubSubClient client(espClient);

#define SS_PIN 5
#define RST_PIN 4
MFRC522 rfid(SS_PIN, RST_PIN);

#define SERVO_PIN 25
Servo myServo;

MPU6050 mpu(Wire);

byte validUID[4] = {0xFB, 0xFA, 0xD0, 0xDB};

// ===== FSM =====
typedef enum {
  STATE_CLOSE,
  STATE_OPEN
} SystemState_t;

typedef enum {
  EVENT_NONE,
  EVENT_OPEN_CMD,
  EVENT_CLOSE_CMD,
  EVENT_RFID
} SystemEvent_t;

SystemState_t currentState = STATE_CLOSE;
SystemEvent_t currentEvent = EVENT_NONE;

// ===== TIMER =====
unsigned long lastMPU = 0;
unsigned long lastReconnect = 0;
unsigned long lastRFID = 0;

bool ignoreFirstMsg = true;

void setup_wifi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// ===== MQTT CALLBACK =====
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  msg.trim();

  Serial.print("TOPIC: ");
  Serial.println(topic);
  Serial.print("MQTT: ");
  Serial.println(msg);

  if (ignoreFirstMsg) {
    ignoreFirstMsg = false;
    Serial.println("IGNORE FIRST MESSAGE");
    return;
  }

  if (msg == "OPEN")  currentEvent = EVENT_OPEN_CMD;
  if (msg == "CLOSE") currentEvent = EVENT_CLOSE_CMD;
}

void reconnect() {
  if (millis() - lastReconnect < 3000) return;
  lastReconnect = millis();

  if (client.connect("ESP32_SAFE")) {
    client.subscribe("safe/control");
    Serial.println("MQTT connected");

    currentState = STATE_CLOSE;   // FSM
    currentEvent = EVENT_NONE;

    myServo.write(180);             // servo đóng
    delay(300);                   // cho servo kịp chạy

    client.publish("safe/status", "CLOSE", true);
    client.publish("safe/log", "SYSTEM INIT CLOSE");

    ignoreFirstMsg = true;
  }
}
void doOpen(String src) {
  Serial.println("OPEN by " + src);

  myServo.write(0);
  delay(300);

  digitalWrite(buzzer,LOW);
  client.publish("safe/status", "OPEN", true);
  client.publish("safe/log", ("OPEN by " + src).c_str());
}

void doClose(String src) {
  Serial.println("CLOSE by " + src);

  myServo.write(180);
  delay(300);

  client.publish("safe/status", "CLOSE", true);
  client.publish("safe/log", ("CLOSE by " + src).c_str());
}

void checkRFID() {
  if (millis() - lastRFID < 2000) return;
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  lastRFID = millis();

  Serial.print("UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  bool isValid = true;

  if (rfid.uid.size != 4) {
    isValid = false;
  } else {
    for (byte i = 0; i < 4; i++) {
      if (rfid.uid.uidByte[i] != validUID[i]) {
        isValid = false;
        break;
      }
    }
  }
  if (isValid) {
    currentEvent = EVENT_RFID;
  } else {
    client.publish("safe/log", "RFID FAIL");
  }
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void checkMPU() {
  if (currentState != STATE_CLOSE) return;
  if (millis() - lastMPU < 1000) return;
  lastMPU = millis();

  mpu.update();

  // ===== ACCEL =====
  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();

  float A = sqrt(ax * ax + ay * ay + az * az);

  // lọc nhiễu
  static float lastA = 1.0;
  float deltaA = abs(A - lastA);
  lastA = A;

  // ===== GYRO =====
  float gx = mpu.getGyroX();
  float gy = mpu.getGyroY();
  float gz = mpu.getGyroZ();

  float G = sqrt(gx * gx + gy * gy + gz * gz);
  char msg[200];
  sprintf(msg, "{\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f,\"A\":%.2f,\"G\":%.2f}",
          ax, ay, az, gx, gy, gz, A, G);
  client.publish("safe/mpu", msg);
  // ===== CHỐNG SPAM =====
  static unsigned long lastAlert = 0;

  float THRESHOLD_A = 0.4;
  float THRESHOLD_G = 120.0;

  if ((deltaA > THRESHOLD_A || G > THRESHOLD_G) &&
      millis() - lastAlert > 2000) {

    Serial.println("SHAKE DETECTED");

    client.publish("safe/alert", "SHAKE DETECTED!");
    client.publish("safe/log", "ALERT SHAKE");
    digitalWrite(buzzer,HIGH);
    delay(1000);
    digitalWrite(buzzer,LOW);
    lastAlert = millis();
  }
}

// ===== FSM =====
void ProcessSystem() {
  switch (currentState) {

    case STATE_CLOSE:
      switch (currentEvent) {

        case EVENT_OPEN_CMD:
          doOpen("WEB");
          currentState = STATE_OPEN;
          break;

        case EVENT_RFID:
          doOpen("RFID");
          currentState = STATE_OPEN;
          break;

        default: break;
      }
      break;

    case STATE_OPEN:
      switch (currentEvent) {

        case EVENT_CLOSE_CMD:
          doClose("WEB");
          currentState = STATE_CLOSE;
          break;

        case EVENT_RFID:
          doClose("RFID");
          currentState = STATE_CLOSE;
          break;

        default: break;
      }
      break;
  }

  currentEvent = EVENT_NONE;
}

void setup() {
  Serial.begin(115200);

  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  SPI.begin();
  rfid.PCD_Init();
  
  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN);
  myServo.write(180);

  Wire.begin(21, 22);
  mpu.begin();
  mpu.calcGyroOffsets(true);

  pinMode(buzzer,OUTPUT);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  checkRFID();
  checkMPU();
  ProcessSystem();
}