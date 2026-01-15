// ============================================================================
// main.cpp - ESP-01 (512KB) optimized developer-friendly firmware
// Production mode: compile WITHOUT DEBUG defined
// Developer mode: add  -DDEBUG  in platformio.ini
// ============================================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// ============================================================================
// CONFIGURATION / CONSTANTS
// ============================================================================
#define EEPROM_SIZE 256  // Total EEPROM size allocated for configuration
#define MAGIC_ADDR  0    // Address for magic byte to check if EEPROM is initialized
#define MAGIC_VAL   0xA5 // Magic value indicating valid configuration
#define HOST_ADDR   1    // Start address for hostname storage
#define SSID_ADDR   33   // Start address for WiFi SSID
#define PASS_ADDR   65   // Start address for WiFi password
#define APF_ADDR    129  // Address for AP fallback flag
#define STATE_ADDR  130  // Address for last relay state

// GPIO (ESP-01 supports only 0 & 2)
static const uint8_t RELAY_PIN = 2;  // Pin connected to the relay

// MQTT Broker defaults (can be overridden if needed)
static const char* PUB_TOPIC  = "home/switch/status";  // Topic for publishing status
static const char* SUB_TOPIC  = "home/switch/control"; // Topic for subscribing to commands
static const char* LWT_TOPIC  = "home/switch/alert";   // Last Will and Testament topic
static const char* HB_TOPIC   = "home/switch/heartbeat"; // Heartbeat topic

char mqttServer[64] = "broker.emqx.io"; // Default MQTT broker server
uint16_t mqttPort    = 1883;            // Default MQTT broker port

// ============================================================================
// GLOBAL STATE
// ============================================================================
char hostnameLocal[32] = "remoteswitch"; // Default local hostname
char storedSSID[32]    = "";             // Stored WiFi SSID
char storedPASS[64]    = "";             // Stored WiFi password
bool apFallback         = true;          // Flag to enable AP fallback mode
int relayState          = 0;             // Current state of the relay (0: OFF, 1: ON)

ESP8266WebServer web(80);               // Web server instance on port 80
WiFiClient espClient;                   // WiFi client for MQTT
PubSubClient mqtt(espClient);           // MQTT client instance

unsigned long lastHeartbeat = 0;                        // Timestamp of last heartbeat
static const unsigned long HB_INTERVAL     = 30000UL;   // Heartbeat interval in ms (30 seconds)
unsigned long lastStateWrite = 0;                       // Timestamp of last EEPROM state write
static const unsigned long STATE_WRITE_MIN = 5000UL;    // Minimum delay between EEPROM writes (5 seconds) to prevent wear

