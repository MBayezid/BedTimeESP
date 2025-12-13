Architecture doc, hardware wiring guide, UI mockup, PlatformIO project layout and a production-ready MQTT topic scheme — all tuned for an **ESP-01 (512 KB)** and your chosen infrastructure (EMQX Serverless broker + Namecheap shared hosting as remote portal). I kept the firmware single-file (fits 512K), focused on robustness, and avoided heavy extras that would kill the device in the field.

Everything below is drop-in ready. Read, copy, flash, and don’t blame the hardware if you tie the relay directly to mains without proper protection.

---

# ARCHITECTURE.md (Full Version)

## Project: ESP-01 Smart Relay (512 KB Edition)

### Purpose

A minimal, resilient smart relay that:

* Always offers local control (AP fallback).
* Works over LAN (mDNS + web UI).
* Works over Internet via MQTT (EMQX Serverless).
* Recovers state across power cycles.
* Fits and runs reliably on ESP-01 with 512 KB flash.

---

## System Overview (Logical)

```
User (Android/iOS app or browser)
  ↕ (MQTT over internet)         ↕ (HTTP or mDNS local)
EMQX Serverless (cloud broker)   Router/LAN
           ↕                         ↕
         Internet                ESP-01 (STA + optional AP)
                                     ├─ PubSub MQTT client
                                     ├─ Tiny HTTP server (control + setup UI)
                                     ├─ EEPROM config (WiFi + state)
                                     └─ Relay driver (GPIO)
```

---

## Components

### Firmware (single-file `main.cpp`)

* WiFi AP + STA provisioning / fallback.
* Tiny HTTP server (UI + REST endpoints).
* PubSubClient MQTT (retain/LWT/heartbeat).
* EEPROM storage for WiFi creds + last relay state.
* Relay driver (debounce & write-throttle).
* mDNS advertising (hostname.local).

### Hardware

* ESP-01 module (512 KB)
* 3.3V regulator (low-drop, capable of 300–500 mA)
* Relay module (3.3V input or transistor driver + optocoupler)
* Flyback diode / snubber for inductive loads
* Proper mains wiring, enclosure, and grounding

### Services

* **Broker:** EMQX Serverless (recommended for free tier)
* **Remote Web Portal (optional):** Namecheap shared hosting (publish status/dashboard from the broker or via simple webhook)

---

## Boot & Run Flow

1. **Boot** → read EEPROM config (magic + version + stored SSID/pass + hostname + apFallback)
2. **STA attempt** (short timeout 8–12s)

   * success → start mDNS and local web server; optionally keep AP if `apFallback = true` and heap allows
   * fail → start AP mode and setup portal at `192.168.4.1`
3. **User config** via AP portal → save SSID, password, hostname → reboot
4. After STA connected → try MQTT connect (LWT set). If connected, publish retained status and heartbeat.
5. Local control is always available through:

   * AP portal at `192.168.4.1` (if AP running)
   * LAN UI at `http://<hostname>.local` (mDNS) or `http://<sta-ip>`
6. Relay state persists; updates stored on change with throttle.

---

## Data & Storage

* **EEPROM layout (bytes approximate)**:

  * byte 0 — magic/version
  * 1..31 — hostname (max 32)
  * 33..64 — SSID (max 32)
  * 97..160 — password (max 64)
  * flag byte — apFallback
  * final byte — lastRelayState

* **Storage policy**:

  * Write only on real changes.
  * Throttle writes (min 5 sec between writes).
  * Validate before accepting config (not blank SSID).

---

## Concurrency & Memory Strategy

* Keep single-file compile to reduce overhead (fits 512KB).
* Avoid LittleFS/SPIFFS (too heavy).
* Keep JSON small (`StaticJsonDocument` with modest capacity).
* Keep TLS off by default (use `setInsecure()` only for demo). TLS will likely push heap too low on 512 KB devices.
* Monitor `ESP.getFreeHeap()` and disable AP fallback when dangerously low.

---

## Security Notes (minimum)

* AP uses temporary password; consider randomizing per-device for production.
* Avoid exposing config endpoints to WAN on shared hosting—use broker as messaging gateway.
* For production cloud MQTT, use secure broker and TLS on a device with more RAM (ESP12/ESP32 recommended).

---

# Hardware Wiring Guide (Full)

> Safety first. If you’re not competent with mains wiring, hire an electrician.

## Recommended BOM (minimum)

