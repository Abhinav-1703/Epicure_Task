#include <WiFi.h>
#include <PubSubClient.h>

// ========== USER CONFIGURATION ==========
const char* WIFI_SSID = "xxx";
const char* WIFI_PASS = "xxx";
const char* BROKER_IP = "xxx";
const int   BROKER_PORT = 1883;

// ========== LED CONFIG ==========
#define LED_ON  LOW
#define LED_OFF HIGH

// ========== TOPICS ==========
const char* MQTT_TOPIC_SUB = "epicure/commands";
const char* MQTT_TOPIC_PUB = "epicure/status";
const char* MQTT_CLIENT_ID = "epicure_esp_bridge";

// ========== HARDWARE PINS ==========
#define STM_UART Serial2
const int PIN_TX = 17;
const int PIN_RX = 16;
const int STATUS_LED = 2;

// ========== STATE VARIABLES ==========
unsigned long lastReconnectAttempt = 0;
unsigned long lastBlink = 0;
unsigned long lastPingTime = 0;
unsigned long lastPongReceived = 0;
int ledState = LED_OFF;

// State Tracking Flags (For logging changes)
bool prevWiFiConnected = false;
bool prevMQTTConnected = false;
bool stmConnected = false;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// ========== PROTOTYPES ==========
void maintainWiFi();
void maintainMQTT();
void maintainSTM32(); // Manages Heartbeat
void handleIncomingUART();
void setStatusLED(int mode);
void blinkLED(int interval);
void forwardToSTM32(String msg);
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool validateCommand(String cmd);

enum {
  MODE_ALL_GOOD = 1, // solid ON
  MODE_MQTT_RETRY = 2, // medium blink
  MODE_WIFI_RETRY = 3, // fast blink
  MODE_STM_MISSING = 4 // slow blink (very slow)
};

void setup() {
  Serial.begin(115200);
  STM_UART.begin(115200, SERIAL_8N1, PIN_RX, PIN_TX);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LED_OFF);

  Serial.println("\n[SYS] ESP32 Advanced Bridge Starting...");

  // NOTE: PubSubClient versions may not have setKeepAlive; comment out if compile fails.
  // client.setKeepAlive(5);
  client.setServer(BROKER_IP, BROKER_PORT);
  client.setCallback(mqttCallback);

  WiFi.mode(WIFI_STA);

  // Force immediate connect attempt
  lastReconnectAttempt = millis() - 10000;
}

void loop() {
  maintainWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    maintainMQTT();
  }

  if (client.connected()) {
    client.loop();

    // Handle UART (ACKs & Pongs)
    handleIncomingUART();

    // Handle STM Heartbeat
    maintainSTM32();

    // Visual Status
    if (!stmConnected) {
      setStatusLED(MODE_STM_MISSING);
    } else {
      setStatusLED(MODE_ALL_GOOD);
    }
  } else {
    // Not connected to broker -> show MQTT retry
    setStatusLED(MODE_MQTT_RETRY);
  }
}

// ========== CONNECTION MANAGERS ==========
void maintainWiFi() {
  bool currentWiFi = (WiFi.status() == WL_CONNECTED);

  // Log State Change
  if (currentWiFi != prevWiFiConnected) {
    if (currentWiFi) {
      Serial.print("[WIFI] Connected! IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("[ERR] WiFi Connection Lost!");
    }
    prevWiFiConnected = currentWiFi;
  }

  // Reconnection Logic
  if (!currentWiFi) {
    setStatusLED(MODE_WIFI_RETRY);
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck > 500) {
      lastWiFiCheck = millis();
      if (WiFi.status() != WL_CONNECTED) {
         WiFi.begin(WIFI_SSID, WIFI_PASS);
         Serial.print("[WIFI] Attempting to connect to ");
         Serial.println(WIFI_SSID);
      }
    }
  }
}

