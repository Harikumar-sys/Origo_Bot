/*
 * ==========================================================================
 * Dasai Mochi + Web Server Control + Gesture & Emotion Control (AP Mode)
 * ==========================================================================
 * Board  : ESP8266 (NodeMCU / Wemos D1 mini)
 * Display: SSD1306 128x64 I2C OLED
 * Motors : L293D driver, two DC motors (differential drive)
 *
 * Access Point Info:
 *   - SSID      : Dasai_Robot
 *   - Password  : 12345678
 *   - Static IP : http://192.168.4.1
 *   - Web UI    : Port 80 (Browser D-Pad)
 *   - Gestures  : Port 81 (Python TCP Socket: 0..4 motion, E:HAPPY/SAD/NORMAL)
 * ==========================================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// 1. CONFIGURATION & NETWORK
// ============================================================================

const char* ssid     = "Dasai_Robot";
const char* password = "12345678";

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// Servers
ESP8266WebServer server(80);       // Web server for phone browser on port 80
WiFiServer gestureServer(81);      // TCP server for Python gestures/emotions on port 81
WiFiClient gestureClient;
bool gestureClientActive = false;
String rxLine = "";

// Web Server Preference Timeout (milliseconds)
unsigned long lastWebCommandTime = 0;
const unsigned long WEB_TIMEOUT = 2000; // 2 seconds lockout for gestures after web use

// ---- OLED ----
#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C
#define SDA_PIN   D2
#define SCL_PIN   D1

// ---- Mood button (kept for wiring compatibility; emotion controlled by Python) ----
#define BUTTON_PIN   D4
#define DEBOUNCE_MS  40

#define AUTO_CYCLE   0
#define MOOD_TIME_MS 5000

// ---- Motor pins (L293D) ----
const int EN1        = D8;   // Enable for Motor A
const int motorA_in1 = D6;   // IN1 for Motor A
const int motorA_in2 = D5;   // IN2 for Motor A

const int EN2        = D7;   // Enable for Motor B
const int motorB_in3 = D3;   // IN3 for Motor B
const int motorB_in4 = D0;   // IN4 for Motor B

#define USE_PWM    0
#define FULL_SPEED 255
#define TURN_SPEED 150

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ============================================================================
// 2. HTML PAGE FOR BROWSER CONTROL PANEL
// ============================================================================
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Dasai Robot Control</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: #1a1a2e; color: #eee;
      font-family: 'Segoe UI', sans-serif;
      display: flex; flex-direction: column;
      align-items: center; justify-content: center;
      min-height: 100vh; gap: 24px;
    }
    h1 { font-size: 1.5rem; color: #00d4ff; text-align: center; }
    #status {
      font-size: 1.1rem; padding: 8px 24px; border-radius: 20px;
      background: #0f3460; border: 2px solid #00d4ff; min-width: 180px; text-align: center;
    }
    .dpad {
      display: grid; grid-template-columns: repeat(3, 80px); grid-template-rows: repeat(3, 80px); gap: 8px;
    }
    .btn {
      background: #16213e; border: 2px solid #0f3460; border-radius: 12px;
      color: #eee; font-size: 1.8rem; cursor: pointer; display: flex;
      align-items: center; justify-content: center; user-select: none;
    }
    .btn:active, .btn.active { background: #00d4ff; color: #1a1a2e; transform: scale(0.93); }
    .btn.stop { background: #c0392b; border-color: #e74c3c; }
  </style>
</head>
<body>
  <h1>🤖 Dasai Robot Controller</h1>
  <div id="status">STOP</div>
  <div class="dpad">
    <div></div>
    <button class="btn" onclick="send('forward')">▲</button>
    <div></div>
    <button class="btn" onclick="send('left')">◄</button>
    <button class="btn stop" onclick="send('stop')">■</button>
    <button class="btn" onclick="send('right')">►</button>
    <div></div>
    <button class="btn" onclick="send('backward')">▼</button>
    <div></div>
  </div>
  <script>
    const labels = { forward: 'FORWARD ▲', backward: 'BACKWARD ▼', left: 'LEFT ◄', right: 'RIGHT ►', stop: 'STOP ■' };
    function send(cmd) {
      document.getElementById('status').innerText = labels[cmd];
      fetch('/' + cmd).catch(() => {});
    }
  </script>
</body>
</html>
)rawliteral";

// ============================================================================
// 3. MOOD DEFINITIONS & ANIMATION STATE
// ============================================================================
enum Mood { NORMAL, HAPPY, ANGRY, TIRED, SLEEPY, LOVE, SAD, SURPRISED, MOOD_COUNT };
const char* moodNames[MOOD_COUNT] = { "NORMAL", "HAPPY", "ANGRY", "TIRED", "SLEEPY", "LOVE", "SAD", "SURPRISED" };
Mood mood = NORMAL;

float curX = 0, curY = 0, tgtX = 0, tgtY = 0;
float openness = 1.0, tgtOpen = 1.0;
float curW = 32, curH = 40, pop = 0;

unsigned long nowMs = 0, lastFrame = 0;
unsigned long nextBlink = 0, blinkEnd = 0, nextLook = 0, nextMood = 0, holdUntil = 0;
bool blinking = false;
bool lastReading = HIGH, buttonState = HIGH;
unsigned long lastDebounce = 0;

void setMood(Mood newMood) {
  mood = newMood;
  pop = 1.0;
  blinking = false;
  nextBlink = nowMs + 1500;
  nextLook = nowMs;
  Serial.print("OLED Mood -> ");
  Serial.println(moodNames[mood]);
}

// ============================================================================
// 4. MOTOR CONTROL FUNCTIONS
// ============================================================================
void setSpeed(int speed) {
#if USE_PWM
  analogWrite(EN1, speed); analogWrite(EN2, speed);
#else
  digitalWrite(EN1, speed > 0 ? HIGH : LOW);
  digitalWrite(EN2, speed > 0 ? HIGH : LOW);
#endif
}

void setMotors(int speed, int a1, int a2, int b3, int b4) {
  setSpeed(speed);
  digitalWrite(motorA_in1, a1); digitalWrite(motorA_in2, a2);
  digitalWrite(motorB_in3, b3); digitalWrite(motorB_in4, b4);
}

void moveForward()  { setMotors(FULL_SPEED, HIGH, LOW,  HIGH, LOW);  }
void moveBackward() { setMotors(FULL_SPEED, LOW,  HIGH, LOW,  HIGH); }
void turnLeft()     { setMotors(TURN_SPEED, LOW,  HIGH, HIGH, LOW);  }
void turnRight()    { setMotors(TURN_SPEED, HIGH, LOW,  LOW,  HIGH); }
void stopMotors()   { setMotors(0,          LOW,  LOW,  LOW,  LOW);  }

void executeMotorAction(int gestureIndex) {
  switch (gestureIndex) {
    case 0: moveForward();  break;
    case 1: moveBackward(); break;
    case 2: turnLeft();     break;
    case 3: turnRight();    break;
    case 4: stopMotors();   break;
    default: stopMotors();  break;
  }
}

// ============================================================================
// 5. WEB SERVER & GESTURE HANDLERS
// ============================================================================
void handleRoot()     { server.send(200, "text/html", PAGE); }
void handleWebForward()  { lastWebCommandTime = millis(); moveForward();  server.send(200, "text/plain", "ok"); }
void handleWebBackward() { lastWebCommandTime = millis(); moveBackward(); server.send(200, "text/plain", "ok"); }
void handleWebLeft()     { lastWebCommandTime = millis(); turnLeft();     server.send(200, "text/plain", "ok"); }
void handleWebRight()    { lastWebCommandTime = millis(); turnRight();    server.send(200, "text/plain", "ok"); }
void handleWebStop()     { lastWebCommandTime = millis(); stopMotors();   server.send(200, "text/plain", "ok"); }

void handleGestures() {
  // Accept a new client if none is connected or if the existing one disconnected
  if (!gestureClient || !gestureClient.connected()) {
    WiFiClient newClient = gestureServer.available();
    if (newClient) {
      if (gestureClient) {
        gestureClient.stop();
      }
      gestureClient = newClient;
      gestureClient.setNoDelay(true);
      gestureClientActive = true;
      rxLine = "";
      Serial.println("Python client connected on Port 81");
    }
  }

  if (!gestureClient || !gestureClient.connected()) {
    return;
  }

  // Read incoming gesture & emotion lines
  while (gestureClient.available()) {
    char c = gestureClient.read();
    if (c == '\n') {
      rxLine.trim();
      if (rxLine.startsWith("E:")) {
        String emo = rxLine.substring(2);
        emo.trim();
        if (emo == "HAPPY") {
          setMood(HAPPY);
        } else if (emo == "SAD") {
          setMood(SAD);
        } else if (emo == "NORMAL") {
          setMood(NORMAL);
        }
      } else if (rxLine.length() > 0 && isDigit(rxLine[0])) {
        int gestureIndex = rxLine.toInt();

        // PREFERENCE CHECK: Only execute gesture if web server hasn't been used recently
        if (millis() - lastWebCommandTime > WEB_TIMEOUT) {
          executeMotorAction(gestureIndex);
        }
      }
      rxLine = "";
    } else if (c != '\r') {
      if (rxLine.length() < 16) {
        rxLine += c;
      } else {
        rxLine = "";
      }
    }
  }
}

// ============================================================================
// 6. SERIAL & BUTTON INPUTS
// ============================================================================
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    int selectedMood = -1;
    if      (c == 'n') selectedMood = NORMAL;
    else if (c == 'h') selectedMood = HAPPY;
    else if (c == 'a') selectedMood = ANGRY;
    else if (c == 't') selectedMood = TIRED;
    else if (c == 's') selectedMood = SLEEPY;
    else if (c == 'l') selectedMood = LOVE;
    else if (c == 'd') selectedMood = SAD;
    else if (c == 'u') selectedMood = SURPRISED;

    if (selectedMood >= 0) {
      setMood((Mood)selectedMood);
      holdUntil = nowMs + 15000;
      nextMood = holdUntil;
    }
  }
}

void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastReading) { lastDebounce = nowMs; lastReading = reading; }
  if ((nowMs - lastDebounce) > DEBOUNCE_MS && reading != buttonState) {
    buttonState = reading;
    // Emotion is controlled by Python over TCP Port 81
  }
}

// ============================================================================
// 7. OLED ANIMATION FUNCTIONS
// ============================================================================
void updateBehavior() {
  bool canBlink = (mood == NORMAL || mood == ANGRY || mood == TIRED || mood == SAD || mood == SURPRISED);
  if (!blinking && canBlink && nowMs >= nextBlink) {
    blinking = true; blinkEnd = nowMs + (mood == TIRED ? 350 : 130);
  }
  if (blinking && nowMs >= blinkEnd) {
    blinking = false; nextBlink = nowMs + random(1500, 4500);
  }
  tgtOpen = blinking ? 0.0 : ((mood == TIRED) ? 0.65 : 1.0);

  if (nowMs >= nextLook) {
    if (mood == NORMAL) {
      tgtX = (random(3) == 0) ? 0 : random(-10, 11);
      tgtY = (random(3) == 0) ? 0 : random(-6, 7);
      nextLook = nowMs + random(1000, 2500);
    } else if (mood == SAD) {
      tgtX = random(-4, 5); tgtY = 8; nextLook = nowMs + random(1500, 3000);
    } else if (mood == TIRED) {
      tgtX = random(-6, 7); tgtY = 4; nextLook = nowMs + random(2000, 4000);
    } else {
      tgtX = 0; tgtY = 0; nextLook = nowMs + 1000;
    }
  }
}

void animate() {
  float targetW = (mood == SAD) ? 30 : ((mood == SURPRISED) ? 36 : 32);
  float targetH = (mood == SAD) ? 32 : ((mood == SURPRISED) ? 50 : 40);

  curW += (targetW - curW) * 0.25;
  curH += (targetH - curH) * 0.25;
  curX += (tgtX - curX) * 0.3;
  curY += (tgtY - curY) * 0.3;
  openness += (tgtOpen - openness) * ((mood == TIRED) ? 0.15 : 0.5);
  pop *= 0.85;
}

void drawHeart(int cx, int cy, int s) {
  display.fillCircle(cx - s / 2, cy - s / 4, s / 2 + 1, WHITE);
  display.fillCircle(cx + s / 2, cy - s / 4, s / 2 + 1, WHITE);
  display.fillTriangle(cx - s - 1, cy - s / 6, cx + s + 1, cy - s / 6, cx, cy + s, WHITE);
}

void drawEye(int cx, int cy, int w, int h, bool isLeft) {
  int x = cx - w / 2, y = cy - h / 2;
  if (mood == HAPPY) {
    for (int i = 0; i < 4; i++) display.drawCircle(cx, cy + 8, 13 + i, WHITE);
    display.fillRect(cx - 20, cy + 8, 41, 30, BLACK);
    return;
  }
  if (mood == LOVE) {
    drawHeart(cx, cy, 10 + (int)(3 * sin(nowMs * 0.01)));
    return;
  }
  display.fillRoundRect(x, y, w, h, min(10, h / 2), WHITE);
  if (h < 10) return;

  if (mood == ANGRY) {
    int drop = h * 4 / 10;
    if (isLeft) display.fillTriangle(x - 1, y - 1, x + w + 1, y - 1, x + w + 1, y + drop, BLACK);
    else display.fillTriangle(x - 1, y - 1, x + w + 1, y - 1, x - 1, y + drop, BLACK);
  } else if (mood == TIRED || mood == SAD) {
    int drop = (mood == SAD) ? h * 5 / 10 : h * 35 / 100;
    if (isLeft) display.fillTriangle(x - 1, y - 1, x + w + 1, y - 1, x - 1, y + drop, BLACK);
    else display.fillTriangle(x - 1, y - 1, x + w + 1, y - 1, x + w + 1, y + drop, BLACK);
    if (mood == SAD) {
      int phase = (nowMs / 25 + (isLeft ? 0 : 50)) % 100;
      int tearY = y + h + phase * 16 / 100;
      if (phase < 90 && tearY < 62) display.fillCircle(cx, tearY, 2, WHITE);
    }
  } else if (mood == SURPRISED && h >= 20) {
    display.fillCircle(cx + (int)(curX / 2), cy + (int)(curY / 2), 6, BLACK);
  }
}

void drawSleepZ() {
  display.setTextColor(WHITE);
  for (int i = 0; i < 3; i++) {
    int phase = (nowMs / 40 + i * 33) % 100;
    display.setTextSize(i == 2 ? 2 : 1);
    display.setCursor(92 + phase / 6 + i * 4, 28 - phase * 28 / 100);
    display.print(i == 2 ? "Z" : "z");
  }
}

void drawEyes() {
  display.clearDisplay();
  int w = (int)(curW * (1 + 0.25 * pop));
  int hBase = (int)(curH * (1 + 0.25 * pop));
  int h = max(3, (int)(hBase * openness));
  int cy = 32 + (int)curY, dx = (int)curX;

  if (mood == HAPPY) cy += (int)(sin(nowMs * 0.012) * 3);
  else if (mood == ANGRY) dx += random(-2, 3);
  else if (mood == SLEEPY) { h = 3 + (int)(2.5 + 2.5 * sin(nowMs * 0.003)); cy = 40; }

  drawEye(64 - 28 + dx, cy, w, h, true);
  drawEye(64 + 28 + dx, cy, w, h, false);
  if (mood == SLEEPY) drawSleepZ();
  display.display();
}

// ============================================================================
// 8. SETUP & LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  system_update_cpu_freq(SYS_CPU_160MHZ);

  pinMode(EN1, OUTPUT); pinMode(motorA_in1, OUTPUT); pinMode(motorA_in2, OUTPUT);
  pinMode(EN2, OUTPUT); pinMode(motorB_in3, OUTPUT); pinMode(motorB_in4, OUTPUT);
  stopMotors();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    for (;;) delay(1000);
  }
  randomSeed(os_random());
  display.clearDisplay(); display.display();

  // Setup AP and Static IP
  WiFi.mode(WIFI_AP);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);

  server.on("/",         handleRoot);
  server.on("/forward",  handleWebForward);
  server.on("/backward", handleWebBackward);
  server.on("/left",     handleWebLeft);
  server.on("/right",    handleWebRight);
  server.on("/stop",     handleWebStop);
  server.onNotFound([](){ server.send(404, "text/plain", "Not found"); });

  // Start both servers
  server.begin();        // Web server on Port 80
  gestureServer.begin(); // Gesture & emotion TCP server on Port 81

  Serial.println("Access Point Started!");
  Serial.print("IP: http://"); Serial.println(WiFi.softAPIP());

  nowMs = millis();
  nextMood = nowMs + MOOD_TIME_MS;
  nextBlink = nowMs + 2000;
}

void loop() {
  nowMs = millis();

  // Handle inputs and networking
  server.handleClient();   // Process browser requests (Port 80)
  handleGestures();        // Process Python gestures & emotions (Port 81)
  handleSerial();
  handleButton();

  if (nowMs - lastFrame < 33) return;
  lastFrame = nowMs;

  updateBehavior();
  animate();
  drawEyes();
}