// ============================================================================
// HTML UI (ultra-compact for flash saving)
// ============================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Remote Switch</title>
<style>
body{font-family:Arial;margin:12px;background:#f5f5f5}
.card{max-width:420px;margin:auto;background:#fff;padding:16px;border-radius:8px}
button,input{width:100%;padding:10px;margin-top:8px;font-size:15px}
.on{background:#4CAF50;color:#fff;border:none}
.off{background:#F44336;color:#fff;border:none}
.cfg{background:#2196F3;color:#fff;border:none}
label{font-size:13px;color:#444}
.small{font-size:12px;color:#777;text-align:center;margin-top:10px}
.hidden{display:none}
</style>
</head>

<body>
<div class='card'>

<h2>Remote Switch</h2>

<p>Status: <b id='st'>---</b></p>

<button class='on' onclick="cmd('/on')">Turn ON</button>
<button class='off' onclick="cmd('/off')">Turn OFF</button>

<button class='cfg' onclick="toggleCfg()">Wi-Fi Settings</button>

<div id="cfg" class="hidden">
<hr>
<form method="POST" action="/save">
<label>Wi-Fi SSID</label>
<input name="ssid" required>

<label>Password</label>
<input name="pass" type="password">

<label>Device Name (optional)</label>
<input name="host">

<button type="submit">Save & Reboot</button>
</form>
</div>

<p class='small'>
If not connected, join device AP and open 192.168.4.1
</p>

</div>

<script>
function toggleCfg(){
 let c=document.getElementById('cfg');
 c.style.display = c.style.display==='none'?'block':'none';
}

async function cmd(p){
 try{await fetch(p);}catch(e){}
 setTimeout(update,300);
}

async function update(){
 try{
  let r=await fetch('/status');
  let j=await r.json();
  document.getElementById('st').innerText=j.state.toUpperCase();
 }catch(e){}
}

update();
</script>

</body>
</html>
)rawliteral";  // Compact HTML for the web interface, stored in PROGMEM to save RAM

// ============================================================================
// EEPROM HELPERS
// ============================================================================
void eWriteStr(int addr, const char* s, int maxLen) {
  // Write a string to EEPROM, null-terminated, up to maxLen
  int n = strlen(s);
  for (int i = 0; i < maxLen; i++)
    EEPROM.write(addr + i, (i < n) ? s[i] : 0);
}

void eReadStr(int addr, char* buf, int maxLen) {
  // Read a null-terminated string from EEPROM into buf, up to maxLen
  for (int i = 0; i < maxLen; i++) {
    buf[i] = EEPROM.read(addr + i);
    if (buf[i] == 0) break;
  }
  buf[maxLen - 1] = 0;  // Ensure null-termination
}

void loadConfig() {
  // Initialize and load configuration from EEPROM
  EEPROM.begin(EEPROM_SIZE);

  if (EEPROM.read(MAGIC_ADDR) != MAGIC_VAL) {
    // First boot or corrupted: Initialize with defaults
    eWriteStr(HOST_ADDR, hostnameLocal, 32);
    eWriteStr(SSID_ADDR, "", 32);
    eWriteStr(PASS_ADDR, "", 64);
    EEPROM.write(APF_ADDR, apFallback ? 1 : 0);
    EEPROM.write(STATE_ADDR, relayState ? 1 : 0);
    EEPROM.write(MAGIC_ADDR, MAGIC_VAL);
    EEPROM.commit();
  }

  // Load stored values into global variables
  eReadStr(HOST_ADDR, hostnameLocal, 32);
  eReadStr(SSID_ADDR, storedSSID, 32);
  eReadStr(PASS_ADDR, storedPASS, 64);
  apFallback  = EEPROM.read(APF_ADDR) == 1;
  relayState  = EEPROM.read(STATE_ADDR) == 1;
}

void saveConfigWiFi(const char* ssid, const char* pass, const char* host) {
  // Save WiFi configuration to EEPROM
  eWriteStr(SSID_ADDR, ssid, 32);
  eWriteStr(PASS_ADDR, pass, 64);
  eWriteStr(HOST_ADDR, host, 32);
  EEPROM.commit();

  // Reload the saved values into globals
  eReadStr(SSID_ADDR, storedSSID, 32);
  eReadStr(PASS_ADDR, storedPASS, 64);
  eReadStr(HOST_ADDR, hostnameLocal, 32);
}

void saveRelayState(int s) {
  // Save relay state to EEPROM with throttling to reduce wear
  unsigned long now = millis();
  if (now - lastStateWrite < STATE_WRITE_MIN) return;

  EEPROM.write(STATE_ADDR, s ? 1 : 0);
  EEPROM.commit();
  lastStateWrite = now;
}

// ============================================================================
// RELAY
// ============================================================================
void applyRelay(int s) {
  // Apply the relay state and persist it
  relayState = (s ? 1 : 0);
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  saveRelayState(relayState);

#ifdef DEBUG
  Serial.printf("Relay -> %s\n", relayState ? "ON" : "OFF");  // Debug output
#endif
}

// ============================================================================
// WEB HANDLERS
// ============================================================================
void handleRoot()      { web.send_P(200, "text/html", INDEX_HTML); }  // Serve the main HTML page

void handleOn()        { applyRelay(1); web.send(302, "text/plain", ""); }  // Turn relay ON and redirect

void handleOff()       { applyRelay(0); web.send(302, "text/plain", ""); }  // Turn relay OFF and redirect

void handleStatus() {
  // Send current relay status as JSON
  StaticJsonDocument<64> doc;
  doc["state"] = relayState ? "on" : "off";

  char out[64];
  size_t n = serializeJson(doc, out);
  web.send(200, "application/json", String(out));
}

void handleSave() {
  // Handle saving WiFi configuration from form POST
  String s = web.arg("ssid");
  String p = web.arg("pass");
  String h = web.arg("host");

  if (!s.length()) {
    web.send(400, "text/plain", "Missing SSID");  // Error if SSID missing
    return;
  }

  saveConfigWiFi(s.c_str(), p.c_str(), h.length() ? h.c_str() : hostnameLocal);
  web.send(200, "text/html",
           "<html><body>Saved. Rebooting..."
           "<script>setTimeout(()=>location.reload(),1500)</script></body></html>");  // Confirmation page

  delay(500);
  ESP.restart();  // Restart to apply changes
}

// ============================================================================
// MQTT HANDLING
// ============================================================================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  // Callback for incoming MQTT messages
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload, len)) return;  // Parse failure: ignore

  if (!doc["switch"].is<int>() || !doc["command"].is<const char*>()) return;  // Invalid format: ignore

  if ((int)doc["switch"] != 1) return;  // Not targeted at this switch: ignore

  const char* cmd = doc["command"];
  if (!strcmp(cmd, "on"))  applyRelay(1);   // Handle ON command
  if (!strcmp(cmd, "off")) applyRelay(0);   // Handle OFF command
}

void mqttReconnect() {
  // Attempt to reconnect to MQTT broker if disconnected
  if (mqtt.connected()) return;

  String id = String("rs-") + String(ESP.getChipId(), HEX);  // Unique client ID
  String lwt = id + " lost";  // LWT payload

  mqtt.setServer(mqttServer, mqttPort);  // Set broker details
  mqtt.setCallback(mqttCallback);        // Set message callback

  if (!mqtt.connect(id.c_str(), nullptr, nullptr, LWT_TOPIC, 1, true, lwt.c_str()))
    return;  // Connection failed: exit

  mqtt.subscribe(SUB_TOPIC);  // Subscribe to control topic

  // Publish initial status
  StaticJsonDocument<96> doc;
  doc["switch"] = 1;
  doc["state"]  = relayState ? "on" : "off";
  doc["success"] = true;

  char buf[96];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(PUB_TOPIC, (uint8_t*)buf, n, true);  // Publish with retain
}

// ============================================================================
// WIFI / AP
// ============================================================================
String apName() {
  // Generate unique AP name based on chip ID
  return "RS-" + String(ESP.getChipId(), HEX);
}

void startAP() {
  // Start Access Point mode
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig({192,168,4,1}, {192,168,4,1}, {255,255,255,0});  // AP IP config
  WiFi.softAP(apName().c_str(), "12345678");  // SSID and password

#ifdef DEBUG
  Serial.println("AP active");  // Debug output
#endif
}

void tryStartSTA() {
  // Attempt to connect in Station mode if credentials available
  if (strlen(storedSSID) < 2) return;

  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostnameLocal);  // Set hostname
  WiFi.begin(storedSSID, storedPASS);  // Connect to WiFi
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  // Initialize relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // Start OFF

#ifdef DEBUG
  Serial.begin(115200);  // Start serial for debugging
  delay(50);
  Serial.println("Booting...");  // Debug boot message
#endif

  loadConfig();          // Load config from EEPROM
  applyRelay(relayState);  // Restore last relay state

  // Register web server endpoints
  web.on("/",        HTTP_GET,  handleRoot);
  web.on("/on",      HTTP_GET,  handleOn);
  web.on("/off",     HTTP_GET,  handleOff);
  web.on("/status",  HTTP_GET,  handleStatus);
  web.on("/save",    HTTP_POST, handleSave);
  web.begin();           // Start web server

  // Attempt STA connection with 10s timeout
  bool staConnected = false;
  if (strlen(storedSSID) > 1) {
    tryStartSTA();
    unsigned long start = millis();

    while (millis() - start < 10000UL) {
      if (WiFi.status() == WL_CONNECTED) {
        staConnected = true;
        break;
      }
      web.handleClient();  // Handle clients during connection attempt
      delay(200);
    }
  }

  if (!staConnected) {
    // STA failed: Start AP only
    startAP();
#ifdef DEBUG
    Serial.println("STA failed -> AP only");  // Debug output
#endif
  } else {
    // STA success: Log IP and start services
#ifdef DEBUG
    Serial.print("STA IP: "); Serial.println(WiFi.localIP());  // Debug IP
#endif

    MDNS.begin(hostnameLocal);  // Start mDNS for .local discovery

    if (apFallback && ESP.getFreeHeap() > 12000)
        startAP();  // Start AP in fallback if heap allows
  }

  mqtt.setClient(espClient);  // Set MQTT client
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  // Main loop: Handle web and MQTT
  web.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) mqttReconnect();  // Reconnect MQTT if needed
    else mqtt.loop();                        // Process MQTT messages
  }

  // Send periodic heartbeat if connected
  if (millis() - lastHeartbeat > HB_INTERVAL) {
    if (mqtt.connected()) {
      StaticJsonDocument<80> doc;
      doc["id"] = String(ESP.getChipId(), HEX);
      doc["uptime"] = millis() / 1000;  // Uptime in seconds

      char buf[80];
      size_t n = serializeJson(doc, buf);
      mqtt.publish(HB_TOPIC, (uint8_t*)buf, n, true);  // Publish heartbeat
    }
    lastHeartbeat = millis();
  }

  // Disable AP if heap is low to free resources
  if (apFallback && ESP.getFreeHeap() < 10000) {
    apFallback = false;
    EEPROM.write(APF_ADDR, 0);
    EEPROM.commit();
    WiFi.softAPdisconnect(true);  // Disconnect AP
  }

  delay(2);  // Short delay for stability
}