* ESP-01 module (512 KB)
* Relay module (ideally 3.3V trigger, opto-isolated) or

  * Relay coil + transistor driver: 2N2222 / BC337 + base resistor + flyback diode (1N4007)
* 3.3V regulator (AMS1117-3.3 or better; prefer LDO with low noise)
* 5V supply (if using a 5V relay board)
* Decoupling capacitors: 10 µF electrolytic + 0.1 µF ceramic near ESP VCC/GND
* Logic-level MOSFET or transistor as driver (if relay board expects 5V)
* Screw terminals for mains
* Fuses and earth for safety
* Enclosure with IP rating for environment
* Ferrite beads for EMI suppression (optional)

## Wiring Diagram (textual)

Assume relay coil is driven at 5V, with transistor driver:

```
Mains Live ----> Relay COM
Relay NO -----> Load
Load -----> Mains Neutral

ESP-01:
  VCC (3.3V) -> 3.3V regulator output
  GND -> common ground with regulator and relay driver ground

Relay driver (transistor):
  ESP GPIO2 -----[1k resistor]----> Base of NPN (2N2222)
  Emitter -> GND
  Collector -> Relay coil negative (other side to +5V)
  Flyback diode across relay coil (cathode to +5V, anode to transistor collector)
```

If using a 3.3V relay board:

* Connect relay VCC to 3.3V
* Connect relay IN pin to ESP GPIO2 (optionally via 1k resistor)
* Always check relay module board docs — many boards contain opto-isolators.

### Important electrical rules

* Keep mains wiring and low-voltage electronics physically separated.
* Use fuses on the mains side of the relay.
* Use an isolated power supply if possible.
* Provide strain relief for incoming mains wires.
* Consider snubber networks or RC across contacts if switching inductive loads.

## PCB and Layout tips

* Place decoupling caps close to ESP VCC and regulator.
* Use ground plane where possible.
* Keep trace width for mains appropriately rated.
* Keep relay coil traces away from ESP antenna area if possible.

## Thermal & Environmental

* Provide ventilation or heat-sinking for regulator and relay if loaded.
* Use IP-rated enclosure for outdoor/dusty environments.
* Avoid plastic enclosures that trap heat around the ESP and regulator.

---

# UI Mock-up / HTML Template

Minimal, mobile-first UI that fits into the firmware (inline HTML). Paste into firmware strings or serve directly from C strings.

```html
<!-- minimal UI: fits directly into ESP string literals -->
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>Smart Relay</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 12px; }
    .card{ max-width:420px; margin:auto; padding:16px; border-radius:8px; box-shadow: 0 2px 6px rgba(0,0,0,.12); }
    button{ width:100%; padding:14px; border:none; border-radius:6px; font-size:16px; }
    .on{ background:#4CAF50; color:#fff; }
    .off{ background:#F44336; color:#fff; }
    .small{ font-size:13px; color:#666; margin-top:8px; display:block; text-align:center; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Remote Switch</h2>
    <p>Status: <strong id="status">OFF</strong></p>
    <button id="btnOn" class="on">Turn ON</button>
    <button id="btnOff" class="off" style="margin-top:8px">Turn OFF</button>
    <p class="small">If not connected to WiFi, connect your phone to device AP and open 192.168.4.1</p>
  </div>
  <script>
    async function doFetch(path, opts) {
      try {
        const res = await fetch(path, opts);
        return res.json ? await res.json() : null;
      } catch(e) { return null; }
    }
    async function updateStatus(){
      const st = await doFetch('/status');
      if(st && st.state) document.getElementById('status').textContent = st.state.toUpperCase();
    }
    document.getElementById('btnOn').onclick = async ()=>{ await doFetch('/on'); updateStatus(); };
    document.getElementById('btnOff').onclick = async ()=>{ await doFetch('/off'); updateStatus(); };
    updateStatus();
  </script>
</body>
</html>
```

Notes:

* Keep JS tiny, fetch to `/on`, `/off`, `/status`.
* For production, embed this string in firmware as a PROGMEM literal to save RAM.

---

# PlatformIO Project (professional but 512K-friendly)

## Folder structure (recommended)

```
/project-root
  /src
    main.cpp         <-- single-file optimized firmware (below)
  platformio.ini
  README.md
```

## platformio.ini (copy)

```ini
[env:esp01_512k]
platform = espressif8266
board = esp01_1m
framework = arduino

board_build.flash_mode = dout
upload_speed = 115200
monitor_speed = 115200

build_flags =
  -D MQTT_MAX_PACKET_SIZE=512
  -D PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH
  -D NDEBUG

lib_deps =
  knolleary/PubSubClient @ ^2.8
  bblanchon/ArduinoJson @ ^6.21.5
```

