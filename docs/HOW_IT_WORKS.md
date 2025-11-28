
---

# HOW_IT_WORKS.md

**BedTimeESP System Architecture**
**Version:** 1.0.0
**Hardware Target:** ESP8266-01 / ESP-01S
**Protocols:** HTTP / MQTT / mDNS

---

## 1. System Overview

The firmware turns an ESP-01/ESP-12E into a network-enabled relay controller. It normally runs in WiFi client mode, falling back to AP mode if no known network is found.

It provides:

* A lightweight local web interface for control and configuration
* Optional MQTT integration for automation
* Predictable behavior aimed at stability, low footprint, and easy integration with common relay modules

---

## 2. Hardware Abstraction Layer

ESP-01 GPIO is limited, so the pin layout is kept minimal and practical while preserving serial output.

| Component     | ESP Pin     | Function       | Notes                          |
| ------------- | ----------- | -------------- | ------------------------------ |
| Relay Output  | GPIO 3 (RX) | Relay Control  | Active LOW/HIGH (configurable) |
| Status LED    | GPIO 2      | Built-in LED   | Active LOW                     |
| Debug Output  | GPIO 1 (TX) | Serial Logging | TX-only logging, no RX input   |
| Boot Mode Pin | GPIO 0      | Flash/Run      | Must remain HIGH at boot       |

Using GPIO3 for the relay removes serial RX. That is intentional.

---

## 3. Operational Control Flow

### 3.1 Boot Sequence

1. Initialize UART (TX-only).
2. Configure GPIO pins.
3. Load stored relay state and network config from EEPROM.
4. Begin network handshake.

### 3.2 Network Handshake

**If stored WiFi credentials work:**

* Device enters STA mode
* mDNS service starts
* MQTT attempts connection; if subscribed successfully, publishes status
* LAN-only mode still works even if internet is unavailable

**If WiFi connection fails:**

* Device enters AP mode

  * SSID: `ESP-Relay-Module`
  * IP: `192.168.4.1`
* Hosts configuration UI

---

## 4. Main Loop & Request Handling

A synchronous HTTP server on port 80 provides all control functions.

It handles:

* WiFi reconnect attempts
* HTTP routing
* UI generation

### Endpoints

| Path        | Method | Action                                 |
| ----------- | ------ | -------------------------------------- |
| `/`         | GET    | Dashboard with state + control buttons |
| `/ON`       | GET    | Turn relay ON and return to dashboard  |
| `/OFF`      | GET    | Turn relay OFF and return to dashboard |
| `/settings` | GET    | Configuration form                     |
| `/save`     | GET    | Write config to EEPROM and reboot      |

---

## 5. EEPROM Layout

256 bytes are reserved for all stored parameters.

| Data        | Address | Length | Type                     |
| ----------- | ------- | ------ | ------------------------ |
| SSID        | 0       | 40     | char[]                   |
| Password    | 40      | 30     | char[]                   |
| IP Address  | 70      | 15     | char[]                   |
| Subnet      | 85      | 15     | char[]                   |
| Gateway     | 100     | 15     | char[]                   |
| DNS         | 115     | 15     | char[]                   |
| IP Type     | 130     | 1      | '1' = Static, '2' = DHCP |
| Relay State | 131     | 1      | '1' = ON, '2' = OFF      |
| mDNS Name   | 132     | 20     | char[]                   |

---

## 6. Network Architecture

### 6.1 DHCP Mode (Default)

The router assigns an IP.

Access via:

* Assigned IP
* `http://<name>.local` (default: `myrelaycard.local`)

### 6.2 Static IP Mode

User specifies:

* IP
* Subnet
* Gateway
* DNS

UI provides common placeholder values for clarity.

---

## 7. Security Considerations

* AP mode is open (WPA2 planned for v1.1).
* Credentials stored in plain EEPROM.
* No web UI authentication.
* Recommended: isolate IoT traffic on its own VLAN.

---

## 8. Troubleshooting

* **Relay flickers at boot:** GPIO3 floats; some relay boards need a pull-up.
* **AP mode unreachable:** ESP-01 needs a stable 3.3V supply with at least 500mA peak.
* **WiFi issues:** ESP8266 supports only 2.4 GHz.

---