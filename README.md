# BedTimeESP (MQTT + HTTP)

Remote relay controller using ESP-01 / ESP-12E (ESP8266EX) via MQTT and HTTP.  
Designed for production-grade home automation and IoT hobbyists.


---

## Key Features
- Control relays remotely via MQTT (`esp01/<id>/relay/<n>`)
- HTTP endpoints for manual control
- Configurable via serial or MQTT
- Supports ULN2803 driver for 5V relays
- Optional I2C GPIO expanders or ESP-12E for additional pins

---

## Quick Start

1. Install [PlatformIO](https://platformio.org/) or Arduino CLI
2. Open `firmware/ESP8266-MQTT-HTTP` in VS Code
3. Configure `platformio.ini` with your board and credentials
4. Upload firmware via FTDI programmer (007 YP-05)
5. Use `examples/` to test MQTT and HTTP relay control

---

## Licensing
MIT License – see `LICENSE` file