Notes:

* Use `esp01_1m` board (fits 512K). Keep everything minimal.
* Do not add large libraries or files into `data/` — avoid SPIFFS/LittleFS.

---

# main.cpp (single-file firmware — drop-in)

This is the pragmatic 512k-ready single-file sketch with AP/STA, tiny web UI, EEPROM config storage, MQTT publish using correct types, heartbeat, and safe memory use.

> Paste this into `src/main.cpp` in the PlatformIO project.

```cpp
// main.cpp - ESP-01 (512KB) single-file optimized firmware
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// ---------- CONFIG ----------
#define EEPROM_SIZE 256
#define MAGIC_ADDR 0
#define MAGIC_VAL 0xA5
#define HOST_ADDR 1      // 32 bytes
#define SSID_ADDR 33     // 32 bytes
#define PASS_ADDR 65     // 64 bytes
#define APF_ADDR 129     // 1 byte
#define STATE_ADDR 130   // 1 byte

const uint8_t RELAY_PIN = 2; // GPIO2 default

// MQTT / Topics
char mqttServer[64] = "broker.emqx.io";
uint16_t mqttPort = 1883;
const char* PUB_TOPIC = "home/switch/status";
const char* SUB_TOPIC = "home/switch/control";
const char* LWT_TOPIC = "home/switch/alert";
const char* HB_TOPIC  = "home/switch/heartbeat";

// Globals
char hostnameLocal[32] = "remoteswitch";
char storedSSID[32] = "";
char storedPASS[64] = "";
bool apFallback = true;
int relayState = 0;

ESP8266WebServer web(80);
WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastHeartbeat = 0;
const unsigned long HB_INTERVAL = 30000UL;
unsigned long lastStateWrite = 0;
const unsigned long STATE_WRITE_MIN = 5000UL; // throttle writes

// minimal UI page (small)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Switch</title>
<style>body{font-family:Arial;margin:12px}.card{max-width:420px;margin:auto}button{width:100%;padding:12px;border:none;border-radius:6px;font-size:16px}.on{background:#4CAF50;color:#fff}.off{background:#F44336;color:#fff}.sm{font-size:13px;color:#666;text-align:center;margin-top:8px}</style>
</head><body><div class='card'><h2>Remote Switch</h2><p>Status: <b id='st'>OFF</b></p><button id='on' class='on'>Turn ON</button><button id='off' class='off' style='margin-top:8px'>Turn OFF</button><p class='sm'>If not on WiFi, connect to device AP & open 192.168.4.1</p></div>
<script>
async function f(p){try{await fetch(p);}catch(e){}}
document.getElementById('on').onclick=()=>{f('/on');setTimeout(s,300)};
document.getElementById('off').onclick=()=>{f('/off');setTimeout(s,300)};
async function s(){try{let r=await fetch('/status');let j=await r.json();document.getElementById('st').textContent=j.state.toUpperCase();}catch(e){}}
s();
</script></body></html>)rawliteral";

// ---------- EEPROM helpers ----------
void eWriteStr(int addr, const char* s, int len) {
  for (int i=0;i<len;i++) EEPROM.write(addr+i, (i < strlen(s)) ? s[i] : 0);
}
void eReadStr(int addr, char* buf, int len) {
  for (int i=0;i<len;i++) { buf[i] = EEPROM.read(addr+i); if (buf[i]==0) { buf[i+1]=0; break; } }
  buf[len-1]=0;
}
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(MAGIC_ADDR) != MAGIC_VAL) {
    // defaults
    eWriteStr(HOST_ADDR, hostnameLocal, 32);
    eWriteStr(SSID_ADDR, "", 32);
    eWriteStr(PASS_ADDR, "", 64);
    EEPROM.write(APF_ADDR, apFallback ? 1 : 0);
    EEPROM.write(STATE_ADDR, relayState ? 1 : 0);
    EEPROM.write(MAGIC_ADDR, MAGIC_VAL);
    EEPROM.commit();
  }
  eReadStr(HOST_ADDR, hostnameLocal, 32);
  eReadStr(SSID_ADDR, storedSSID, 32);
  eReadStr(PASS_ADDR, storedPASS, 64);
  apFallback = EEPROM.read(APF_ADDR) == 1;
  relayState = EEPROM.read(STATE_ADDR) == 1 ? 1 : 0;
}
void saveConfigWiFi(const char* ssid, const char* pass, const char* host) {
  eWriteStr(SSID_ADDR, ssid, 32);
  eWriteStr(PASS_ADDR, pass, 64);
  eWriteStr(HOST_ADDR, host, 32);
  EEPROM.commit();
  // reload into RAM
  eReadStr(HOST_ADDR, hostnameLocal, 32);
  eReadStr(SSID_ADDR, storedSSID, 32);
  eReadStr(PASS_ADDR, storedPASS, 64);
}
void saveRelayState(int s) {
  unsigned long now = millis();
  if (now - lastStateWrite < STATE_WRITE_MIN) return;
  EEPROM.write(STATE_ADDR, s ? 1 : 0);
  EEPROM.commit();
  lastStateWrite = now;
}

// ---------- Relay ----------
void applyRelay(int s) {
  relayState = s ? 1 : 0;
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  saveRelayState(relayState);
}

// ---------- Web handlers ----------
void handleRoot() { web.send_P(200, "text/html", INDEX_HTML); }
void handleOn()  { applyRelay(1); web.sendHeader("Location", "/", true); web.send(302, "text/plain", ""); }
void handleOff() { applyRelay(0); web.sendHeader("Location", "/", true); web.send(302, "text/plain", ""); }
void handleStatus(){
  StaticJsonDocument<128> doc;
  doc["switch"] = 1;
  doc["state"] = relayState ? "on" : "off";
  char buf[128]; size_t n = serializeJson(doc, buf);
  web.send(200, "application/json", String(buf));
}
void handleSave() {
  // POST from AP portal: ssid, pass, host
  String s = web.arg("ssid");
  String p = web.arg("pass");
  String h = web.arg("host");
  if (s.length()>0) {
    saveConfigWiFi(s.c_str(), p.c_str(), h.length()?h.c_str():hostnameLocal);
    web.send(200, "text/html", "<html><body>Saved. Rebooting...<script>setTimeout(()=>location.reload(),1500)</script></body></html>");
    delay(500); ESP.restart();
  } else {
    web.send(400, "text/plain", "Missing SSID");
  }
}

// ---------- MQTT ----------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<200> doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) return;
  if (!doc.containsKey("switch") || !doc.containsKey("command")) return;
  int sw = doc["switch"];
  const char* cmd = doc["command"];
  if (sw != 1) return;
  if (strcasecmp(cmd, "on")==0) applyRelay(1);
  else if (strcasecmp(cmd, "off")==0) applyRelay(0);
}
void mqttReconnect() {
  if (mqtt.connected()) return;
  String clientId = String("rs-") + String(ESP.getChipId(), HEX);
  String lwt = clientId + " lost";
  mqtt.setServer(mqttServer, mqttPort);
  mqtt.setCallback(mqttCallback);
  if (mqtt.connect(clientId.c_str(), nullptr, nullptr, LWT_TOPIC, 1, true, lwt.c_str())) {
    mqtt.subscribe(SUB_TOPIC);
    // publish retained status
    StaticJsonDocument<120> doc; doc["switch"]=1; doc["state"]=relayState?"on":"off"; doc["success"]=true;
    char buf[120]; size_t n = serializeJson(doc, buf);
    mqtt.publish(PUB_TOPIC, (const uint8_t*)buf, n, true);
  }
}

// ---------- Setup/AP/STA ----------
String apName() { return String("RS-") + String(ESP.getChipId(), HEX); }
void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  String ssid = apName();
  WiFi.softAP(ssid.c_str(), "12345678");
}
void tryStartSTA() {
  if (strlen(storedSSID) < 2) return;
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostnameLocal);
  WiFi.begin(storedSSID, storedPASS);
}

// ---------- Setup ----------
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  Serial.begin(115200);
  delay(50);
  loadConfig();
  applyRelay(relayState); // restore state

  // web endpoints
  web.on("/", HTTP_GET, handleRoot);
  web.on("/on", HTTP_GET, handleOn);
  web.on("/off", HTTP_GET, handleOff);
  web.on("/status", HTTP_GET, handleStatus);
  web.on("/save", HTTP_POST, handleSave);
  web.begin();

  // Attempt STA
  bool staOk = false;
  if (strlen(storedSSID) > 1) {
    tryStartSTA();
    unsigned long start = millis();
    while (millis() - start < 10000UL) {
      if (WiFi.status() == WL_CONNECTED) { staOk = true; break; }
      web.handleClient();
      delay(200);
    }
  }

  if (!staOk) {
    startAP(); // provisioning + local control
    Serial.println("AP mode. Connect to: " + apName());
  } else {
    Serial.print("STA IP: "); Serial.println(WiFi.localIP());
    if (MDNS.begin(hostnameLocal)) Serial.println("mDNS ready");
    if (apFallback && ESP.getFreeHeap() > 12000) startAP();
  }

  // Setup MQTT (non-TLS by default for 512K)
  mqtt.setClient(espClient);
}

// ---------- Loop ----------
void loop() {
  web.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) mqttReconnect();
    else mqtt.loop();
  }

  if (millis() - lastHeartbeat > HB_INTERVAL) {
    if (mqtt.connected()) {
      StaticJsonDocument<120> doc; doc["id"]=String(ESP.getChipId(), HEX); doc["uptime"]=millis()/1000;
      char buf[120]; size_t n = serializeJson(doc, buf);
      mqtt.publish(HB_TOPIC, (const uint8_t*)buf, n, true);
    }
    lastHeartbeat = millis();
  }

  // heap safety: disable AP fallback if dangerously low
  if (ESP.getFreeHeap() < 10000 && apFallback) {
    apFallback = false;
    EEPROM.write(APF_ADDR, 0);
    EEPROM.commit();
    WiFi.softAPdisconnect(true);
  }

  delay(2);
}
```

