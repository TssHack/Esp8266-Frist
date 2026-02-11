#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// -------------------- Display wiring --------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI D7
#define OLED_CLK D5
#define OLED_DC D4
#define OLED_CS D8
#define OLED_RST D3

// -------------------- Buzzer wiring --------------------
#define BUZZER_PIN D2

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RST, OLED_CS);
ESP8266WebServer server(80);

// -------------------- Wi-Fi --------------------
const char *ssid = "EHSAN-2.4G";
const char *password = "Ehsan1386@";
IPAddress local_IP(192, 168, 1, 4);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// -------------------- Eye geometry --------------------
constexpr int EYE_RADIUS = 16;
constexpr int PUPIL_RADIUS = 7;
constexpr int EYE_SEPARATION = 30;
constexpr int EYE_Y = 32;

int leftEyeX = (SCREEN_WIDTH / 2) - EYE_SEPARATION;
int rightEyeX = (SCREEN_WIDTH / 2) + EYE_SEPARATION;

String currentMood = "normal";
int lookDirectionX = 0;
int lookDirectionY = 0;

bool isBlinking = false;
unsigned long blinkEndTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long nextBlinkDelay = 3200;

unsigned long lastMoveTime = 0;
unsigned long nextMoveDelay = 2000;

unsigned long winkEndTime = 0;
bool autoMoveEnabled = true;

// -------------------- Buzzer engine --------------------
struct MelodyNote {
  int frequency;
  int durationMs;
  int gapMs;
};

constexpr MelodyNote HAPPY_JINGLE[] = {
    {523, 90, 20}, {659, 90, 20}, {784, 120, 35}, {1046, 130, 50}, {784, 120, 35}};
constexpr MelodyNote ALERT_JINGLE[] = {
    {880, 120, 30}, {440, 120, 30}, {880, 120, 30}, {440, 150, 60}};
constexpr MelodyNote POWER_JINGLE[] = {
    {392, 90, 15}, {523, 110, 20}, {659, 120, 25}, {784, 140, 40}};

const MelodyNote *activeMelody = nullptr;
size_t activeMelodyLength = 0;
size_t melodyIndex = 0;
bool melodyPlayingTone = false;
unsigned long melodyStepTime = 0;

void playMelody(const MelodyNote *melody, size_t length) {
  activeMelody = melody;
  activeMelodyLength = length;
  melodyIndex = 0;
  melodyPlayingTone = false;
  melodyStepTime = 0;
}

void updateBuzzer() {
  if (!activeMelody || activeMelodyLength == 0) {
    return;
  }

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

void drawBrandBadge() {
  display.fillRoundRect(2, 2, 46, 13, 3, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(5, 5);
  display.print("Ehsan");
  display.setTextColor(SSD1306_WHITE);
}

void drawEyes(int pupilOffsetX, int pupilOffsetY, bool blink = false) {
  display.clearDisplay();

  int leftPupilX = leftEyeX + pupilOffsetX;
  int leftPupilY = EYE_Y + pupilOffsetY;
  int rightPupilX = rightEyeX + pupilOffsetX;
  int rightPupilY = EYE_Y + pupilOffsetY;

  drawBrandBadge();

  // eye globe
  display.fillCircle(leftEyeX, EYE_Y, EYE_RADIUS, SSD1306_WHITE);
  display.fillCircle(rightEyeX, EYE_Y, EYE_RADIUS, SSD1306_WHITE);

  if (blink) {
    display.drawFastHLine(leftEyeX - EYE_RADIUS + 2, EYE_Y, (EYE_RADIUS - 2) * 2, SSD1306_BLACK);
    display.drawFastHLine(rightEyeX - EYE_RADIUS + 2, EYE_Y, (EYE_RADIUS - 2) * 2, SSD1306_BLACK);
  } else {
    display.fillCircle(leftPupilX, leftPupilY, PUPIL_RADIUS, SSD1306_BLACK);
    display.fillCircle(rightPupilX, rightPupilY, PUPIL_RADIUS, SSD1306_BLACK);
    display.fillCircle(leftPupilX - 2, leftPupilY - 2, 2, SSD1306_WHITE);
    display.fillCircle(rightPupilX - 2, rightPupilY - 2, 2, SSD1306_WHITE);
  }

  // mood overlays
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
  }

  display.setTextSize(1);
  display.setCursor(55, 2);
  display.print("Mood:");
  display.print(currentMood);

  display.display();
}

void queueRandomBlink() {
  nextBlinkDelay = 2800 + random(2600);
}

void queueRandomMove() {
  nextMoveDelay = 1700 + random(2600);
}

void updateEyes() {
  unsigned long now = millis();

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

  if (isBlinking && now >= blinkEndTime) {
    isBlinking = false;
  }

  if (autoMoveEnabled && now - lastMoveTime > nextMoveDelay) {
    lookDirectionX = random(-5, 6);
    lookDirectionY = random(-5, 6);
    lastMoveTime = now;
    queueRandomMove();
  }

  drawEyes(lookDirectionX, lookDirectionY, isBlinking);
}

void showLoadingScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(22, 10);
  display.println("EHsan Fazli Lab");
  display.setTextSize(2);
  display.setCursor(10, 24);
  display.println("EYE CORE");

  for (int i = 0; i <= SCREEN_WIDTH; i += 4) {
    display.drawRoundRect(0, 52, SCREEN_WIDTH, 9, 4, SSD1306_WHITE);
    display.fillRoundRect(1, 53, i - 2, 7, 3, SSD1306_WHITE);
    display.display();
    delay(18);
  }
  playMelody(POWER_JINGLE, sizeof(POWER_JINGLE) / sizeof(POWER_JINGLE[0]));
}

