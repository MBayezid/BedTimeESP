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
#define RELAY_PIN 2 // ESP-01 GPIO2 (common; change to 0 for some "Black/Blue Shield" modules)
#define RELAY_ACTIVE_LOW true // Most ESP-01 relay shields are active LOW
#define EEPROM_SIZE 512
#define MAGIC_VAL 0xA5
/* =======================
   Timing / Throttling
   ======================= */
#define EEPROM_WRITE_COOLDOWN 5000UL
#define MQTT_RECONNECT_DELAY 3000UL
#define WIFI_RECONNECT_DELAY 10000UL
#define MQTT_KEEP_ALIVE 60 // Seconds
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
unsigned long lastWifiAttempt = 0;
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
  digitalWrite(RELAY_PIN, state ? (RELAY_ACTIVE_LOW ? LOW : HIGH) : (RELAY_ACTIVE_LOW ? HIGH : LOW));
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
  if (strcmp(topic, config.sub_topic) != 0) return;
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload, len)) return;
  if (doc.containsKey("switch") && doc["switch"] == 1 && doc.containsKey("command")) {
    const char* cmd = doc["command"];
    if (!strcmp(cmd, "on")) applyRelay(1);
    else if (!strcmp(cmd, "off")) applyRelay(0);
    publishState(true);
  }
}
void ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;
  if (millis() - lastMqttAttempt < MQTT_RECONNECT_DELAY) return;
  lastMqttAttempt = millis();
  mqtt.setServer(config.mqtt_broker, config.mqtt_port);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(256);
  mqtt.setKeepAlive(MQTT_KEEP_ALIVE);
  char clientId[32];
  snprintf(clientId, sizeof(clientId), "BedTimeESP-%06X", ESP.getChipId());
  if (mqtt.connect(clientId, config.mqtt_user, config.mqtt_pass, NULL, 0, false, NULL, true)) { // Clean session: true
    mqtt.subscribe(config.sub_topic);
    publishState(true);
  }
}
/* =======================
   WiFi Handling
   ======================= */
void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiAttempt < WIFI_RECONNECT_DELAY) return;
  lastWifiAttempt = millis();
  WiFi.begin(config.ssid, config.pass);
}
/* =======================
   Web Handlers
   ======================= */
void handleRoot() {
  // Tell the browser we are sending in chunks
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  // Header & Style
  server.sendContent_P(PSTR("<!DOCTYPE html><html><head><meta charset='utf-8'><title>BedTimeESP</title>"));
  server.sendContent_P(PSTR("<style>body{font-family:sans-serif;padding:20px;background:#f4f4f4;}"));
  server.sendContent_P(PSTR(".card{background:white;padding:20px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.1);max-width:400px;margin:auto;}"));
  server.sendContent_P(PSTR("input{width:100%;padding:8px;margin:5px 0;box-sizing:border-box;}</style></head><body><div class='card'>"));
  server.sendContent_P(PSTR("<h2>BedTimeESP Config</h2><form method='POST' action='/save'>"));
 
  // WiFi Section
  String s = "WiFi SSID:<br><input name='ssid' value='" + String(config.ssid) + "'><br>";
  s += "WiFi Pass:<br><input name='pass' type='password' value='" + String(config.pass) + "'><br>";
  server.sendContent(s);
  // System Section
  s = "Hostname:<br><input name='host' value='" + String(config.hostname) + "'><br>";
  server.sendContent(s);
  // MQTT Section
  s = "MQTT Broker:<br><input name='broker' value='" + String(config.mqtt_broker) + "'><br>";
  s += "MQTT Port:<br><input name='port' type='number' value='" + String(config.mqtt_port) + "'><br>";
  server.sendContent(s);
  s = "MQTT User:<br><input name='m_user' value='" + String(config.mqtt_user) + "'><br>";
  s += "MQTT Pass:<br><input name='m_pass' type='password' value='" + String(config.mqtt_pass) + "'><br>";
  server.sendContent(s);
  s = "Pub Topic:<br><input name='pub_t' value='" + String(config.pub_topic) + "'><br>";
  s += "Sub Topic:<br><input name='sub_t' value='" + String(config.sub_topic) + "'><br>";
  server.sendContent(s);
  server.sendContent_P(PSTR("<br><button type='submit' style='width:100%;padding:10px;background:#2ecc71;color:white;border:none;'>Save & Reboot</button></form>"));
  server.sendContent_P(PSTR("</div></body></html>"));
 
  server.sendContent(""); // End transmission
}
void handleSave() {
  strncpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid));
  config.ssid[sizeof(config.ssid) - 1] = '\0';
  strncpy(config.pass, server.arg("pass").c_str(), sizeof(config.pass));
  config.pass[sizeof(config.pass) - 1] = '\0';
  strncpy(config.hostname, server.arg("host").c_str(), sizeof(config.hostname));
  config.hostname[sizeof(config.hostname) - 1] = '\0';
  strncpy(config.mqtt_broker, server.arg("broker").c_str(), sizeof(config.mqtt_broker));
  config.mqtt_broker[sizeof(config.mqtt_broker) - 1] = '\0';
  strncpy(config.mqtt_user, server.arg("m_user").c_str(), sizeof(config.mqtt_user));
  config.mqtt_user[sizeof(config.mqtt_user) - 1] = '\0';
  strncpy(config.mqtt_pass, server.arg("m_pass").c_str(), sizeof(config.mqtt_pass));
  config.mqtt_pass[sizeof(config.mqtt_pass) - 1] = '\0';
  strncpy(config.pub_topic, server.arg("pub_t").c_str(), sizeof(config.pub_topic));
  config.pub_topic[sizeof(config.pub_topic) - 1] = '\0';
  strncpy(config.sub_topic, server.arg("sub_t").c_str(), sizeof(config.sub_topic));
  config.sub_topic[sizeof(config.sub_topic) - 1] = '\0';
  int port = server.arg("port").toInt();
  if (port > 0 && port <= 65535) {
    config.mqtt_port = static_cast<uint16_t>(port);
  } // Else keep previous
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
void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}
/* =======================
   Setup / Loop
   ======================= */
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW); // Initial off
  EEPROM.begin(EEPROM_SIZE);
  loadConfig();
  // Apply last relay state IMMEDIATELY
  applyRelay(config.last_state);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(config.hostname);
  WiFi.begin(config.ssid, config.pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) delay(100);
  MDNS.begin(config.hostname);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", handleStatus);
  server.on("/reboot", handleReboot);
  server.begin();
}
void loop() {
  server.handleClient();
  ensureWifi();
  ensureMqtt();
  mqtt.loop();
}