---

# Production-ready MQTT Topic Naming Scheme

Design goals: clarity, uniqueness, per-device isolation, support multi-device setups, and retained state.

**Topic hierarchy template**

```
<org>/<site>/<device_type>/<device_id>/<channel>/<action>
```

**Practical mapping for your relay**

* `home/<house_id>/switch/<device_id>/control`

  * Client publishes commands (JSON): `{"switch":1,"command":"on"}`
  * Server/broker -> device subscribes

* `home/<house_id>/switch/<device_id>/status`

  * Device publishes status retained: `{"switch":1,"state":"on","success":true}`

* `home/<house_id>/switch/<device_id>/heartbeat`

  * Device publishes periodic heartbeat (retained optional)

* `home/<house_id>/switch/<device_id>/lwt`

  * Broker LWT topic for unexpected disconnects

**Examples**

* Subscribe: `home/house123/switch/rs-1A2B3C/control`
* Publish retained status: `home/house123/switch/rs-1A2B3C/status`
* Heartbeat: `home/house123/switch/rs-1A2B3C/heartbeat`

**Payload Examples**

* Command: `{"switch":1,"command":"on"}`
* Status (retained): `{"switch":1,"state":"on","success":true,"ts":1670000000}`
* Heartbeat: `{"id":"rs-1A2B3C","uptime":7200}`

