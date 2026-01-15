# BedTimeESP - Production-Ready IoT Smart Relay

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-ESP8266-blue.svg)](https://www.espressif.com/en/products/socs/esp8266)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D.svg)](https://www.arduino.cc/)

Transform a low-cost ESP-01 (512KB) or ESP-12E module into a production-ready IoT smart relay with local and remote control capabilities. Features dual-mode WiFi (AP + STA), MQTT integration, web interface, and persistent state management.

![Project Banner](docs/images/banner.png)

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Key Features](#-key-features)
- [Hardware Requirements](#-hardware-requirements)
- [Architecture](#-architecture)
- [Quick Start](#-quick-start)
- [Installation](#-installation)
- [Configuration](#-configuration)
- [Usage](#-usage)
- [MQTT Integration](#-mqtt-integration)
- [API Reference](#-api-reference)
- [Troubleshooting](#-troubleshooting)
- [Safety & Legal](#-safety--legal)
- [Contributing](#-contributing)
- [License](#-license)

---

## 🎯 Overview

BedTimeESP is a minimal, resilient smart relay system designed for the ESP8266 platform. It provides both local and cloud-based control while maintaining reliability and fitting within the constraints of the ESP-01's 512KB flash memory.

### Why BedTimeESP?

- **Always Accessible**: AP fallback ensures local control even when WiFi is unavailable
- **Dual Control**: Local web UI + remote MQTT control
- **Persistent State**: Survives power cycles with EEPROM storage
- **Production Ready**: Robust error handling, LWT (Last Will and Testament), heartbeat monitoring
- **Minimal Footprint**: Optimized for ESP-01 (512KB) but scalable to larger boards
- **Open Protocol**: Standard MQTT topics for easy integration

---

## ✨ Key Features

### Core Functionality
- ✅ **Dual WiFi Mode**: Station (STA) + Access Point (AP) with automatic fallback
- ✅ **Local Web Interface**: Responsive mobile-first UI accessible via mDNS
- ✅ **MQTT Integration**: Full pub/sub with retained messages and LWT support
- ✅ **State Persistence**: EEPROM storage for configuration and relay state
- ✅ **Zero-Config Discovery**: mDNS advertising (`hostname.local`)
- ✅ **Heartbeat Monitoring**: Periodic status updates for health checking

### Technical Highlights
- 🔒 **Safe Writes**: Throttled EEPROM writes (5-second minimum interval)
- 🔄 **Automatic Recovery**: WiFi reconnection and AP fallback
- 📊 **Memory Management**: Heap monitoring with automatic AP shutdown
- 🎛️ **RESTful API**: Simple HTTP endpoints for integration
- 📡 **OTA Ready**: Supports over-the-air updates (ESP-12E+)

---

## 🛠️ Hardware Requirements

### Minimum Components

| Component | Specification | Notes |
|-----------|--------------|-------|
| **Microcontroller** | ESP-01 (512KB) or ESP-12E/F | ESP-01 for minimal build, ESP-12E for extended features |
| **Relay Module** | 3.3V or 5V, optoisolated | Must support 3.3V trigger or use transistor driver |
| **Power Supply** | 3.3V @ 500mA | AMS1117-3.3 or better LDO regulator |
| **Transistor** | 2N2222 or BC337 (if 5V relay) | With 1kΩ base resistor |
| **Diode** | 1N4007 (flyback) | Protection for relay coil |
| **Capacitors** | 10µF electrolytic + 0.1µF ceramic | Decoupling near ESP VCC/GND |

### Optional Components
- 5V relay board with built-in optocoupler
- Screw terminals for mains connections
- Fuses and circuit breakers (required for mains)
- IP-rated enclosure
- Ferrite beads for EMI suppression

### Wiring Diagram

```
┌─────────────────────────────────────────────────┐
│                  Power Section                  │
│  AC/DC 5V ─► AMS1117-3.3 ─► ESP-01 VCC          │
│                    │                            │
│                    └─► 10µF + 0.1µF caps        │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│              Relay Control Section              │
│  ESP GPIO2 ──[1kΩ]─► 2N2222 Base               │
│                      Collector ─► Relay Coil─   │
│                      Emitter ─► GND             │
│                                                 │
│  [1N4007 Diode across relay coil]               │
│  Cathode (+5V) ←─ Anode (Collector)             │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│               Mains Section (⚠️)                 │
│  Live ──► [Fuse] ──► Relay COM                  │
│  Relay NO ──► Load ──► Neutral                  │
│  Earth ──────────────► Load Earth               │
└─────────────────────────────────────────────────┘
```

> ⚠️ **DANGER**: Mains voltage can be lethal. See [Safety & Legal](#safety--legal) section.

---

## 🏗️ Architecture

```
┌────────────────────────────────────────────────────┐
│                    User Clients                     │
│  ┌─────────────┐        ┌─────────────┐           │
│  │   Browser   │        │  MQTT App   │           │
│  │  (Local)    │        │  (Remote)   │           │
│  └──────┬──────┘        └──────┬──────┘           │
└─────────┼────────────────────────┼─────────────────┘
          │                        │
          │ HTTP/mDNS              │ MQTT
          ▼                        ▼
┌─────────────────┐      ┌──────────────────┐
│  LAN Router     │◄────►│  EMQX Broker     │
│  192.168.x.x    │      │  (Cloud/Self)    │
└────────┬────────┘      └──────────────────┘
         │                        │
         │ WiFi (STA)             │ TLS (optional)
         ▼                        ▼
┌─────────────────────────────────────────────┐
│           ESP8266 Smart Relay               │
│  ┌─────────────────────────────────────┐   │
│  │  WiFi Stack (STA + AP)              │   │
│  ├─────────────────────────────────────┤   │
│  │  • HTTP Server (Port 80)            │   │
│  │  • MQTT Client (PubSubClient)       │   │
│  │  • mDNS Responder                   │   │
│  │  • EEPROM Manager                   │   │
│  ├─────────────────────────────────────┤   │
│  │  Application Logic                  │   │
│  │  • State Machine                    │   │
│  │  • Relay Driver (GPIO2)             │   │
│  │  • Heartbeat Timer                  │   │
│  └─────────────────────────────────────┘   │
│              ┌──────────┐                   │
│              │  EEPROM  │ (Persistent)      │
│              └──────────┘                   │
└─────────────────────────────────────────────┘
         │
         ▼
    [RELAY MODULE] ──► LOAD
```

### Boot Sequence

1. **Initialize** → Load config from EEPROM (WiFi credentials, hostname, last state)
2. **Restore State** → Apply last relay state from persistent storage
3. **WiFi Connect** → Attempt STA connection (8-12 second timeout)
   - ✅ Success → Start web server + mDNS + MQTT client
   - ❌ Failure → Start AP mode at `192.168.4.1`
4. **Service Loop** → Handle web requests, MQTT messages, heartbeats

---

## 🚀 Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/MBayezid/BedTimeESP.git
cd BedTimeESP
```

### 2. Install PlatformIO

```bash
# Using pip
pip install platformio

# Or using VSCode extension
# Install "PlatformIO IDE" from VSCode marketplace
```

### 3. Configure Settings

Edit `src/main.cpp` and update these values:

```cpp
// MQTT Broker Configuration
char mqttServer[64] = "broker.emqx.io";  // Your broker
uint16_t mqttPort = 1883;

// Device Topics (customize per device)
const char* PUB_TOPIC = "home/switch/status";
const char* SUB_TOPIC = "home/switch/control";
```

### 4. Build and Upload

```bash
# Build firmware
pio run -e esp01_512k

# Upload via serial (connect ESP-01 to USB-TTL adapter)
pio run -e esp01_512k --target upload

# Monitor serial output
pio device monitor
```

### 5. Initial Setup

1. Device boots in AP mode: `RS-<CHIP_ID>`
2. Connect your phone to this network (password: `12345678`)
3. Open browser to `http://192.168.4.1`
4. Enter your WiFi credentials
5. Device reboots and connects to your network
6. Access via `http://<hostname>.local` or IP address

---

## 📦 Installation

### Hardware Setup

Detailed wiring guides available in [`docs/hardware-setup.md`](docs/hardware-setup.md)

**Basic ESP-01 Connection:**

| ESP-01 Pin | Connect To |
|------------|------------|
| VCC | 3.3V (regulated) |
| GND | Common ground |
| GPIO0 | HIGH for normal boot, LOW for programming |
| GPIO2 | Relay control via transistor |
| CH_PD | 3.3V (HIGH) |
| TX/RX | USB-TTL adapter for programming |

**Programming Mode:**
- GPIO0 → GND (during power-up)
- GPIO2 → HIGH (via 10kΩ pullup)
- CH_PD → HIGH (via 10kΩ pullup)

### Software Installation

#### Option A: PlatformIO (Recommended)

1. Open project in VSCode with PlatformIO extension
2. Select environment: `esp01_512k` or `esp12e`
3. Click "Build" → "Upload"

#### Option B: Arduino IDE

1. Install ESP8266 board support:
   - File → Preferences → Additional Board URLs:
   - `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
2. Install libraries:
   - PubSubClient (by Nick O'Leary)
   - ArduinoJson (v6.21.5+)
3. Select board: Tools → Board → Generic ESP8266 Module
4. Configure:
   - Flash Size: 512KB (for ESP-01)
   - Upload Speed: 115200
5. Upload sketch

---

## ⚙️ Configuration

### EEPROM Memory Map

| Address | Size | Content | Default |
|---------|------|---------|---------|
| 0 | 1 byte | Magic value (0xA5) | 0xA5 |
| 1-32 | 32 bytes | Hostname | "remoteswitch" |
| 33-64 | 32 bytes | WiFi SSID | "" (empty) |
| 65-128 | 64 bytes | WiFi Password | "" (empty) |
| 129 | 1 byte | AP Fallback flag | 1 (enabled) |
| 130 | 1 byte | Last Relay State | 0 (OFF) |

### WiFi Configuration

**Via AP Portal:**
1. Connect to device AP: `RS-<CHIP_ID>`
2. Navigate to `http://192.168.4.1`
3. Submit configuration form:
   - SSID: Your network name
   - Password: Your network password
   - Hostname: Custom hostname (optional)

**Via Code (before compilation):**
```cpp
// In setup() function
strcpy(storedSSID, "YourNetworkName");
strcpy(storedPASS, "YourPassword");
strcpy(hostnameLocal, "bedroom-switch");
```

### MQTT Topics

**Default Topic Structure:**
```
home/switch/<device_id>/control   ← Subscribe (commands)
home/switch/<device_id>/status    ← Publish (state)
home/switch/<device_id>/heartbeat ← Publish (health)
home/switch/<device_id>/alert     ← LWT topic
```

**Customize Topics:**
```cpp
const char* PUB_TOPIC = "home/bedroom/relay/status";
const char* SUB_TOPIC = "home/bedroom/relay/control";
const char* HB_TOPIC = "home/bedroom/relay/heartbeat";
const char* LWT_TOPIC = "home/bedroom/relay/lwt";
```

---

## 📖 Usage

### Local Control

**Web Interface:**
- Access via mDNS: `http://remoteswitch.local`
- Or direct IP: `http://192.168.1.xxx`
- Mobile-optimized responsive design
- Buttons: Turn ON / Turn OFF
- Real-time status display

**HTTP API:**
```bash
# Turn relay ON
curl http://remoteswitch.local/on

# Turn relay OFF
curl http://remoteswitch.local/off

# Get status
curl http://remoteswitch.local/status
# Response: {"switch":1,"state":"on"}
```

### Remote Control (MQTT)

**Control Command:**
```json
// Publish to: home/switch/<device_id>/control
{
  "switch": 1,
  "command": "on"  // or "off"
}
```

**Status Updates (retained):**
```json
// Published to: home/switch/<device_id>/status
{
  "switch": 1,
  "state": "on",
  "success": true
}
```

**Heartbeat:**
```json
// Published to: home/switch/<device_id>/heartbeat
{
  "id": "rs-1a2b3c",
  "uptime": 7200  // seconds
}
```

### Integration Examples

**Node-RED Flow:**
```json
[
  {
    "type": "mqtt in",
    "topic": "home/switch/+/status",
    "broker": "your-broker"
  },
  {
    "type": "mqtt out",
    "topic": "home/switch/rs-xxx/control",
    "payload": "{\"switch\":1,\"command\":\"on\"}"
  }
]
```

**Home Assistant:**
```yaml
switch:
  - platform: mqtt
    name: "Bedroom Switch"
    state_topic: "home/switch/rs-xxx/status"
    command_topic: "home/switch/rs-xxx/control"
    payload_on: '{"switch":1,"command":"on"}'
    payload_off: '{"switch":1,"command":"off"}'
    state_on: '"state":"on"'
    state_off: '"state":"off"'
    availability_topic: "home/switch/rs-xxx/heartbeat"
    availability_template: "{{ 'online' if value_json.uptime else 'offline' }}"
```

---

## 🔌 MQTT Integration

### Broker Setup

**Recommended: EMQX Serverless**
1. Sign up at [emqx.io](https://www.emqx.io/)
2. Create a serverless deployment (free tier available)
3. Note your broker address and credentials
4. Configure ACLs for topic restrictions

**Alternative: Mosquitto (Self-Hosted)**
```bash
# Install
sudo apt install mosquitto mosquitto-clients

# Configure
sudo nano /etc/mosquitto/mosquitto.conf

# Add:
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd

# Create user
sudo mosquitto_passwd -c /etc/mosquitto/passwd username

# Start
sudo systemctl restart mosquitto
```

### Security Best Practices

1. **Never use anonymous access** in production
2. **Implement ACLs** to restrict topic access per device
3. **Use TLS** for internet-exposed brokers (requires ESP-12E+ for sufficient RAM)
4. **Rotate credentials** periodically
5. **Monitor connection logs** for anomalies

### TLS Configuration (ESP-12E+)

```cpp
// Add to main.cpp (requires more RAM than ESP-01 has)
WiFiClientSecure espClient;
espClient.setInsecure(); // For testing only!

// Production: use certificate validation
const char* root_ca = R"EOF(
-----BEGIN CERTIFICATE-----
[Your Root CA Certificate]
-----END CERTIFICATE-----
)EOF";

espClient.setCACert(root_ca);
```

---

## 📡 API Reference

### HTTP Endpoints

| Endpoint | Method | Description | Response |
|----------|--------|-------------|----------|
| `/` | GET | Main control interface | HTML page |
| `/on` | GET | Turn relay ON | 302 Redirect |
| `/off` | GET | Turn relay OFF | 302 Redirect |
| `/status` | GET | Get current state | JSON |
| `/save` | POST | Save WiFi config | HTML |

### Status Response

```json
{
  "switch": 1,         // Device ID
  "state": "on"        // Current state: "on" or "off"
}
```

### Configuration Endpoint

**POST `/save`**

Parameters (form-encoded):
- `ssid` (required): WiFi network name
- `pass` (required): WiFi password
- `host` (optional): Custom hostname

Response: HTML confirmation page with auto-redirect

---

## 🐛 Troubleshooting

### Device Won't Connect to WiFi

**Symptoms**: AP mode persists, can't connect to home network

**Solutions**:
1. Verify SSID and password (case-sensitive)
2. Check WiFi signal strength at device location
3. Ensure router supports 2.4GHz (ESP8266 doesn't support 5GHz)
4. Check for special characters in password
5. Monitor serial output for connection errors

```bash
# Serial monitor should show:
# "STA IP: 192.168.x.x" for success
# "AP mode. Connect to: RS-xxx" for failure
```

### MQTT Not Connecting

**Symptoms**: Device online but MQTT commands don't work

**Solutions**:
1. Verify broker address and port in code
2. Check firewall rules on broker side
3. Test broker with mosquitto client:
   ```bash
   mosquitto_pub -h broker.emqx.io -t test -m "hello"
   ```
4. Enable debug logging:
   ```cpp
   mqtt.setClient(espClient);
   mqtt.setCallback(mqttCallback);
   // Add debug:
   Serial.println("MQTT connecting...");
   ```

### Relay Not Switching

**Symptoms**: Device responds but relay doesn't click/switch

**Solutions**:
1. Check wiring diagram carefully
2. Verify transistor orientation (E-B-C)
3. Test relay coil voltage with multimeter
4. Check flyback diode polarity
5. Measure GPIO2 output (should be 3.3V when ON)
6. Try direct connection (GPIO → Relay IN) to isolate driver issue

### Memory Issues

**Symptoms**: Crashes, reboots, AP not starting

**Solutions**:
1. Monitor free heap: `Serial.println(ESP.getFreeHeap());`
2. Disable AP fallback if heap < 12KB
3. Reduce MQTT packet size
4. Remove unnecessary features for ESP-01

### Serial Upload Fails

**Symptoms**: `Failed to connect to ESP8266`

**Solutions**:
1. Hold GPIO0 LOW during power-up
2. Check USB-TTL wiring (RX→TX, TX→RX)
3. Verify baud rate (115200)
4. Try different upload speeds (57600, 9600)
5. Add 10µF capacitor across RESET and GND

---

## ⚠️ Safety & Legal

### Electrical Safety

**CRITICAL WARNINGS:**

⚠️ **Mains voltage (110V/220V AC) can cause serious injury or death.**

- Only qualified electricians should work with mains wiring
- Always disconnect power before working
- Use appropriate fuses and circuit breakers
- Maintain proper clearance between low-voltage and high-voltage sections
- Use proper insulation and enclosures
- Test thoroughly before permanent installation
- Consider using a 5V relay board with optical isolation

### Compliance

**This project may require:**
- FCC certification (USA)
- CE marking (Europe)
- UL/CSA approval for commercial use
- Local electrical permits

**The authors assume NO liability for:**
- Improper installation
- Fire, shock, or property damage
- Regulatory violations
- Failure to follow electrical codes

### Best Practices

1. **Use isolation**: Optocoupler between ESP and relay coil
2. **Add protection**: Fuses, circuit breakers, RCDs
3. **Test first**: Dry-run with low-voltage loads
4. **Document**: Keep wiring diagrams and test logs
5. **Maintain**: Regular inspection of connections and enclosure

---

## 🤝 Contributing

We welcome contributions! Please follow these guidelines:

### How to Contribute

1. **Fork** the repository
2. **Create** a feature branch: `git checkout -b feature/amazing-feature`
3. **Commit** your changes: `git commit -m 'Add amazing feature'`
4. **Push** to branch: `git push origin feature/amazing-feature`
5. **Open** a Pull Request

### Code Standards

- Follow existing code style (2-space indentation)
- Comment complex logic
- Test on real hardware before submitting
- Update documentation for new features
- Keep commits atomic and well-described

### Reporting Issues

Use GitHub Issues with these templates:

**Bug Report:**
- Description
- Steps to reproduce
- Expected behavior
- Actual behavior
- Hardware: ESP-01 / ESP-12E or any from ESP MCU Family.
- Serial output (if available)

**Feature Request:**
- Use case
- Proposed solution
- Alternative approaches
- Impact on existing features

---

## 📄 License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

```
MIT License

Copyright (c) 2025 BedTimeESP Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

[Full license text in LICENSE file]
```

---

## 🙏 Acknowledgments

- **ESP8266 Community** - For extensive documentation and support
- **PlatformIO** - For excellent embedded development platform
- **MQTT.org** - For IoT messaging protocol
- **EMQX Team** - For reliable MQTT broker
- **Arduino Core Team** - For ESP8266 Arduino framework

### Libraries Used

- [PubSubClient](https://github.com/knolleary/pubsubclient) by Nick O'Leary
- [ArduinoJson](https://arduinojson.org/) by Benoît Blanchon
- [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)

---

## 📞 Support

- **Documentation**: [Wiki](https://github.com/MBayezid/BedTimeESP/wiki)
- **Issues**: [GitHub Issues](https://github.com/MBayezid/BedTimeESP/issues)
- **Discussions**: [GitHub Discussions](https://github.com/MBayezid/BedTimeESP/discussions)
- **Email**: [musanna324@gmail.com]

---

## 🗺️ Roadmap

- [ ] Web-based OTA firmware updates
- [ ] Multiple relay support (ESP-12E)
- [ ] Scheduling/timers
- [ ] Energy monitoring integration
- [ ] MQTT discovery for Home Assistant
- [ ] Mobile app (Android/iOS)
- [ ] Alexa/Google Home integration
- [ ] Temperature sensor support
- [ ] Advanced MQTT features (QoS 2, will delay)

---

## 📊 Project Stats

![GitHub stars](https://img.shields.io/github/stars/MBayezid/BedTimeESP?style=social)
![GitHub forks](https://img.shields.io/github/forks/MBayezid/BedTimeESP?style=social)
![GitHub issues](https://img.shields.io/github/issues/MBayezid/BedTimeESP)
![GitHub pull requests](https://img.shields.io/github/issues-pr/MBayezid/BedTimeESP)

---

**Made with ❤️ for the IoT community**

*If this project helped you, consider giving it a ⭐ on GitHub!*