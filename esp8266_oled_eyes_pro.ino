#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <math.h>

// -------------------- Display wiring --------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI D7
#define OLED_CLK D5
#define OLED_DC D4
#define OLED_CS D8
#define OLED_RST D3

// -------------------- Buzzer / LED wiring --------------------
#define BUZZER_PIN D2
#define LED_PIN D1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RST, OLED_CS);
ESP8266WebServer server(80);

// -------------------- WiFi credentials (editable from panel) --------------------
String wifiSsid = "EHSAN-2.4G";
String wifiPassword = "Ehsan1386@";

// Static IP (optional)
bool useStaticIp = true;
IPAddress local_IP(192, 168, 1, 4);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// Access Point fallback
String apSsid = "Ehsan-Eye-Setup";
String apPassword = "12345678";
bool apModeActive = false;

// -------------------- Time / Iran --------------------
const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.nist.gov";
constexpr long IRAN_UTC_OFFSET_SECONDS = 12600; // UTC +03:30
const char *IRAN_TZ = "<+0330>-3:30";
IPAddress dns1(8, 8, 8, 8);
IPAddress dns2(1, 1, 1, 1);
unsigned long lastTimeSyncMs = 0;
constexpr unsigned long AUTO_SYNC_INTERVAL_MS = 15UL * 60UL * 1000UL;
unsigned long lastClockCheckMs = 0;
bool clockHealthy = false;

// -------------------- Eye geometry --------------------
constexpr int EYE_RADIUS = 16;
constexpr int PUPIL_RADIUS = 7;
constexpr int EYE_SEPARATION = 30;
constexpr int EYE_Y = 32;

int leftEyeX = (SCREEN_WIDTH / 2) - EYE_SEPARATION;
int rightEyeX = (SCREEN_WIDTH / 2) + EYE_SEPARATION;

// -------------------- State --------------------
String currentMood = "normal";
String currentAnimation = "alive";
int lookDirectionX = 0;
int lookDirectionY = 0;

bool isBlinking = false;
unsigned long blinkEndTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long nextBlinkDelay = 3200;

unsigned long lastMoveTime = 0;
unsigned long nextMoveDelay = 2200;

unsigned long winkEndTime = 0;
bool autoMoveEnabled = true;

bool showClock = true;
bool showSeconds = true;
bool showDate = false;

unsigned long lastFrameTick = 0;
unsigned long frameCount = 0;
int orbitAngle = 0;
int matrixShift = 0;

String overlayMessage = "";
unsigned long overlayMessageUntilMs = 0;

// -------------------- LED (D1) --------------------
String ledMode = "breath"; // off, on, breath, pulse, strobe, heartbeat, follow
int ledBrightnessMax = 900; // 0..1023
unsigned long lastLedTick = 0;
bool ledEnabled = true;

// -------------------- Buzzer --------------------
struct MelodyNote {
  int frequency;
  int durationMs;
  int gapMs;
};

constexpr MelodyNote HAPPY_JINGLE[] = {{523, 90, 20}, {659, 90, 20}, {784, 120, 35}, {1046, 130, 50}, {784, 120, 35}};
constexpr MelodyNote ALERT_JINGLE[] = {{880, 120, 30}, {440, 120, 30}, {880, 120, 30}, {440, 150, 60}};
constexpr MelodyNote POWER_JINGLE[] = {{392, 90, 15}, {523, 110, 20}, {659, 120, 25}, {784, 140, 40}};
constexpr MelodyNote COOL_JINGLE[] = {{330, 80, 10}, {392, 80, 10}, {494, 80, 10}, {587, 140, 25}, {784, 160, 35}};
constexpr MelodyNote LOVE_JINGLE[] = {{659, 110, 15}, {784, 110, 15}, {880, 110, 20}, {784, 140, 15}, {659, 180, 40}};
constexpr MelodyNote BOSS_JINGLE[] = {{294, 80, 10}, {294, 80, 10}, {392, 100, 15}, {523, 130, 20}, {784, 160, 25}, {1046, 200, 50}};

const MelodyNote *activeMelody = nullptr;
size_t activeMelodyLength = 0;
size_t melodyIndex = 0;
bool melodyPlayingTone = false;
unsigned long melodyStepTime = 0;

void notify(const String &msg, unsigned long ms = 2500) {
  overlayMessage = msg;
  overlayMessageUntilMs = millis() + ms;
  Serial.println(msg);
}

void playMelody(const MelodyNote *melody, size_t length) {
  activeMelody = melody;
  activeMelodyLength = length;
  melodyIndex = 0;
  melodyPlayingTone = false;
  melodyStepTime = 0;
}