String buildHtml() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Ehsan Fazli - Eye Controller</title>
<style>
:root{
  --bg:#0f172a;--card:#1e293b;--main:#22c55e;--accent:#38bdf8;--txt:#e2e8f0;
}
*{box-sizing:border-box}
body{font-family:tahoma,sans-serif;background:radial-gradient(circle at top,#1d4ed8,#0f172a 50%);color:var(--txt);margin:0;padding:24px;text-align:center}
.container{max-width:900px;margin:auto}
.title{font-size:30px;font-weight:900;letter-spacing:1px}
.subtitle{opacity:.8;margin-top:6px}
.card{background:rgba(30,41,59,.92);border:1px solid rgba(56,189,248,.25);border-radius:16px;padding:16px;margin-top:16px;box-shadow:0 20px 60px rgba(0,0,0,.35)}
h3{margin-top:0;color:#7dd3fc}
.grid{display:flex;flex-wrap:wrap;justify-content:center;gap:8px}
button{background:linear-gradient(135deg,var(--main),#16a34a);color:#fff;border:none;padding:10px 16px;border-radius:12px;cursor:pointer;font-size:15px;font-weight:700;min-width:110px}
button.alt{background:linear-gradient(135deg,var(--accent),#0284c7)}
button.warn{background:linear-gradient(135deg,#f97316,#ea580c)}
button:hover{transform:translateY(-2px)}
#status{margin-top:18px;font-size:17px;color:#fde047;min-height:28px}
.footer{margin-top:18px;opacity:.7}
.badge{display:inline-block;padding:4px 10px;border-radius:999px;background:#0ea5e9;color:#082f49;font-weight:bold}
</style>
</head>
<body>
<div class="container">
  <div class="title">پنل پیشرفته چشم OLED</div>
  <div class="subtitle">طراحی شده توسط <span class="badge">Ehsan Fazli</span></div>

  <div class="card">
    <h3>حالت چشم</h3>
    <div class="grid">
      <button onclick="setMood('normal')">عادی</button>
      <button onclick="setMood('happy')">شاد</button>
      <button onclick="setMood('sad')">غمگین</button>
      <button onclick="setMood('angry')">عصبانی</button>
      <button onclick="setMood('robot')">ربات</button>
      <button onclick="setMood('wink')">چشمک</button>
    </div>
  </div>

  <div class="card">
    <h3>جهت نگاه</h3>
    <div class="grid">
      <button class="alt" onclick="look('up')">بالا</button>
      <button class="alt" onclick="look('down')">پایین</button>
      <button class="alt" onclick="look('left')">چپ</button>
      <button class="alt" onclick="look('right')">راست</button>
      <button class="alt" onclick="look('center')">مرکز</button>
      <button class="warn" onclick="toggleAuto()">روشن/خاموش حرکت خودکار</button>
    </div>
  </div>

  <div class="card">
    <h3>افکت صوتی با بازر D2</h3>
    <div class="grid">
      <button onclick="blinkNow()">پلک فوری</button>
      <button onclick="buzzer('happy')">ملودی شادی</button>
      <button onclick="buzzer('alert')">هشدار</button>
      <button onclick="buzzer('startup')">شروع سیستم</button>
    </div>
  </div>

  <div id="status">سیستم آماده است...</div>
  <div class="footer">ESP8266 Smart Eyes • Web + OLED + Buzzer</div>
</div>
<script>
function request(path,msg){
 fetch(path).then(r=>r.text()).then(t=>{document.getElementById('status').innerText=msg+" | "+t;});
}
function setMood(mood){request('/mood?state='+mood,'حالت تغییر کرد: '+mood)}
function look(direction){request('/look?dir='+direction,'جهت نگاه: '+direction)}
function blinkNow(){request('/blink','پلک انجام شد')}
function buzzer(kind){request('/buzzer?kind='+kind,'افکت صوتی: '+kind)}
function toggleAuto(){request('/auto/toggle','حرکت خودکار تغییر کرد')}
</script>
</body>
</html>
)rawliteral";
  return html;
}

void handleRoot() { server.send(200, "text/html", buildHtml()); }

void handleMood() {
  String state = server.arg("state");
  if (state == "happy" || state == "sad" || state == "angry" || state == "normal" || state == "wink" || state == "robot") {
    currentMood = state;
    if (state == "wink") {
      winkEndTime = millis() + 300;
      playMelody(HAPPY_JINGLE, sizeof(HAPPY_JINGLE) / sizeof(HAPPY_JINGLE[0]));
    }
    server.send(200, "text/plain", "Mood set: " + state);
  } else {
    server.send(400, "text/plain", "Invalid mood");
  }
}

void handleLook() {
  String dir = server.arg("dir");
  if (dir == "up") {
    lookDirectionX = 0;
    lookDirectionY = -5;
  } else if (dir == "down") {
    lookDirectionX = 0;
    lookDirectionY = 5;
  } else if (dir == "left") {
    lookDirectionX = -5;
    lookDirectionY = 0;
  } else if (dir == "right") {
    lookDirectionX = 5;
    lookDirectionY = 0;
  } else if (dir == "center") {
    lookDirectionX = 0;
    lookDirectionY = 0;
  } else {
    server.send(400, "text/plain", "Invalid direction");
    return;
  }

  autoMoveEnabled = false;
  server.send(200, "text/plain", "Looking " + dir + " (auto OFF)");
}

void handleBlink() {
  isBlinking = true;
  blinkEndTime = millis() + 150;
  playMelody(HAPPY_JINGLE, sizeof(HAPPY_JINGLE) / sizeof(HAPPY_JINGLE[0]));
  server.send(200, "text/plain", "Blinked");
}

void handleBuzzer() {
  String kind = server.arg("kind");
  if (kind == "happy") {
    playMelody(HAPPY_JINGLE, sizeof(HAPPY_JINGLE) / sizeof(HAPPY_JINGLE[0]));
  } else if (kind == "alert") {
    playMelody(ALERT_JINGLE, sizeof(ALERT_JINGLE) / sizeof(ALERT_JINGLE[0]));
  } else if (kind == "startup") {
    playMelody(POWER_JINGLE, sizeof(POWER_JINGLE) / sizeof(POWER_JINGLE[0]));
  } else {
    server.send(400, "text/plain", "Invalid melody");
    return;
  }
  server.send(200, "text/plain", "Buzzer: " + kind);
}

void handleAutoToggle() {
  autoMoveEnabled = !autoMoveEnabled;
  server.send(200, "text/plain", String("Auto movement ") + (autoMoveEnabled ? "ON" : "OFF"));
}

void handleState() {
  String json = "{";
  json += "\"mood\":\"" + currentMood + "\",";
  json += "\"lookX\":" + String(lookDirectionX) + ",";
  json += "\"lookY\":" + String(lookDirectionY) + ",";
  json += "\"auto\":" + String(autoMoveEnabled ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void connectWiFi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);

  unsigned long startWait = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(350);
    Serial.print('.');
    if (millis() - startWait > 20000) {
      Serial.println("\nWiFi timeout, rebooting...");
      ESP.restart();
    }
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(A0));

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true) {
      delay(100);
    }
  }

  showLoadingScreen();
  connectWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/mood", HTTP_GET, handleMood);
  server.on("/look", HTTP_GET, handleLook);
  server.on("/blink", HTTP_GET, handleBlink);
  server.on("/buzzer", HTTP_GET, handleBuzzer);
  server.on("/auto/toggle", HTTP_GET, handleAutoToggle);
  server.on("/api/state", HTTP_GET, handleState);

  server.begin();
  Serial.println("HTTP server started");

  queueRandomBlink();
  queueRandomMove();
}

void loop() {
  server.handleClient();
  updateEyes();
  updateBuzzer();
}
