#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

/* =======================
   Hardware Configuration
   ======================= */
#define RELAY_PIN 2          // ESP-01 GPIO2
#define EEPROM_SIZE 512
#define MAGIC_VAL 0xA5

/* =======================
   Timing / Throttling
   ======================= */
#define EEPROM_WRITE_COOLDOWN 5000UL
#define MQTT_RECONNECT_DELAY  3000UL

/* =======================
   Global Objects
   ======================= */
ESP8266WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

/* =======================
   Persistent Config
   ======================= */
struct Config {
  uint8_t magic;
  char hostname[32];
  char ssid[32];
  char pass[64];
  uint8_t ap_fallback;
  uint8_t last_state;
  char mqtt_broker[64];
  uint16_t mqtt_port;
  char mqtt_user[32];
  char mqtt_pass[32];
  char pub_topic[64];
  char sub_topic[64];
};

Config config;

/* =======================
   Runtime State
   ======================= */
unsigned long lastEepromWrite = 0;
unsigned long lastMqttAttempt = 0;

/* =======================
   PROGMEM Assets
   ======================= */
const char PAGE_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="utf-8"><title>BedTimeESP</title></head>
<body>
<h2>BedTimeESP Configuration</h2>
<form method="POST" action="/save">
WiFi SSID:<br><input name="ssid"><br>
WiFi Pass:<br><input name="pass"><br>
Hostname:<br><input name="host"><br><br>
MQTT Broker:<br><input name="broker"><br>
MQTT Port:<br><input name="port"><br>
MQTT User:<br><input name="m_user"><br>
MQTT Pass:<br><input name="m_pass"><br>
Pub Topic:<br><input name="pub_t"><br>
Sub Topic:<br><input name="sub_t"><br><br>
<input type="submit" value="Save">
</form>
</body></html>
)rawliteral";

/* =======================
   Utility Functions
   ======================= */
void saveConfig() {
  if (millis() - lastEepromWrite < EEPROM_WRITE_COOLDOWN) return;
  EEPROM.put(0, config);
  EEPROM.commit();
  lastEepromWrite = millis();
}

void loadConfig() {
  EEPROM.get(0, config);
  if (config.magic != MAGIC_VAL) {
    memset(&config, 0, sizeof(Config));
    config.magic = MAGIC_VAL;
    strcpy(config.hostname, "BedTimeESP");
    config.ap_fallback = 1;
    config.last_state = 0;
    config.mqtt_port = 1883;
    saveConfig();
  }
}

/* =======================
   Relay Control
   ======================= */
void applyRelay(uint8_t state) {
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  config.last_state = state;
  saveConfig();
}

/* =======================
   MQTT Handling
   ======================= */
void publishState(bool success) {
  StaticJsonDocument<128> doc;
  doc["switch"] = 1;
  doc["state"] = config.last_state ? "on" : "off";
  doc["success"] = success;

  char payload[128];
  serializeJson(doc, payload);
  mqtt.publish(config.pub_topic, payload, true);
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload, len)) return;

  if (doc.containsKey("command")) {
    const char* cmd = doc["command"];
    if (!strcmp(cmd, "on")) applyRelay(1);
    else if (!strcmp(cmd, "off")) applyRelay(0);
    publishState(true);
  }
}

void ensureMqtt() {
  if (mqtt.connected()) return;
  if (millis() - lastMqttAttempt < MQTT_RECONNECT_DELAY) return;
  lastMqttAttempt = millis();

  mqtt.setServer(config.mqtt_broker, config.mqtt_port);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(256);

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "BedTimeESP-%06X", ESP.getChipId());

  if (mqtt.connect(clientId, config.mqtt_user, config.mqtt_pass)) {
    mqtt.subscribe(config.sub_topic);
    publishState(true);
  }
}

/* =======================
   Web Handlers
   ======================= */
void handleRoot() {
  server.send_P(200, "text/html", PAGE_INDEX);
}

void handleSave() {
  strncpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid));
  strncpy(config.pass, server.arg("pass").c_str(), sizeof(config.pass));
  strncpy(config.hostname, server.arg("host").c_str(), sizeof(config.hostname));
  strncpy(config.mqtt_broker, server.arg("broker").c_str(), sizeof(config.mqtt_broker));
  strncpy(config.mqtt_user, server.arg("m_user").c_str(), sizeof(config.mqtt_user));
  strncpy(config.mqtt_pass, server.arg("m_pass").c_str(), sizeof(config.mqtt_pass));
  strncpy(config.pub_topic, server.arg("pub_t").c_str(), sizeof(config.pub_topic));
  strncpy(config.sub_topic, server.arg("sub_t").c_str(), sizeof(config.sub_topic));
  config.mqtt_port = server.arg("port").toInt();

  saveConfig();
  server.send(200, "text/plain", "Saved. Rebooting...");
  delay(500);
  ESP.restart();
}

void handleStatus() {
  StaticJsonDocument<64> doc;
  doc["state"] = config.last_state ? "on" : "off";
  char out[64];
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* =======================
   Setup / Loop
   ======================= */
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  // Apply last relay state IMMEDIATELY
  applyRelay(config.last_state);

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) delay(100);

  if (WiFi.status() != WL_CONNECTED && config.ap_fallback) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(config.hostname);
  }

  MDNS.begin(config.hostname);

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", handleStatus);
  server.begin();
}

void loop() {
  server.handleClient();
  ensureMqtt();
  mqtt.loop();
}
