# BedTimeESP – How It Works

## Overview
BedTimeESP is an ESP8266-based relay controller designed to automate switches via MQTT and HTTP.  
It supports small-scale home automation projects, with optional expansion via I2C GPIO or ESP-12E boards for additional relays.

---

## Hardware

- **ESP-01 / ESP-12E**: Main controller
- **ULN2803APG**: Relay driver and level shifter
- **Relays**: 5V standard, controlled via GPIO through ULN2803
- **Power Supply**:
  - 5V DC for relays
  - 3.3V DC for ESP-01 / I2C modules

### Optional Expansion
- I2C GPIO expander IC for additional relay channels
- ESP-12E board for more GPIOs if needed

---

## Firmware

- Written in PlatformIO (C++ / Arduino framework)
- Handles:
  - Wi-Fi connectivity
  - MQTT topics for relay control (`esp01/<id>/relay/<n>`)
  - HTTP endpoints for manual toggle
  - Serial configuration for basic parameters

---

## Software Flow

1. ESP connects to Wi-Fi
2. Subscribes to MQTT topics
3. Listens for HTTP requests
4. Toggles relays via ULN2803
5. Optional logging and debug over serial

---

## Diagrams

- **Block Diagram**: ESP → ULN2803 → Relays
- **Schematic**: Shows connections of ESP, ULN2803, relays, power supply
- **PCB Layout**: If applicable

---

## Usage Notes

- Keep GPIO0 high at boot for ESP-01 to ensure stable startup
- Use FTDI programmer for flashing
- Expand GPIOs via I2C if more relays are needed

---

## References
- PlatformIO documentation
- ESP8266EX datasheet
- ULN2803 datasheet

