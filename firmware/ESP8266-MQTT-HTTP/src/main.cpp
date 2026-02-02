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
#define RELAY_PIN 2
#define RELAY_ACTIVE_LOW true
#define EEPROM_SIZE 512
#define MAGIC_VAL 0xA5
/* =======================
   Timing & Stability
   ======================= */
#define EEPROM_WRITE_COOLDOWN 5000UL
#define MQTT_RECONNECT_DELAY 5000UL
#define WIFI_RECONNECT_DELAY 10000UL
#define HEARTBEAT_INTERVAL 60000UL
#define MIN_SAFE_HEAP 7500 // Threshold to kill AP
#define SAFE_HEAP_RECOVER 11500 // Threshold to restore AP
/* =======================
   Global Objects
   ======================= */
ESP8266WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
struct Config {
  uint8_t magic;
  char hostname[32];
  char ssid[32];
  char pass[64];
  uint8_t last_state;
  char mqtt_broker[64];
  uint16_t mqtt_port;
  char mqtt_user[32];
  char mqtt_pass[32];
  char pub_topic[64]; // State Topic (JSON)
  char sub_topic[64]; // Command Topic (JSON)
  char avail_topic[64]; // Availability Topic (online/offline)
};
Config config;
unsigned long lastEepromWrite = 0, lastMqttAttempt = 0, lastWifiAttempt = 0, lastHeartbeat = 0;
bool apDisabledByGuard = false;
/* =======================
   Persistence
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
    strcpy(config.pub_topic, "home/switch/status");
    strcpy(config.sub_topic, "home/switch/control");
    strcpy(config.avail_topic, "home/switch/availability");
    config.last_state = 0;
    config.mqtt_port = 1883;
    saveConfig();
  }
}
/* =======================
   Relay & MQTT Logic
   ======================= */
void applyRelay(uint8_t state) {
  digitalWrite(RELAY_PIN, state ? (RELAY_ACTIVE_LOW ? LOW : HIGH) : (RELAY_ACTIVE_LOW ? HIGH : LOW));
  config.last_state = state;
  saveConfig();
}
void publishState() {
  if (!mqtt.connected()) return;
  StaticJsonDocument<192> doc; // Increased buffer
  doc["switch"] = 1;
  doc["state"] = config.last_state ? "on" : "off";
  doc["heap"] = ESP.getFreeHeap();
  doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

  char payload[192];
  serializeJson(doc, payload);
  mqtt.publish(config.pub_topic, payload, true);
}
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  if (strcmp(topic, config.sub_topic) != 0) return;
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload, len)) return;
  if (doc.containsKey("command")) {
    const char* cmd = doc["command"];
    if (!strcmp(cmd, "on")) applyRelay(1);
    else if (!strcmp(cmd, "off")) applyRelay(0);
    publishState();
  }
}
void ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED || strlen(config.mqtt_broker) < 3) return;
  if (mqtt.connected()) return;
  if (millis() - lastMqttAttempt < MQTT_RECONNECT_DELAY) return;

  lastMqttAttempt = millis();
  mqtt.setServer(config.mqtt_broker, config.mqtt_port);
  mqtt.setCallback(mqttCallback);

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "BedTimeESP-%06X", ESP.getChipId());

  // Birth & LWT Logic (QoS 1, Retained)
  if (mqtt.connect(clientId, config.mqtt_user, config.mqtt_pass, config.avail_topic, 1, true, "offline")) {
    // Note: Birth is QoS 0 (PubSubClient limitation); LWT is QoS 1 via broker.
    mqtt.publish(config.avail_topic, "online", true); // Birth Message
    mqtt.subscribe(config.sub_topic);
    publishState();
  }
}
void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (strlen(config.ssid) < 1) return; // Prevent spam on empty config
  if (millis() - lastWifiAttempt < WIFI_RECONNECT_DELAY) return;
  lastWifiAttempt = millis();
  WiFi.begin(config.ssid, config.pass);
}
/* =======================
   Stability & Web UI
   ======================= */