void playMelodyByName(const String &kind) {
  if (kind == "happy") playMelody(HAPPY_JINGLE, sizeof(HAPPY_JINGLE) / sizeof(HAPPY_JINGLE[0]));
  else if (kind == "alert") playMelody(ALERT_JINGLE, sizeof(ALERT_JINGLE) / sizeof(ALERT_JINGLE[0]));
  else if (kind == "startup") playMelody(POWER_JINGLE, sizeof(POWER_JINGLE) / sizeof(POWER_JINGLE[0]));
  else if (kind == "cool") playMelody(COOL_JINGLE, sizeof(COOL_JINGLE) / sizeof(COOL_JINGLE[0]));
  else if (kind == "love") playMelody(LOVE_JINGLE, sizeof(LOVE_JINGLE) / sizeof(LOVE_JINGLE[0]));
  else if (kind == "boss") playMelody(BOSS_JINGLE, sizeof(BOSS_JINGLE) / sizeof(BOSS_JINGLE[0]));
}

void updateBuzzer() {
  if (!activeMelody || activeMelodyLength == 0) return;

  unsigned long now = millis();
  const MelodyNote &note = activeMelody[melodyIndex];

  if (!melodyPlayingTone) {
    tone(BUZZER_PIN, note.frequency, note.durationMs);
    melodyPlayingTone = true;
    melodyStepTime = now;
    return;
  }

  if (now - melodyStepTime >= static_cast<unsigned long>(note.durationMs + note.gapMs)) {
    melodyPlayingTone = false;
    melodyIndex++;
    if (melodyIndex >= activeMelodyLength) {
      noTone(BUZZER_PIN);
      activeMelody = nullptr;
      activeMelodyLength = 0;
      melodyIndex = 0;
    }
  }
}

void setLedPWM(int value) {
  value = constrain(value, 0, 1023);
  analogWrite(LED_PIN, value);
}

void setLedOff() {
  analogWrite(LED_PIN, 0);
  digitalWrite(LED_PIN, LOW);
}

void setLedOn() {
  analogWrite(LED_PIN, 1023);
  digitalWrite(LED_PIN, HIGH);
}

bool isNightNow() {
  time_t now = time(nullptr);
  if (now < 100000) return false;
  now += IRAN_UTC_OFFSET_SECONDS;
  struct tm ti;
  gmtime_r(&now, &ti);
  return (ti.tm_hour >= 23 || ti.tm_hour <= 6);
}

void updateLed() {
  if (!ledEnabled) {
    setLedOff();
    return;
  }

  unsigned long now = millis();
  if (now - lastLedTick < 20) return;
  lastLedTick = now;

  if (ledMode == "off") {
    setLedOff();
    return;
  }

  if (ledMode == "on") {
    setLedOn();
    return;
  }

  int base = ledBrightnessMax;
  if (isNightNow()) base = max(130, ledBrightnessMax / 3);

  if (ledMode == "breath") {
    float phase = (frameCount % 180) * 0.0349f;
    int pwm = static_cast<int>((sin(phase) * 0.5f + 0.5f) * base);
    setLedPWM(pwm);
  } else if (ledMode == "pulse") {
    int section = frameCount % 50;
    int pwm = (section < 12) ? base : ((section < 22) ? base / 4 : 0);
    setLedPWM(pwm);
  } else if (ledMode == "strobe") {
    setLedPWM((frameCount % 8 < 2) ? base : 0);
  } else if (ledMode == "heartbeat") {
    int t = frameCount % 80;
    int pwm = 0;
    if (t < 5 || (t > 10 && t < 15)) pwm = base;
    else if (t < 20) pwm = base / 3;
    setLedPWM(pwm);
  } else if (ledMode == "follow") {
    if (currentMood == "love") setLedPWM(base);
    else if (currentMood == "angry") setLedPWM((frameCount % 10 < 3) ? base : 0);
    else if (currentMood == "sad") setLedPWM(base / 6);
    else setLedPWM(base / 2);
  } else {
    setLedPWM(base / 2);
  }
}

void queueRandomBlink() { nextBlinkDelay = 2400 + random(2800); }
void queueRandomMove() { nextMoveDelay = 1200 + random(3200); }

void syncTimeIran() {
  setenv("TZ", IRAN_TZ, 1);
  tzset();
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
  lastTimeSyncMs = millis();
}

bool getIranTime(struct tm *timeInfo) {
  if (!timeInfo) return false;
  time_t now = time(nullptr);
  if (now < 100000) return false;
  localtime_r(&now, timeInfo);
  return true;
}

String nowTimeString() {
  struct tm timeInfo;
  if (!getIranTime(&timeInfo)) return "--:--";

  char buffer[12];
  if (showSeconds) strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeInfo);
  else strftime(buffer, sizeof(buffer), "%H:%M", &timeInfo);
  return String(buffer);
}

