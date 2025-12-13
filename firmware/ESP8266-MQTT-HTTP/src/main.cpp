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
#define EEPROM_SIZE 256
#define MAGIC_ADDR  0
#define MAGIC_VAL   0xA5
#define HOST_ADDR   1
#define SSID_ADDR   33
#define PASS_ADDR   65
#define APF_ADDR    129
#define STATE_ADDR  130

// GPIO (ESP-01 supports only 0 & 2)
static const uint8_t RELAY_PIN = 2;

// MQTT
static const char* PUB_TOPIC  = "home/switch/status";
static const char* SUB_TOPIC  = "home/switch/control";
static const char* LWT_TOPIC  = "home/switch/alert";
static const char* HB_TOPIC   = "home/switch/heartbeat";

char mqttServer[64] = "broker.emqx.io";
uint16_t mqttPort    = 1883;

// ============================================================================
// GLOBAL STATE
// ============================================================================
char hostnameLocal[32] = "remoteswitch";
char storedSSID[32]    = "";
char storedPASS[64]    = "";
bool apFallback         = true;
int relayState          = 0;

ESP8266WebServer web(80);
WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastHeartbeat = 0;
static const unsigned long HB_INTERVAL     = 30000UL;
unsigned long lastStateWrite = 0;
static const unsigned long STATE_WRITE_MIN = 5000UL; // min delay between EEPROM writes

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
)rawliteral";


// ============================================================================
// EEPROM HELPERS
// ============================================================================
void eWriteStr(int addr, const char* s, int maxLen) {
  int n = strlen(s);
  for (int i = 0; i < maxLen; i++)
    EEPROM.write(addr + i, (i < n) ? s[i] : 0);
}

void eReadStr(int addr, char* buf, int maxLen) {
  for (int i = 0; i < maxLen; i++) {
    buf[i] = EEPROM.read(addr + i);
    if (buf[i] == 0) break;
  }
  buf[maxLen - 1] = 0;
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);

  if (EEPROM.read(MAGIC_ADDR) != MAGIC_VAL) {
    // Initialize defaults
    eWriteStr(HOST_ADDR, hostnameLocal, 32);
    eWriteStr(SSID_ADDR, "", 32);
    eWriteStr(PASS_ADDR, "", 64);
    EEPROM.write(APF_ADDR, apFallback ? 1 : 0);
    EEPROM.write(STATE_ADDR, relayState ? 1 : 0);
    EEPROM.write(MAGIC_ADDR, MAGIC_VAL);
    EEPROM.commit();
  }

  // Load into RAM
  eReadStr(HOST_ADDR, hostnameLocal, 32);
  eReadStr(SSID_ADDR, storedSSID, 32);
  eReadStr(PASS_ADDR, storedPASS, 64);
  apFallback  = EEPROM.read(APF_ADDR) == 1;
  relayState  = EEPROM.read(STATE_ADDR) == 1;
}

void saveConfigWiFi(const char* ssid, const char* pass, const char* host) {
  eWriteStr(SSID_ADDR, ssid, 32);
  eWriteStr(PASS_ADDR, pass, 64);
  eWriteStr(HOST_ADDR, host, 32);
  EEPROM.commit();

  // Reload
  eReadStr(SSID_ADDR, storedSSID, 32);
  eReadStr(PASS_ADDR, storedPASS, 64);
  eReadStr(HOST_ADDR, hostnameLocal, 32);
}

void saveRelayState(int s) {
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
  relayState = (s ? 1 : 0);
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  saveRelayState(relayState);

#ifdef DEBUG
  Serial.printf("Relay -> %s\n", relayState ? "ON" : "OFF");
#endif
}

// ============================================================================
// WEB HANDLERS
// ============================================================================
void handleRoot()      { web.send_P(200, "text/html", INDEX_HTML); }
void handleOn()        { applyRelay(1); web.send(302, "text/plain", ""); }
void handleOff()       { applyRelay(0); web.send(302, "text/plain", ""); }

void handleStatus() {
  StaticJsonDocument<64> doc;
  doc["state"] = relayState ? "on" : "off";

  char out[64];
  size_t n = serializeJson(doc, out);
  web.send(200, "application/json", String(out));
}