void heapGuard() {
  uint32_t freeHeap = ESP.getFreeHeap();
  if (!apDisabledByGuard && freeHeap < MIN_SAFE_HEAP) {
    WiFi.softAPdisconnect(true);
    apDisabledByGuard = true;
  } else if (apDisabledByGuard && freeHeap > SAFE_HEAP_RECOVER) {
    WiFi.softAP(config.hostname);
    apDisabledByGuard = false;
  }
}
void handleSave() {
  auto updateField = [](char* dest, const char* argName, size_t size) {
    if (server.hasArg(argName)) {
      strncpy(dest, server.arg(argName).c_str(), size);
      dest[size - 1] = '\0';
    }
  };
  updateField(config.ssid, "ssid", sizeof(config.ssid));
  updateField(config.pass, "pass", sizeof(config.pass));
  updateField(config.hostname, "host", sizeof(config.hostname));
  updateField(config.mqtt_broker, "broker", sizeof(config.mqtt_broker));
  updateField(config.mqtt_user, "m_user", sizeof(config.mqtt_user));
  updateField(config.mqtt_pass, "m_pass", sizeof(config.mqtt_pass));
  updateField(config.pub_topic, "pub_t", sizeof(config.pub_topic));
  updateField(config.sub_topic, "sub_t", sizeof(config.sub_topic));
  updateField(config.avail_topic, "avail_t", sizeof(config.avail_topic));

  if (server.hasArg("port")) {
    long p = server.arg("port").toInt();
    if (p >= 1 && p <= 65535) config.mqtt_port = (uint16_t)p;
  }
  saveConfig();
  server.send(200, "text/plain", "Saved. Rebooting...");
  delay(1200);
  ESP.restart();
}
void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(PSTR("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>BedTimeESP</title><style>body{font-family:sans-serif;padding:10px;background:#f4f4f4;} .card{background:white;padding:20px;border-radius:10px;max-width:400px;margin:auto;} input{width:100%;padding:8px;margin:5px 0;box-sizing:border-box;} button{width:100%;padding:10px;background:#2ecc71;color:white;border:none;border-radius:5px;margin-top:10px;}</style></head><body><div class='card'><h2>BedTimeESP Config</h2><form method='POST' action='/save'>"));

  auto sendInput = [](const char* label, const char* name, const char* val, const char* type="text") {
    server.sendContent(String(label) + ":<br><input name='" + String(name) + "' type='" + String(type) + "' value='" + String(val) + "'><br>");
  };
  sendInput("WiFi SSID", "ssid", config.ssid);
  sendInput("WiFi Pass", "pass", config.pass, "password");
  sendInput("Hostname", "host", config.hostname);
  sendInput("MQTT Broker", "broker", config.mqtt_broker);
  sendInput("MQTT Port", "port", String(config.mqtt_port).c_str(), "number");
  sendInput("MQTT User", "m_user", config.mqtt_user);
  sendInput("MQTT Pass", "m_pass", config.mqtt_pass, "password");
  sendInput("State Topic", "pub_t", config.pub_topic);
  sendInput("Command Topic", "sub_t", config.sub_topic);
  sendInput("Availability Topic", "avail_t", config.avail_topic);
  server.sendContent_P(PSTR("<button type='submit'>Save & Reboot</button></form></div></body></html>"));
  server.sendContent("");
}
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  EEPROM.begin(EEPROM_SIZE);
  loadConfig();
  applyRelay(config.last_state);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(config.hostname);
  WiFi.begin(config.ssid, config.pass);
  MDNS.begin(config.hostname);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", [](){
      StaticJsonDocument<128> doc;
      doc["state"] = config.last_state ? "on" : "off";
      doc["heap"] = ESP.getFreeHeap();
      doc["ap_disabled"] = apDisabledByGuard;
      char out[128]; serializeJson(doc, out); server.send(200, "application/json", out);
  });
  server.begin();
}
void loop() {
  server.handleClient();
  MDNS.update();
  ensureWifi();
  ensureMqtt();
  mqtt.loop();
  heapGuard();
  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    publishState();
  }
}