void maintainMQTT() {
  bool currentMQTT = client.connected();

  // Log State Change
  if (currentMQTT != prevMQTTConnected) {
    if (currentMQTT) {
      Serial.println("[MQTT] Broker Connected.");
      client.publish(MQTT_TOPIC_PUB, "ESP32 Online");
      // Re-subscribe if needed
      client.subscribe(MQTT_TOPIC_SUB);
    } else {
      Serial.println("[ERR] MQTT Connection Lost!");
    }
    prevMQTTConnected = currentMQTT;
  }

  // Reconnection Logic
  if (!currentMQTT) {
    setStatusLED(MODE_MQTT_RETRY);
    unsigned long now = millis();
    // Try every 2 seconds (faster recovery)
    if (now - lastReconnectAttempt > 2000) {
      lastReconnectAttempt = now;
      Serial.print("[MQTT] Connecting...");
      if (client.connect(MQTT_CLIENT_ID)) {
        Serial.println(" OK");
        client.subscribe(MQTT_TOPIC_SUB);
      } else {
        Serial.print(" Fail (rc=");
        Serial.print(client.state());
        Serial.println(")");
      }
    }
  }
}

void maintainSTM32() {
  unsigned long now = millis();

  // 1. Send PING every 2 seconds
  if (now - lastPingTime > 2000) {
    lastPingTime = now;
    forwardToSTM32("ping"); // Internal command
  }

  // 2. Check for Timeout (No Pong for 5 seconds)
  if (now - lastPongReceived > 5000) {
    if (stmConnected) {
      Serial.println("[ERR] STM32 Not Responding (Check Wiring!)");
      client.publish(MQTT_TOPIC_PUB, "STM:OFFLINE");
      client.publish(MQTT_TOPIC_PUB, "ERR:STM_TIMEOUT");
      stmConnected = false;
    }
  }
}

// ========== UART HANDLING ==========
void handleIncomingUART() {
  static int state = 0;
  static int len = 0;
  static int idx = 0;
  static uint8_t checksum = 0;
  static char buffer[128];

  while (STM_UART.available()) {
    uint8_t b = STM_UART.read();

    switch (state) {
      case 0: if (b == 0xAA) state = 1; break; // Start
      case 1: // Length
        len = b;
        if (len > 0 && len < 127) { idx = 0; checksum = 0; state = 2; }
        else state = 0;
        break;
      case 2: // Payload
        buffer[idx++] = (char)b;
        checksum += b;
        if (idx >= len) state = 3;
        break;
      case 3: // Checksum
        if (b == checksum) {
          buffer[len] = '\0';
          String payload = String(buffer);

          // Check if it's a PONG
          if (payload == "ACK:pong") {
            lastPongReceived = millis();
            if (!stmConnected) {
              Serial.println("[STM] Connection Established.");
              stmConnected = true;
              client.publish(MQTT_TOPIC_PUB, "STM:ONLINE");
            }
          } else {
             Serial.print("[UART] Msg: "); Serial.println(payload);
             client.publish(MQTT_TOPIC_PUB, payload.c_str());
          }
        } else {
          Serial.println("[UART] Checksum mismatch, dropping packet.");
        }
        state = 0;
        break;
    }
  }
}

// ========== HELPERS ==========
void setStatusLED(int mode) {
  if (mode == MODE_ALL_GOOD) { digitalWrite(STATUS_LED, LED_ON); return; } // Solid ON

  // Blink Patterns
  int interval = 500;
  if (mode == MODE_WIFI_RETRY) interval = 100; // WiFi (Fast)
  if (mode == MODE_MQTT_RETRY) interval = 500; // MQTT (Medium)
  if (mode == MODE_STM_MISSING) interval = 1000; // STM Missing (Very Slow)

  blinkLED(interval);
}

void blinkLED(int interval) {
  if (millis() - lastBlink > (unsigned long)interval) {
    lastBlink = millis();
    ledState = (ledState == LED_OFF) ? LED_ON : LED_OFF;
    digitalWrite(STATUS_LED, ledState);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (length > 200) return; // guard
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  Serial.print("[RX] "); Serial.println(message);
  if (validateCommand(message)) forwardToSTM32(message);
}

bool validateCommand(String cmd) {
  cmd.trim();
  if (cmd.startsWith("led:")) return true;
  if (cmd.startsWith("motor:")) return true;
  return false;
}

void forwardToSTM32(String msg) {
  uint8_t len = (uint8_t)msg.length();
  uint8_t checksum = 0;
  const char* cstr = msg.c_str();
  for (int i = 0; i < len; i++) checksum += (uint8_t)cstr[i];

  STM_UART.write(0xAA);
  STM_UART.write(len);
  // Ensure raw bytes (no extra formatting) sent:
  STM_UART.write((const uint8_t*)cstr, len);
  STM_UART.write(checksum);

  Serial.print("[TX->STM] "); Serial.println(msg);
}