**Best practices**

* Use device MAC or chip ID suffix as `device_id` to ensure uniqueness.
* Use retained messages for `status` so clients joining late get current state.
* Use LWT with `offline` payload populated on connect.
* Implement rate-limiting server-side or device-side for `control` to prevent floods.
* Use ACLs on EMQX to limit publish/subscribe scope for each client.

---

# Notes about EMQX Serverless and Namecheap Hosting

## EMQX Serverless

* Good free-tier broker for prototyping. Create a project & provide credentials to device.
* Use secure credentials; store on device in EEPROM.
* Configure ACLs so devices can only subscribe/publish to their own topics.

## Namecheap Shared Hosting (as remote portal)

* Namecheap shared hosting can host a static dashboard or a small PHP/Node script.
* Don’t expose device control endpoints directly from shared hosting — use it as a front-end that talks to the broker (MQTT over WebSocket or server-side bridge) or polls device state via broker-to-webhook.
* For remote control through Namecheap:

  * Implement a server-side bridge that connects to EMQX (broker) and offers authenticated REST endpoints or websockets to your mobile web UI.
  * Keep secrets server-side (do not hardcode broker credentials into client-side JS).

---

# Final Checklist before production

* [ ] Move to a bigger board (ESP12/ESP32) if you need TLS + many features.
* [ ] Add inrush/fuse protection and mains safety.
* [ ] Heat-test enclosure at worst-case load for 24–48 hours.
* [ ] Validate relay lifecycle under expected load (test cycles).
* [ ] Secure MQTT with TLS & proper certs in production (requires more RAM).
* [ ] Plan firmware update path (OTA on larger devices).
* [ ] Implement device-side rate-limits & watchdogs.


