# Origo Bot — Web UI, Gesture & Emotion Controller

An ESP8266 differential-drive robot running in Wi-Fi AP mode (`Dasai_Robot`) with an animated SSD1306 OLED face, a built-in browser D-pad web server, and a Python vision client for hand-gesture drive commands and facial-expression OLED moods[cite: 2, 3].

## Control Modes & Network
- **Wi-Fi AP:** SSID `Dasai_Robot` | Password `12345678` | Static IP `http://192.168.4.1`
- **Web Server UI (Port `80`):** Serves a mobile/desktop D-pad page at `/` with `/forward`, `/backward`, `/left`, `/right`, and `/stop` endpoints. Any web command sets `lastWebCommandTime` and locks out TCP gesture motor commands for `2000 ms` (`WEB_TIMEOUT`).
- **TCP Socket Server (Port `81`):** Accepts commands from `expression.py`[cite: 2, 3]:
  - `0`–`4` → `FORWARD`, `BACKWARD`, `LEFT`, `RIGHT`, `STOP` (when not in the 2-second web override window)[cite: 2, 3]
  - `E:HAPPY`, `E:SAD`, `E:NORMAL` → Updates the OLED eye animation (`EMOTION_HOLD_SEC = 2.0` in `expression.py`)[cite: 2, 3]

## Pin Connections (ESP8266 NodeMCU)
- **SSD1306 OLED (128×64, I2C `0x3C`):** `VCC` → `3V3`, `GND` → `GND`, `SDA` → `D2` (GPIO 4), `SCL` → `D1` (GPIO 5)[cite: 3]
- **L293D Motor Driver:**
  - **Motor A:** `EN1` → `D8` (GPIO 15), `IN1` → `D6` (GPIO 12), `IN2` → `D5` (GPIO 14)[cite: 3]
  - **Motor B:** `EN2` → `D7` (GPIO 13), `IN3` → `D3` (GPIO 0), `IN4` → `D0` (GPIO 16)[cite: 3]
  - **Power:** `VCC1` → `3V3` or `5V`, `VCC2` → Battery `+`, `GND` → Common `GND`[cite: 3]

## Files
```text
.
├── dasai_robot_2.ino   # AP setup, Port 80 web D-pad server, Port 81 TCP server, L293D & OLED control
└── expression.py       # MediaPipe Hands (0..4 motion) & Face Mesh (HAPPY/SAD/NORMAL) client