String nowDateString() {
  struct tm timeInfo;
  if (!getIranTime(&timeInfo)) return "NO-DATE";

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y/%m/%d", &timeInfo);
  return String(buffer);
}

bool waitForTimeSync(unsigned long timeoutMs = 12000) {
  unsigned long start = millis();
  struct tm t;
  while (millis() - start < timeoutMs) {
    if (getIranTime(&t)) return true;
    delay(200);
  }
  return false;
}

void updateClockHealth() {
  if (millis() - lastClockCheckMs < 3000) return;
  lastClockCheckMs = millis();
  struct tm t;
  clockHealthy = getIranTime(&t);
}

void drawBrandBadge() {
  display.fillRoundRect(2, 2, 68, 13, 3, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(5, 5);
  display.print("Ehsan Fazli");
  display.setTextColor(SSD1306_WHITE);
}

void drawTimePanel() {
  if (!showClock) return;

  display.drawRoundRect(73, 0, 55, 22, 3, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(76, 3);
  display.print(nowTimeString());

  if (showDate) {
    display.setCursor(76, 13);
    String dateText = nowDateString();
    display.print(dateText.substring(0, min(10, static_cast<int>(dateText.length()))));
  }
}

void drawHeart(int cx, int cy, int size, int color) {
  display.fillCircle(cx - size / 2, cy - size / 3, size / 3, color);
  display.fillCircle(cx + size / 2, cy - size / 3, size / 3, color);
  display.fillTriangle(cx - size, cy - size / 4, cx + size, cy - size / 4, cx, cy + size, color);
}

void drawAnimationDecor() {
  if (currentAnimation == "wave") {
    int waveY = 55 + (frameCount % 2);
    for (int x = 0; x < SCREEN_WIDTH; x += 8) {
      long waveDelta = static_cast<long>((x + (frameCount * 2UL)) % 16UL) - 8L;
      int h = 2 + static_cast<int>(labs(waveDelta)) / 2;
      display.drawLine(x, waveY, x + 4, waveY - h, SSD1306_WHITE);
    }
  } else if (currentAnimation == "scan") {
    int scanX = (frameCount * 3) % SCREEN_WIDTH;
    display.drawFastVLine(scanX, 0, SCREEN_HEIGHT, SSD1306_WHITE);
  } else if (currentAnimation == "matrix") {
    matrixShift = (matrixShift + 1) % 8;
    for (int x = 0; x < SCREEN_WIDTH; x += 8) {
      int y = (x + matrixShift * 7 + frameCount) % SCREEN_HEIGHT;
      display.drawPixel(x, y, SSD1306_WHITE);
      display.drawPixel(x, (y + 9) % SCREEN_HEIGHT, SSD1306_WHITE);
      if ((x / 8) % 2 == 0) display.drawPixel(x, (y + 18) % SCREEN_HEIGHT, SSD1306_WHITE);
    }
  } else if (currentAnimation == "cyber") {
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.drawFastHLine(0, 47, SCREEN_WIDTH, SSD1306_WHITE);
    display.drawFastHLine(0, 48, SCREEN_WIDTH, SSD1306_WHITE);
  } else if (currentAnimation == "starfield") {
    for (int i = 0; i < 18; i++) {
      int x = (i * 17 + frameCount * (i % 3 + 1)) % SCREEN_WIDTH;
      int y = (i * 11 + frameCount * (i % 2 + 1)) % SCREEN_HEIGHT;
      display.drawPixel(x, y, SSD1306_WHITE);
    }
  } else if (currentAnimation == "heart_rain") {
    for (int i = 0; i < 10; i++) {
      int x = (i * 13 + (i % 2) * 6) % SCREEN_WIDTH;
      int y = static_cast<int>((frameCount * (i % 3 + 1) + i * 9) % SCREEN_HEIGHT);
      drawHeart(x, y, 3 + (i % 2), SSD1306_WHITE);
    }
  } else if (currentAnimation == "fire") {
    for (int x = 0; x < SCREEN_WIDTH; x += 6) {
      int h = static_cast<int>((sin((x + frameCount * 5) * 0.12f) * 0.5f + 0.5f) * 12);
      display.drawLine(x, SCREEN_HEIGHT - 1, x, SCREEN_HEIGHT - 1 - h, SSD1306_WHITE);
    }
  }
}

void drawMoodOverlay(int rightPupilX, int rightPupilY) {
  if (currentMood == "happy") {
    display.drawCircle(leftEyeX, EYE_Y, EYE_RADIUS + 2, SSD1306_WHITE);
    display.drawCircle(rightEyeX, EYE_Y, EYE_RADIUS + 2, SSD1306_WHITE);
  } else if (currentMood == "sad") {
    display.drawFastHLine(leftEyeX - EYE_RADIUS, EYE_Y + EYE_RADIUS, EYE_RADIUS * 2, SSD1306_WHITE);
    display.drawFastHLine(rightEyeX - EYE_RADIUS, EYE_Y + EYE_RADIUS, EYE_RADIUS * 2, SSD1306_WHITE);
  } else if (currentMood == "angry") {
    display.drawLine(leftEyeX - EYE_RADIUS, EYE_Y - EYE_RADIUS, leftEyeX, EYE_Y - EYE_RADIUS + 5, SSD1306_WHITE);
    display.drawLine(rightEyeX + EYE_RADIUS, EYE_Y - EYE_RADIUS, rightEyeX, EYE_Y - EYE_RADIUS + 5, SSD1306_WHITE);
  } else if (currentMood == "robot") {
    display.drawRect(leftEyeX - 15, EYE_Y - 15, 30, 30, SSD1306_BLACK);
    display.drawRect(rightEyeX - 15, EYE_Y - 15, 30, 30, SSD1306_BLACK);
    display.drawRect(leftEyeX - 15, EYE_Y - 15, 30, 30, SSD1306_WHITE);
    display.drawRect(rightEyeX - 15, EYE_Y - 15, 30, 30, SSD1306_WHITE);
  } else if (currentMood == "wink") {
    display.drawFastHLine(leftEyeX - EYE_RADIUS + 2, EYE_Y, (EYE_RADIUS - 2) * 2, SSD1306_BLACK);
    display.fillCircle(rightPupilX, rightPupilY, PUPIL_RADIUS, SSD1306_BLACK);
  } else if (currentMood == "sleepy") {
    display.drawFastHLine(leftEyeX - EYE_RADIUS + 2, EYE_Y - 3, (EYE_RADIUS - 2) * 2, SSD1306_BLACK);
    display.drawFastHLine(rightEyeX - EYE_RADIUS + 2, EYE_Y - 3, (EYE_RADIUS - 2) * 2, SSD1306_BLACK);
  } else if (currentMood == "surprised") {
    display.drawCircle(leftEyeX, EYE_Y, EYE_RADIUS + 4, SSD1306_WHITE);
    display.drawCircle(rightEyeX, EYE_Y, EYE_RADIUS + 4, SSD1306_WHITE);
  } else if (currentMood == "love") {
    drawHeart(leftEyeX, EYE_Y, 6, SSD1306_BLACK);
    drawHeart(rightEyeX, EYE_Y, 6, SSD1306_BLACK);
  } else if (currentMood == "crazy") {
    display.drawCircle(leftEyeX + 3, EYE_Y - 2, 2, SSD1306_BLACK);
    display.drawCircle(rightEyeX - 3, EYE_Y + 2, 2, SSD1306_BLACK);
  }
}

void drawOverlayMessage() {
  if (overlayMessage.length() == 0) return;
  if (millis() > overlayMessageUntilMs) return;

  display.fillRect(0, 46, 128, 18, SSD1306_BLACK);
  display.drawRect(0, 46, 128, 18, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(3, 51);
  String msg = overlayMessage;
  if (msg.length() > 22) msg = msg.substring(0, 22);
  display.print(msg);
}

void drawEyes(int pupilOffsetX, int pupilOffsetY, bool blink = false) {
  display.clearDisplay();

  int dynamicPupilRadius = PUPIL_RADIUS;
  if (currentAnimation == "alive") dynamicPupilRadius = PUPIL_RADIUS + ((frameCount / 6) % 2 == 0 ? 0 : 1);

  int leftPupilX = leftEyeX + pupilOffsetX;
  int leftPupilY = EYE_Y + pupilOffsetY;
  int rightPupilX = rightEyeX + pupilOffsetX;
  int rightPupilY = EYE_Y + pupilOffsetY;

  if (currentAnimation == "orbit") {
    orbitAngle = (orbitAngle + 8) % 360;
    float rad = orbitAngle * 0.0174533f;
    leftPupilX = leftEyeX + static_cast<int>(cos(rad) * 4.5f);
    leftPupilY = EYE_Y + static_cast<int>(sin(rad) * 4.5f);
    rightPupilX = rightEyeX + static_cast<int>(cos(rad) * 4.5f);
    rightPupilY = EYE_Y + static_cast<int>(sin(rad) * 4.5f);
  }

  drawAnimationDecor();
  drawBrandBadge();
  drawTimePanel();

  display.fillCircle(leftEyeX, EYE_Y, EYE_RADIUS, SSD1306_WHITE);
  display.fillCircle(rightEyeX, EYE_Y, EYE_RADIUS, SSD1306_WHITE);

  if (blink) {
    display.drawFastHLine(leftEyeX - EYE_RADIUS + 2, EYE_Y, (EYE_RADIUS - 2) * 2, SSD1306_BLACK);
    display.drawFastHLine(rightEyeX - EYE_RADIUS + 2, EYE_Y, (EYE_RADIUS - 2) * 2, SSD1306_BLACK);
  } else {
    display.fillCircle(leftPupilX, leftPupilY, dynamicPupilRadius, SSD1306_BLACK);
    display.fillCircle(rightPupilX, rightPupilY, dynamicPupilRadius, SSD1306_BLACK);
    if (currentMood != "robot") {
      display.fillCircle(leftPupilX - 2, leftPupilY - 2, 2, SSD1306_WHITE);
      display.fillCircle(rightPupilX - 2, rightPupilY - 2, 2, SSD1306_WHITE);
    }
  }

  drawMoodOverlay(rightPupilX, rightPupilY);

  display.setTextSize(1);
  display.setCursor(2, 55);
  display.print("M:");
  display.print(currentMood.substring(0, min(6, static_cast<int>(currentMood.length()))));
  display.setCursor(66, 55);
  display.print(clockHealthy ? "T:OK" : "T:WAIT");

  drawOverlayMessage();
  display.display();
}

void updateEyes() {
  unsigned long now = millis();

  if (now - lastFrameTick >= 40) {
    frameCount++;
    lastFrameTick = now;
  }

  if (winkEndTime > 0 && now >= winkEndTime) {
    currentMood = "normal";
    winkEndTime = 0;
  }

  if (!isBlinking && now - lastBlinkTime > nextBlinkDelay) {
    isBlinking = true;
    blinkEndTime = now + 140;
    lastBlinkTime = now;
    queueRandomBlink();
  }

  if (isBlinking && now >= blinkEndTime) isBlinking = false;

  if (autoMoveEnabled && now - lastMoveTime > nextMoveDelay) {
    lookDirectionX = random(-5, 6);
    lookDirectionY = random(-5, 6);
    lastMoveTime = now;
    queueRandomMove();
  }

  updateClockHealth();
  drawEyes(lookDirectionX, lookDirectionY, isBlinking);
}

void showLoadingScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 6);
  display.println("EHSAN");
  display.setCursor(10, 24);
  display.println("FAZLI");

  display.setTextSize(1);
  display.setCursor(9, 45);
  display.println("EYE MASTER CORE");

  for (int i = 0; i <= SCREEN_WIDTH; i += 4) {
    display.drawRoundRect(0, 55, SCREEN_WIDTH, 8, 3, SSD1306_WHITE);
    display.fillRoundRect(1, 56, i - 2, 6, 2, SSD1306_WHITE);
    display.display();
    delay(12);
  }

  playMelodyByName("startup");
}

String buildHtml() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Ehsan Fazli - Ultimate Eye Console</title>
<style>
:root{--bg:#020617;--card:#0f172a;--line:#334155;--txt:#e2e8f0;--main:#22c55e;--alt:#38bdf8;--warn:#f97316;--pink:#ec4899}
*{box-sizing:border-box}body{margin:0;font-family:tahoma,sans-serif;background:radial-gradient(circle at top,#1d4ed8,#020617 60%);color:var(--txt)}
.wrap{max-width:980px;margin:auto;padding:14px}.head{background:rgba(15,23,42,.9);padding:14px;border-radius:14px;text-align:center;border:1px solid var(--line)}
.head h1{margin:4px 0;font-size:clamp(20px,5vw,32px)}.chip{display:inline-block;background:#0ea5e9;color:#082f49;padding:4px 10px;border-radius:999px;font-weight:700}
.grid{display:grid;gap:10px;margin-top:10px}.card{background:rgba(15,23,42,.95);padding:12px;border:1px solid var(--line);border-radius:12px}
.card h3{margin:0 0 10px;color:#67e8f9;font-size:16px}.btns{display:flex;flex-wrap:wrap;gap:7px}
button{border:0;border-radius:10px;padding:10px 12px;color:#fff;font-weight:700;cursor:pointer;background:linear-gradient(135deg,var(--main),#15803d);font-size:13px}
button.alt{background:linear-gradient(135deg,var(--alt),#0369a1)} button.warn{background:linear-gradient(135deg,var(--warn),#c2410c)} button.pink{background:linear-gradient(135deg,var(--pink),#be185d)} button.dark{background:linear-gradient(135deg,#475569,#0f172a)}
.row{display:flex;gap:8px;flex-wrap:wrap}.row input{flex:1;min-width:120px;background:#020617;border:1px solid var(--line);color:#fff;padding:9px;border-radius:8px}
#status{margin-top:12px;background:#0b1220;border:1px solid var(--line);padding:10px;border-radius:10px;color:#fde047;min-height:42px;white-space:pre-line}
.small{font-size:12px;opacity:.8} @media (max-width:640px){.wrap{padding:10px}button{flex:1;min-width:44%}}
</style>
</head>
<body><div class="wrap">
  <div class="head"><h1>پنل حرفه‌ای چشم OLED</h1><div>by <span class="chip">Ehsan Fazli</span></div><div class="small">Responsive • Error-safe • AP fallback</div></div>

  <div class="grid">
    <div class="card"><h3>🎭 Mood</h3><div class="btns">
      <button onclick="setMood('normal')">normal</button><button onclick="setMood('happy')">happy</button><button onclick="setMood('sad')">sad</button><button onclick="setMood('angry')">angry</button>
      <button onclick="setMood('robot')">robot</button><button onclick="setMood('sleepy')">sleepy</button><button onclick="setMood('surprised')">surprised</button><button class="pink" onclick="setMood('love')">love</button>
      <button class="warn" onclick="setMood('wink')">wink</button><button class="dark" onclick="setMood('crazy')">crazy</button>
    </div></div>

    <div class="card"><h3>✨ Animation</h3><div class="btns">
      <button class="alt" onclick="setAnim('alive')">alive</button><button class="alt" onclick="setAnim('wave')">wave</button><button class="alt" onclick="setAnim('scan')">scan</button>
      <button class="alt" onclick="setAnim('matrix')">matrix</button><button class="alt" onclick="setAnim('orbit')">orbit</button><button class="alt" onclick="setAnim('cyber')">cyber</button>
      <button class="alt" onclick="setAnim('starfield')">starfield</button><button class="pink" onclick="setAnim('heart_rain')">heart rain</button><button class="warn" onclick="setAnim('fire')">fire</button>
    </div></div>

    <div class="card"><h3>💡 LED D1</h3><div class="btns">
      <button onclick="led('off')">off</button><button onclick="led('on')">on</button><button onclick="led('breath')">breath</button><button onclick="led('pulse')">pulse</button>
      <button onclick="led('strobe')">strobe</button><button onclick="led('heartbeat')">heartbeat</button><button class="alt" onclick="led('follow')">follow</button>
      <button class="warn" onclick="ledEnable('off')">disable LED</button><button class="warn" onclick="ledEnable('on')">enable LED</button>
    </div></div>

    <div class="card"><h3>📶 WiFi management (STA/AP)</h3>
      <div class="row"><input id="ssid" placeholder="SSID"><input id="pass" placeholder="Password"></div>
      <div class="btns" style="margin-top:7px"><button onclick="applyWifi()">apply WiFi</button><button class="alt" onclick="scanWifi()">scan SSID</button><button class="warn" onclick="apStart()">start AP</button><button class="warn" onclick="apStop()">stop AP</button></div>
      <div class="small" id="wifiList">No scan yet.</div>
    </div>

    <div class="card"><h3>📝 Text to OLED</h3>
      <div class="row"><input id="msg" placeholder="متن را بنویس... مثلا سلام احسان"></div>
      <div class="btns" style="margin-top:7px"><button class="pink" onclick="sendText()">show text</button><button class="dark" onclick="clearText()">clear text</button></div>
    </div>

    <div class="card"><h3>🕒 Iran Clock (fixed)</h3><div class="btns">
      <button onclick="clockMode('on')">clock on</button><button onclick="clockMode('off')">clock off</button><button class="alt" onclick="clockSec('on')">seconds on</button><button class="alt" onclick="clockSec('off')">seconds off</button>
      <button class="alt" onclick="clockDate('on')">date on</button><button class="alt" onclick="clockDate('off')">date off</button><button class="warn" onclick="syncTime()">sync time</button><button class="dark" onclick="state()">state</button>
    </div></div>
  </div>

  <div id="status">Ready 🚀</div>
</div>
<script>
function setStatus(t){document.getElementById('status').innerText=t}
function req(path,msg){fetch(path).then(r=>r.text().then(t=>({ok:r.ok,t}))).then(o=>setStatus((o.ok?'✅ ':'❌ ')+msg+' | '+o.t)).catch(()=>setStatus('❌ network error'))}
function setMood(v){req('/mood?state='+encodeURIComponent(v),'mood '+v)}
function setAnim(v){req('/anim?mode='+encodeURIComponent(v),'anim '+v)}
function led(v){req('/led?mode='+encodeURIComponent(v),'led '+v)}
function ledEnable(v){req('/led/enable?value='+encodeURIComponent(v),'led enable '+v)}
function applyWifi(){const s=document.getElementById('ssid').value;const p=document.getElementById('pass').value;req('/wifi/connect?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p),'wifi connect')}
function scanWifi(){fetch('/wifi/scan').then(r=>r.json()).then(j=>{document.getElementById('wifiList').innerText='Found '+j.count+':\n'+j.ssids.join('\n');setStatus('✅ wifi scan done')}).catch(()=>setStatus('❌ wifi scan error'))}
function apStart(){req('/wifi/ap/start','AP start')}
function apStop(){req('/wifi/ap/stop','AP stop')}
function sendText(){const m=document.getElementById('msg').value;req('/display/text?msg='+encodeURIComponent(m),'show text')}
function clearText(){req('/display/text?clear=1','clear text')}
function clockMode(v){req('/clock?show='+v,'clock '+v)}
function clockSec(v){req('/clock/seconds?show='+v,'seconds '+v)}
function clockDate(v){req('/clock/date?show='+v,'date '+v)}
function syncTime(){req('/time/sync','time sync')}
function state(){fetch('/api/state').then(r=>r.json()).then(j=>setStatus(JSON.stringify(j,null,2))).catch(()=>setStatus('❌ state error'))}
</script>
</body></html>
)rawliteral";
  return html;
}

bool validMood(const String &m) {
  return m == "normal" || m == "happy" || m == "sad" || m == "angry" || m == "robot" || m == "wink" || m == "sleepy" || m == "surprised" || m == "love" || m == "crazy";
}

bool validAnim(const String &m) {
  return m == "alive" || m == "wave" || m == "scan" || m == "matrix" || m == "orbit" || m == "cyber" || m == "starfield" || m == "heart_rain" || m == "fire";
}

bool validLedMode(const String &m) {
  return m == "off" || m == "on" || m == "breath" || m == "pulse" || m == "strobe" || m == "heartbeat" || m == "follow";
}

bool connectStation(const String &ssid, const String &pass, unsigned long timeoutMs = 16000) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(150);

  if (useStaticIp) WiFi.config(local_IP, gateway, subnet, dns1, dns2);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void startAP() {
  if (apModeActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid.c_str(), apPassword.c_str());
  apModeActive = true;
  notify("AP active: " + apSsid);
}

void stopAP() {
  if (!apModeActive) return;
  WiFi.softAPdisconnect(true);
  apModeActive = false;
  notify("AP disabled");
}

void ensureConnectivity() {
  if (WiFi.status() == WL_CONNECTED) return;
  startAP();
}

void handleRoot() { server.send(200, "text/html", buildHtml()); }

void handleMood() {
  String state = server.arg("state");
  if (!validMood(state)) {
    server.send(400, "text/plain", "Invalid mood");
    return;
  }

  currentMood = state;
  if (state == "wink") {
    winkEndTime = millis() + 330;
    playMelodyByName("happy");
  } else if (state == "love") {
    playMelodyByName("love");
    currentAnimation = "heart_rain";
  } else if (state == "surprised") {
    playMelodyByName("cool");
  } else if (state == "crazy") {
    currentAnimation = "starfield";
    playMelodyByName("boss");
  }

  server.send(200, "text/plain", "Mood set: " + state);
}

void handleAnim() {
  String mode = server.arg("mode");
  if (!validAnim(mode)) {
    server.send(400, "text/plain", "Invalid animation");
    return;
  }
  currentAnimation = mode;
  server.send(200, "text/plain", "Animation: " + mode);
}

void handleLed() {
  String mode = server.arg("mode");
  if (!validLedMode(mode)) {
    server.send(400, "text/plain", "Invalid led mode");
    return;
  }
  ledMode = mode;
  server.send(200, "text/plain", "LED mode: " + mode);
}

void handleLedEnable() {
  String value = server.arg("value");
  if (value == "on") {
    ledEnabled = true;
    server.send(200, "text/plain", "LED enabled");
  } else if (value == "off") {
    ledEnabled = false;
    setLedOff();
    server.send(200, "text/plain", "LED disabled");
  } else {
    server.send(400, "text/plain", "Invalid value");
  }
}

void handleDisplayText() {
  if (server.hasArg("clear") && server.arg("clear") == "1") {
    overlayMessage = "";
    overlayMessageUntilMs = 0;
    server.send(200, "text/plain", "Text cleared");
    return;
  }

  String msg = server.arg("msg");
  if (msg.length() == 0) {
    server.send(400, "text/plain", "No message");
    return;
  }
  notify(msg, 10000);
  server.send(200, "text/plain", "Text displayed");
}

void handleClockToggle() {
  String show = server.arg("show");
  if (show == "on") showClock = true;
  else if (show == "off") showClock = false;
  else {
    server.send(400, "text/plain", "Invalid value");
    return;
  }
  server.send(200, "text/plain", String("Clock ") + (showClock ? "ON" : "OFF"));
}

void handleClockSeconds() {
  String show = server.arg("show");
  if (show == "on") showSeconds = true;
  else if (show == "off") showSeconds = false;
  else {
    server.send(400, "text/plain", "Invalid value");
    return;
  }
  server.send(200, "text/plain", String("Clock seconds ") + (showSeconds ? "ON" : "OFF"));
}

void handleClockDate() {
  String show = server.arg("show");
  if (show == "on") showDate = true;
  else if (show == "off") showDate = false;
  else {
    server.send(400, "text/plain", "Invalid value");
    return;
  }
  server.send(200, "text/plain", String("Clock date ") + (showDate ? "ON" : "OFF"));
}

void handleTimeSync() {
  syncTimeIran();
  server.send(200, "text/plain", "Time sync requested");
}

void handleWifiConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID required");
    return;
  }

  bool ok = connectStation(ssid, pass, 16000);
  if (ok) {
    wifiSsid = ssid;
    wifiPassword = pass;
    notify("WiFi OK: " + ssid);
    server.send(200, "text/plain", "Connected: " + ssid + " IP: " + WiFi.localIP().toString());
  } else {
    startAP();
    notify("WiFi fail -> AP mode");
    server.send(500, "text/plain", "WiFi failed. AP active: " + apSsid);
  }
}

void handleWifiScan() {
  int n = WiFi.scanNetworks();
  String json = "{\"count\":" + String(max(0, n)) + ",\"ssids\":[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "\"" + WiFi.SSID(i) + "\"";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleApStart() {
  startAP();
  server.send(200, "text/plain", "AP started: " + apSsid + " IP: " + WiFi.softAPIP().toString());
}

void handleApStop() {
  stopAP();
  server.send(200, "text/plain", "AP stopped");
}

void handleState() {
  String json = "{";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"apMode\":" + String(apModeActive ? "true" : "false") + ",";
  json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"ssid\":\"" + wifiSsid + "\",";
  json += "\"mood\":\"" + currentMood + "\",";
  json += "\"animation\":\"" + currentAnimation + "\",";
  json += "\"clock\":" + String(showClock ? "true" : "false") + ",";
  json += "\"clockHealthy\":" + String(clockHealthy ? "true" : "false") + ",";
  json += "\"time\":\"" + nowTimeString() + "\",";
  json += "\"date\":\"" + nowDateString() + "\",";
  json += "\"ledMode\":\"" + ledMode + "\",";
  json += "\"ledEnabled\":" + String(ledEnabled ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void connectInitialWiFi() {
  bool ok = connectStation(wifiSsid, wifiPassword, 14000);
  if (ok) {
    notify("WiFi connected: " + wifiSsid);
  } else {
    notify("WiFi failed, AP fallback");
    startAP();
  }
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/mood", HTTP_GET, handleMood);
  server.on("/anim", HTTP_GET, handleAnim);
  server.on("/led", HTTP_GET, handleLed);
  server.on("/led/enable", HTTP_GET, handleLedEnable);
  server.on("/display/text", HTTP_GET, handleDisplayText);
  server.on("/clock", HTTP_GET, handleClockToggle);
  server.on("/clock/seconds", HTTP_GET, handleClockSeconds);
  server.on("/clock/date", HTTP_GET, handleClockDate);
  server.on("/time/sync", HTTP_GET, handleTimeSync);
  server.on("/wifi/connect", HTTP_GET, handleWifiConnect);
  server.on("/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/wifi/ap/start", HTTP_GET, handleApStart);
  server.on("/wifi/ap/stop", HTTP_GET, handleApStop);
  server.on("/api/state", HTTP_GET, handleState);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(A0));

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  analogWriteRange(1023);
  noTone(BUZZER_PIN);
  setLedOff();

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true) {
      delay(100);
    }
  }

  showLoadingScreen();

  if (useStaticIp) WiFi.config(local_IP, gateway, subnet, dns1, dns2);
  connectInitialWiFi();
  syncTimeIran();
  clockHealthy = waitForTimeSync(14000);
  if (!clockHealthy) notify("Clock sync pending...");
  setupServer();

  queueRandomBlink();
  queueRandomMove();
}

void loop() {
  server.handleClient();
  updateEyes();
  updateBuzzer();
  updateLed();

  ensureConnectivity();

  if (!clockHealthy && millis() - lastTimeSyncMs > 20000UL) syncTimeIran();
  if (millis() - lastTimeSyncMs > AUTO_SYNC_INTERVAL_MS) syncTimeIran();
}