void handleSave() {
  String s = web.arg("ssid");
  String p = web.arg("pass");
  String h = web.arg("host");

  if (!s.length()) {
    web.send(400, "text/plain", "Missing SSID");
    return;
  }

  saveConfigWiFi(s.c_str(), p.c_str(), h.length() ? h.c_str() : hostnameLocal);
  web.send(200, "text/html",
           "<html><body>Saved. Rebooting..."
           "<script>setTimeout(()=>location.reload(),1500)</script></body></html>");

  delay(500);
  ESP.restart();
}

// ============================================================================
// MQTT HANDLING
// ============================================================================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload, len)) return;

  if (!doc["switch"].is<int>() || !doc["command"].is<const char*>()) return;

  if ((int)doc["switch"] != 1) return;

  const char* cmd = doc["command"];
  if (!strcmp(cmd, "on"))  applyRelay(1);
  if (!strcmp(cmd, "off")) applyRelay(0);
}

void mqttReconnect() {
  if (mqtt.connected()) return;

  String id = String("rs-") + String(ESP.getChipId(), HEX);
  String lwt = id + " lost";

  mqtt.setServer(mqttServer, mqttPort);
  mqtt.setCallback(mqttCallback);

  if (!mqtt.connect(id.c_str(), nullptr, nullptr, LWT_TOPIC, 1, true, lwt.c_str()))
    return;

  mqtt.subscribe(SUB_TOPIC);

  StaticJsonDocument<96> doc;
  doc["switch"] = 1;
  doc["state"]  = relayState ? "on" : "off";
  doc["success"] = true;

  char buf[96];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(PUB_TOPIC, (uint8_t*)buf, n, true);
}

// ============================================================================
// WIFI / AP
// ============================================================================
String apName() {
  return "RS-" + String(ESP.getChipId(), HEX);
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig({192,168,4,1}, {192,168,4,1}, {255,255,255,0});
  WiFi.softAP(apName().c_str(), "12345678");

#ifdef DEBUG
  Serial.println("AP active");
#endif
}

void tryStartSTA() {
  if (strlen(storedSSID) < 2) return;

  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostnameLocal);
  WiFi.begin(storedSSID, storedPASS);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

#ifdef DEBUG
  Serial.begin(115200);
  delay(50);
  Serial.println("Booting...");
#endif

  loadConfig();
  applyRelay(relayState);

  // Web endpoints
  web.on("/",        HTTP_GET,  handleRoot);
  web.on("/on",      HTTP_GET,  handleOn);
  web.on("/off",     HTTP_GET,  handleOff);
  web.on("/status",  HTTP_GET,  handleStatus);
  web.on("/save",    HTTP_POST, handleSave);
  web.begin();

  // Attempt STA (10s timeout)
  bool staConnected = false;
  if (strlen(storedSSID) > 1) {
    tryStartSTA();
    unsigned long start = millis();

    while (millis() - start < 10000UL) {
      if (WiFi.status() == WL_CONNECTED) {
        staConnected = true;
        break;
      }
      web.handleClient();
      delay(200);
    }
  }

  if (!staConnected) {
    startAP();
#ifdef DEBUG
    Serial.println("STA failed -> AP only");
#endif
  } else {
#ifdef DEBUG
    Serial.print("STA IP: "); Serial.println(WiFi.localIP());
#endif

    MDNS.begin(hostnameLocal);

    if (apFallback && ESP.getFreeHeap() > 12000)
        startAP();
  }

  mqtt.setClient(espClient);
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  web.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) mqttReconnect();
    else mqtt.loop();
  }

  // Heartbeat
  if (millis() - lastHeartbeat > HB_INTERVAL) {
    if (mqtt.connected()) {
      StaticJsonDocument<80> doc;
      doc["id"] = String(ESP.getChipId(), HEX);
      doc["uptime"] = millis() / 1000;

      char buf[80];
      size_t n = serializeJson(doc, buf);
      mqtt.publish(HB_TOPIC, (uint8_t*)buf, n, true);
    }
    lastHeartbeat = millis();
  }

  // Disable AP fallback if heap is too low
  if (apFallback && ESP.getFreeHeap() < 10000) {
    apFallback = false;
    EEPROM.write(APF_ADDR, 0);
    EEPROM.commit();
    WiFi.softAPdisconnect(true);
  }

  delay(2);
}
