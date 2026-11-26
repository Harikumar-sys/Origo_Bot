# Origo_Bot
Codes of Origo bot
# Dasai Robot — Gesture & Expression Controller

An ESP8266 differential-drive desk/rover robot with an animated SSD1306 OLED face, browser D-pad control, and real-time Python hand-gesture + facial-expression control over Wi-Fi AP mode.

## Features
- **Wi-Fi Access Point (`Dasai_Robot`):** Hosts a static IP at `192.168.4.1` with no external router required.
- **Browser D-Pad (Port `80`):** Control forward, backward, left, right, and stop from a phone or laptop browser (takes 2-second priority over gesture commands when used).
- **Hand Gesture Motion (Port `81`):** Uses MediaPipe Hands to map raised fingers (`0`–`4`) to motor actions over TCP.
- **Facial Expression OLED (Port `81`):** Uses MediaPipe Face Mesh lip landmarks to switch the OLED expression between `HAPPY`, `SAD`, and `NORMAL` with a hold timer (`2.0s` in `expression.py`).

## Pin Connections (ESP8266 NodeMCU)
- **OLED (SSD1306 128×64 I2C, `0x3C`):** `VCC` → `3V3`, `GND` → `GND`, `SDA` → `D2` (GPIO 4), `SCL` → `D1` (GPIO 5)
- **Mood Button:** `D4` (GPIO 2, `INPUT_PULLUP`) to `GND` (avoid holding low during boot)
- **L293D Motor Driver:**
  - Motor A: `EN1` → `D8` (GPIO 15), `IN1` → `D6` (GPIO 12), `IN2` → `D5` (GPIO 14)
  - Motor B: `EN2` → `D7` (GPIO 13), `IN3` → `D3` (GPIO 0), `IN4` → `D0` (GPIO 16)
  - Power: `VCC1` → `3V3` or `5V`, `VCC2` → Battery `+`, `GND` → Common `GND`

## Repository Structure
```text
.
├── dasai_robot.ino   # ESP8266 AP mode, web UI (port 80), TCP server (port 81), OLED animations
└── expression.py     # Webcam hand-gesture & facial-expression TCP client
