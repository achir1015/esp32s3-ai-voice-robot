// ============================================================================
//  ESP32-S3 AI 語音視覺聊天機器人「小柯」（OpenAI 版）
//  硬體：Goouuu ESP32-S3-CAM (N16R8) + 2.8" ST7789 TFT + INMP441 + MAX98357A + 按鍵
//  流程：直接說話（聲控自動偵測，或按住按鍵）→ OpenAI 語音轉文字 → GPT 回答
//        （說「拍照/你看到什麼」會附上鏡頭畫面）→ OpenAI TTS 播放
//  螢幕：上方時間列 + 可愛表情（眨眼、東張西望、聆聽/思考/說話/想睡）
//  開發環境：Arduino IDE 2.x + esp32 by Espressif 3.x（設定見 README.md）
// ============================================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <algorithm>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ILI9341.h>
#include <time.h>
#include <vector>
#include <initializer_list>
#include "esp_camera.h"
#include "img_converters.h"
#include "qrcode.h"
#include "mbedtls/base64.h"
#if __has_include("config.h")
#include "config.h"          // 自己的設定（不上傳 GitHub）
#else
#include "config.example.h"  // 範本：Wi-Fi 與 API Key 可開機後用網頁設定
#endif
#include "cjk_font.h"
#include "lunar_table.h"
#include "web_page.h"

// ------------------------------------------------------------------ 顏色 ----
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_CYAN    0x07FF
#define C_YELLOW  0xFFE0
#define C_ORANGE  0xFD20
#define C_GRAY    0x8410
#define C_BG      0x0000
#define C_BAR_LINE 0x2945     // 時間列分隔線
#define C_HELMET   0x44BF     // 頭盔：亮藍
#define C_HELMET_D 0x2B37     // 頭盔陰影：深藍
#define C_EAR      0x8D7F     // 耳朵：淺藍
#define C_EAR_D    0x2B37
#define C_VISOR    0x10A6     // 面罩：深藍
#define C_GLOW     0x3FFF     // 發光表情：青色
#define C_GLOW_DIM 0x1C71
#define C_RED2     0xF8C7     // 生氣／愛心
#define C_PINK     0xE372     // 腮紅
#define C_TONGUE   0xFB2C     // 舌頭

#define SCREEN_W 320
#define SCREEN_H 240
#define BAR_H    36           // 上方時間列高度
#define LINE_H   18           // 中文字行高

// ------------------------------------------------------------------ 螢幕 ----
SPIClass tftSPI(HSPI);
#if TFT_IS_ILI9341
Adafruit_ILI9341 tft(&tftSPI, TFT_DC, TFT_CS, TFT_RST);
#else
Adafruit_ST7789 tft(&tftSPI, TFT_CS, TFT_DC, TFT_RST);
#endif
GFXcanvas16 *cv = nullptr;    // 整個畫面先畫在 PSRAM，再一次送出，不會閃爍

// 16px 繁中點陣字（GNU Unifont，見 cjk_font.h），y 為基線，透明背景
class CjkText {
 public:
  Adafruit_GFX *g = &tft;
  void setTarget(Adafruit_GFX *t) { g = t; }
  void setForegroundColor(uint16_t c) { fg = c; }

  int getUTF8Width(const char *s) {
    int w = 0;
    while (*s) w += glyphWidth(nextCodepoint(s));
    return w;
  }

  void drawUTF8(int x, int y, const char *s) {
    y -= 14;
    while (*s) {
      uint32_t cp = nextCodepoint(s);
      int idx = findGlyph(cp);
      int w = glyphWidth(cp);
      if (x + w > g->width()) break;
      if (idx >= 0) {
        const uint8_t *b = &cjkBitmaps[idx * 32];
        for (int r = 0; r < 16; r++) {
          uint16_t bits = (b[r * 2] << 8) | b[r * 2 + 1];
          for (int c = 0; c < w; c++)
            if (bits & (0x8000 >> c)) g->drawPixel(x + c, y + r, fg);
        }
      }
      x += w;
    }
  }

  // UTF-8 自動換行：英文單字/網址/數字不拆開，標點符號不放在行首（允許超出 16px）
  std::vector<String> wrap(const String &s, int w) {
    static const char *NO_START = "，。、；：！？」』）》〉…,.!?;:)]";
    std::vector<String> lines;
    String line; int lineW = 0;
    size_t i = 0;
    while (i < s.length()) {
      uint8_t c = s[i];
      if (c == '\r') { i++; continue; }
      if (c == '\n') { lines.push_back(line); line = ""; lineW = 0; i++; continue; }
      String tok;
      if (c < 0x80 && c != ' ') {                     // 連續英數字當成一個字
        size_t j = i;
        while (j < s.length() && (uint8_t)s[j] < 0x80 && s[j] != ' ' && s[j] != '\n') j++;
        tok = s.substring(i, j); i = j;
      } else {
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        tok = s.substring(i, i + len); i += len;
      }
      int tw = getUTF8Width(tok.c_str());
      bool noStart = strstr(NO_START, tok.c_str()) != nullptr;
      if (lineW > 0 && lineW + tw > w && !(noStart && lineW + tw <= w + 16)) {
        lines.push_back(line); line = ""; lineW = 0;
        if (tok == " ") continue;
      }
      if (tw > w) {                                   // 超長英文：逐字硬切
        for (size_t k = 0; k < tok.length(); k++) {
          if (lineW + 8 > w) { lines.push_back(line); line = ""; lineW = 0; }
          line += tok[k]; lineW += 8;
        }
      } else { line += tok; lineW += tw; }
    }
    if (line.length()) lines.push_back(line);
    return lines;
  }

 private:
  uint16_t fg = 0xFFFF;

  static uint32_t nextCodepoint(const char *&s) {
    uint8_t c = *s++;
    if (c < 0x80) return c;
    int n = (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : 3;
    uint32_t cp = c & (0x3F >> n);
    while (n-- && (*s & 0xC0) == 0x80) cp = (cp << 6) | (*s++ & 0x3F);
    return cp;
  }
  static int findGlyph(uint32_t cp) {
    if (cp > 0xFFFF) return -1;
    int lo = 0, hi = CJK_FONT_COUNT - 1;
    while (lo <= hi) {
      int m = (lo + hi) / 2;
      if (cjkCodes[m] == cp) return m;
      if (cjkCodes[m] < cp) lo = m + 1; else hi = m - 1;
    }
    return -1;
  }
  static int glyphWidth(uint32_t cp) {
    int i = findGlyph(cp);
    if (i < 0) return cp < 0x80 ? 8 : 16;
    return (cjkWide[i / 8] >> (i % 8)) & 1 ? 16 : 8;
  }
};
CjkText u8f;

// ---------------------------------------------------------------- UI 狀態 ----
enum UiState { UI_IDLE, UI_LISTEN, UI_THINK, UI_SPEAK, UI_MSG, UI_HELP, UI_SONG };
volatile UiState uiState = UI_IDLE;
String uiTitle, uiText;
uint32_t uiSince = 0;
bool uiSad = false;
SemaphoreHandle_t uiLock = nullptr;
volatile int micLevel = 0;         // 目前麥克風音量（給 UI 顯示）
volatile int speakLevel = 0;       // 目前播放音量（嘴巴開合）
volatile uint32_t lastInteraction = 0;
uint16_t *photoPix = nullptr;      // 最近一張照片（320x240 RGB565，PSRAM）
volatile bool photoShow = false;   // 對話中顯示照片
volatile uint32_t photoAt = 0;

// ---- 唱歌：YouTube 歌單 ----
struct Song { String id, title; };
std::vector<Song> songs;
SemaphoreHandle_t songLock = nullptr;
uint16_t *thumbPix = nullptr;      // 歌曲封面 320x180 RGB565（PSRAM）
volatile bool thumbOk = false;
uint8_t qrMods[45 * 45];           // QR 碼模組（最多 version 7 = 45x45）
volatile int qrSize = 0;

void setUi(UiState s, const String &title = "", const String &text = "", bool sad = false) {
  xSemaphoreTake(uiLock, portMAX_DELAY);
  uiState = s; uiTitle = title; uiText = text; uiSad = sad; uiSince = millis();
  xSemaphoreGive(uiLock);
}

// ------------------------------------------------------------------ 音訊 ----
I2SClass micI2S;
I2SClass spkI2S;
volatile int volumePct = DEFAULT_VOLUME;

#define TTS_BUF_BYTES (3 * 1024 * 1024)        // 約 65 秒語音
#define TTS_PREBUFFER (TTS_SAMPLE_RATE * 2 / 2)  // 先緩衝 0.5 秒
struct {
  uint8_t *buf = nullptr;
  volatile size_t written = 0;
  volatile size_t played = 0;
  volatile bool downloadDone = true;
  volatile bool playing = false;
} tts;
TaskHandle_t playTaskHandle;

// 錄音：44 bytes WAV header + PCM（PSRAM）
uint8_t *wavBuf = nullptr;
const size_t MAX_SAMPLES = MIC_SAMPLE_RATE * MAX_RECORD_SEC;
#define FRAME       320                        // 20ms @16kHz
#define PREROLL_FR  15                         // 保留觸發前 300ms，避免吃掉第一個字
int16_t preroll[PREROLL_FR][FRAME];
int prerollHead = 0, prerollCount = 0;
float noiseFloor = 150;
float lastRecAvgRms = 0;           // 上一段錄音的平均音量
bool lastRecHitMax = false;        // 上一段錄音是否一路沒停頓錄到上限（多半是音樂/噪音）
const float NOISE_FLOOR_MAX = 3000;

// 聽到的是噪音或音樂：把門檻提高到這個音量，避免同樣的聲音一直觸發（安靜後會自動降回）
void raiseNoiseFloor(float level) {
  float nf = min(NOISE_FLOOR_MAX, max(noiseFloor, level));
  if (nf > noiseFloor) Serial.printf("（噪音 %.0f → 門檻提高到 %d）\n", level, (int)(nf * VAD_RATIO));
  noiseFloor = nf;
}
int32_t dcState = 0;

// ------------------------------------------------------------------ 其它 ----
bool cameraOK = false;
String histRole[MAX_HISTORY_MSGS];
String histText[MAX_HISTORY_MSGS];
int histCount = 0;
const char *OPENAI_HOST = "https://api.openai.com";

// ---- 可由網頁修改的設定（存在 NVS；沒存過就用 config.h 的值）----
struct Settings { String ssid, pass, apiKey, gdrive, playlist; } cfg;
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
volatile bool portalMode = false;
char ipText[24] = "";
#define AP_SSID "XiaoKe-Setup"
#define AP_PASS "xiaoke123"

String cleanDefault(const char *v, const char *placeholder) {
  return strstr(v, placeholder) ? String("") : String(v);
}
void loadSettings() {
  prefs.begin("robot", true);
  cfg.ssid   = prefs.isKey("ssid")   ? prefs.getString("ssid")   : cleanDefault(WIFI_SSID, "你的WiFi");
  cfg.pass   = prefs.isKey("pass")   ? prefs.getString("pass")   : cleanDefault(WIFI_PASSWORD, "你的WiFi");
  cfg.apiKey = prefs.isKey("apikey") ? prefs.getString("apikey") : cleanDefault(OPENAI_API_KEY, "請填入");
  cfg.gdrive = prefs.isKey("gdrive") ? prefs.getString("gdrive") : String(GDRIVE_SCRIPT_URL);
  cfg.playlist = prefs.isKey("playlist") ? prefs.getString("playlist") : String(YT_PLAYLIST_URL);
  prefs.end();
}
String authHeader() { return "Bearer " + cfg.apiKey; }

// ============================================================================
//  按鍵 / 旋鈕
// ============================================================================
bool buttonPressed() {
  int v = digitalRead(PIN_BUTTON);
  return BUTTON_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

void updateVolumeFromPot() {
#if PIN_POT >= 0
  static int last = -100;
  int v = map(analogRead(PIN_POT), 0, 4095, 0, 100);
  if (abs(v - last) > 3) { last = v; volumePct = v; }
#endif
}

// ============================================================================
//  畫面：機器人頭盔 + 發光表情
// ============================================================================
enum Expr { EX_NORMAL, EX_HAPPY, EX_WINK, EX_LOVE, EX_SURPRISE, EX_SAD, EX_ANGRY, EX_SHY,
            EX_BORED, EX_SLEEPY, EX_LISTEN, EX_THINK, EX_DEAD };
volatile int speakExpr = EX_HAPPY;      // GPT 回覆帶的心情，說話時顯示

// 粗線
void thickLine(int x0, int y0, int x1, int y1, int t, uint16_t c) {
  for (int k = -t / 2; k <= t / 2; k++) {
    cv->drawLine(x0 + k, y0, x1 + k, y1, c);
    cv->drawLine(x0, y0 + k, x1, y1 + k, c);
  }
}
// 半圓環：up = ∩（開心眼），否則 ∪（想睡眼）
void halfRing(int x, int y, int r, int t, bool up, uint16_t c) {
  cv->fillCircle(x, y, r, c);
  cv->fillCircle(x, y, max(0, r - t), C_VISOR);
  if (up) cv->fillRect(x - r - 1, y + 1, 2 * r + 3, r + 1, C_VISOR);
  else    cv->fillRect(x - r - 1, y - r - 1, 2 * r + 3, r + 1, C_VISOR);
}
void heart(int x, int y, int r, uint16_t c) {
  int h = r * 55 / 100;
  cv->fillCircle(x - r / 2, y, h, c);
  cv->fillCircle(x + r / 2, y, h, c);
  cv->fillTriangle(x - r - 1, y + r / 6, x + r + 1, y + r / 6, x, y + r * 6 / 5, c);
}

// vx,vy = 面罩中心；s = 縮放；lx,ly = 眼神方向；mouthOpen = 說話張嘴程度（0~1）
void drawRobot(int vx, int vy, float s, int ex, float lx, float ly, bool blink,
               float mouthOpen, uint32_t now, uint16_t antennaColor, bool signal) {
  auto S = [&](float v) { return (int)(v * s + 0.5f); };
  uint16_t glow = ex == EX_ANGRY ? C_RED2 : C_GLOW;

  // ---- 天線 ----
  int helmetTop = vy + S(2) - S(93);
  int bx = vx, by = helmetTop - S(14);
  thickLine(vx, helmetTop, bx, by, max(1, S(3)), C_HELMET_D);
  if (signal)                                   // 收發訊號的弧線
    for (int i = 0; i < 3; i++) {
      int r = S(10 + i * 7) + (int)((now / 90) % 7) * s;
      cv->drawCircleHelper(bx, by, r, 1 | 2, i == 0 ? antennaColor : C_GLOW_DIM);
    }
  cv->fillCircle(bx, by, S(6), antennaColor);
  cv->fillCircle(bx - S(2), by - S(2), max(1, S(2)), C_WHITE);

  // ---- 耳朵 ----
  for (int side = -1; side <= 1; side += 2) {
    int x = vx + side * S(128) - S(12);
    cv->fillRoundRect(x, vy - S(37), S(24), S(74), S(10), C_EAR);
    cv->fillRoundRect(x + S(6), vy - S(22), S(12), S(44), S(5), C_EAR_D);
  }

  // ---- 頭盔 + 面罩 ----
  cv->fillRoundRect(vx - S(125), vy + S(2) - S(93), S(250), S(186), S(78), C_HELMET_D);
  cv->fillRoundRect(vx - S(123), vy - S(93), S(246), S(182), S(76), C_HELMET);
  cv->fillRoundRect(vx - S(78), vy - S(84), S(34), S(7), S(3), C_WHITE);          // 反光
  cv->fillRoundRect(vx - S(104), vy - S(72), S(208), S(144), S(50), C_HELMET_D);
  cv->fillRoundRect(vx - S(101), vy - S(69), S(202), S(138), S(47), C_VISOR);

  int ey = vy - S(14), dx = S(46), my = vy + S(34);
  bool talking = mouthOpen > 0.05f;
  bool smiley = ex == EX_NORMAL || ex == EX_HAPPY || ex == EX_WINK || ex == EX_LOVE || ex == EX_SHY;

  // ---- 嘴巴（先畫，避免蓋到眼睛）----
  if (talking) {
    int r = S(9 + 9 * mouthOpen);
    cv->fillCircle(vx, my - S(6), r, glow);                         // 下半圓的 D 形嘴
    cv->fillRect(vx - r - 1, my - S(6) - r - 1, 2 * r + 3, r + 1, C_VISOR);
    cv->fillRect(vx - r, my - S(6) - max(1, S(2)), 2 * r + 1, max(2, S(3)), glow);
    if (r > S(6)) cv->fillCircle(vx, my - S(6) + r * 2 / 3, r / 3, C_TONGUE);
  } else if (smiley) {
    int r = S(18);
    cv->fillCircle(vx, my - S(10), r, glow);
    cv->fillCircle(vx, my - S(15), r, C_VISOR);
  } else if (ex == EX_SURPRISE || ex == EX_LISTEN) {
    int r = S(ex == EX_SURPRISE ? 10 : 8) + (ex == EX_LISTEN ? (int)(micLevel / 800) : 0);
    cv->fillCircle(vx, my - S(4), r, glow);
    cv->fillCircle(vx, my - S(4), max(1, r - S(4)), C_VISOR);
  } else if (ex == EX_SAD || ex == EX_ANGRY) {
    int r = S(16);
    cv->fillCircle(vx, my + S(8), r, glow);
    cv->fillCircle(vx, my + S(13), r, C_VISOR);
  } else if (ex == EX_THINK) {
    for (int i = 0; i < 3; i++)
      cv->fillCircle(vx + (i - 1) * S(14), my - S(4), S(4), (now / 250) % 3 == (unsigned)i ? glow : C_GLOW_DIM);
  } else if (ex == EX_SLEEPY) {
    cv->fillCircle(vx, my - S(4), S(4), glow);
  } else {                                                          // BORED / DEAD
    cv->fillRoundRect(vx - S(15), my - S(5), S(30), max(2, S(4)), S(2), glow);
  }

  // ---- 酒窩（笑的時候出現，會輕輕跳動）----
  if (smiley || talking) {
    float p = 0.75f + 0.25f * sinf(now / 180.0f);
    for (int side = -1; side <= 1; side += 2)
      cv->fillCircle(vx + side * S(30), my - S(10), max(1, (int)(S(3) * p)), C_GLOW_DIM);
  }

  // ---- 腮紅 ----
  int blushW = S(ex == EX_SHY || ex == EX_LOVE ? 30 : 24);
  if (ex != EX_DEAD && ex != EX_ANGRY)
    for (int side = -1; side <= 1; side += 2)
      cv->fillRoundRect(vx + side * S(70) - blushW / 2, vy + S(14), blushW, S(10), S(5), C_PINK);

  // ---- 眼睛 ----
  int ew = S(24), eh = S(32);
  for (int side = -1; side <= 1; side += 2) {
    int x = vx + side * dx + (int)lx, y = ey + (int)ly;
    switch (ex) {
      case EX_HAPPY: case EX_SHY:
        halfRing(vx + side * dx, ey + S(6), S(14), max(2, S(5)), true, glow); break;
      case EX_WINK:
        if (side > 0) halfRing(vx + side * dx, ey + S(6), S(13), max(2, S(5)), true, glow);
        else { cv->fillRoundRect(x - ew / 2, y - eh / 2, ew, eh, ew / 2, glow);
               cv->fillCircle(x - S(4), y - S(7), max(1, S(4)), C_WHITE); }
        break;
      case EX_LOVE:
        heart(vx + side * dx, ey, S(15) + (int)(S(3) * sinf(now / 150.0f)), C_RED2); break;
      case EX_SURPRISE:
        cv->fillCircle(vx + side * dx, ey, S(15), glow);
        cv->fillCircle(vx + side * dx - S(5), ey - S(5), max(1, S(4)), C_WHITE); break;
      case EX_BORED:
        cv->fillRoundRect(vx + side * dx - S(14), ey, S(28), max(2, S(5)), S(2), glow); break;
      case EX_SLEEPY:
        halfRing(vx + side * dx, ey - S(2), S(13), max(2, S(4)), false, glow); break;
      case EX_DEAD:
        thickLine(vx + side * dx - S(10), ey - S(10), vx + side * dx + S(10), ey + S(10), max(2, S(4)), glow);
        thickLine(vx + side * dx - S(10), ey + S(10), vx + side * dx + S(10), ey - S(10), max(2, S(4)), glow); break;
      case EX_THINK:
        cv->fillCircle(vx + side * dx + S(6), ey - S(6), S(9), glow); break;
      default: {   // NORMAL / LISTEN / SAD / ANGRY
        int w = ex == EX_LISTEN ? S(28) : ew, h = ex == EX_LISTEN ? S(38) : eh;
        if (ex == EX_ANGRY) h = S(24);
        if (blink && ex != EX_LISTEN) {
          cv->fillRoundRect(x - w / 2, y - S(2), w, max(2, S(5)), S(2), glow);
        } else {
          cv->fillRoundRect(x - w / 2, y - h / 2, w, h, w / 2, glow);
          cv->fillCircle(x - w / 5, y - h / 4, max(1, S(4)), C_WHITE);
        }
        if (ex == EX_SAD)      // 擔心：眉毛內側上揚
          thickLine(x - S(14), y - h / 2 - S(6) - S(5) * side, x + S(14), y - h / 2 - S(6) + S(5) * side,
                    max(2, S(3)), glow);
        if (ex == EX_ANGRY)    // 生氣：眉毛內側下壓
          thickLine(x - S(14), y - h / 2 - S(8) + S(5) * side, x + S(14), y - h / 2 - S(8) - S(5) * side,
                    max(2, S(4)), glow);
      }
    }
  }

  // ---- 鼻子 ----
  if (ex != EX_DEAD) cv->fillRoundRect(vx - S(4), vy + S(8), S(8), max(2, S(5)), S(2), C_GLOW_DIM);

  // ---- 想睡的 Zzz ----
  if (ex == EX_SLEEPY) {
    int k = (now / 500) % 3;
    cv->setTextColor(C_GLOW);
    cv->setTextSize(max(1, S(2)));
    cv->setCursor(vx + S(62), vy - S(52) - k * S(4));
    cv->print("z");
    cv->setTextSize(max(1, S(3)));
    cv->setCursor(vx + S(74), vy - S(66) - k * S(4));
    cv->print("Z");
  }
}

// ---- 功能說明（按鍵翻頁）----
#define DEVELOPER_CREDIT "此系統是吳玉柱先生與 Claude AI 共同開發，有任何意見請聯絡："
#define DEVELOPER_EMAIL  "achir1015@gmail.com"
struct HelpLine { String text; uint16_t color; };
std::vector<std::vector<HelpLine>> helpPages;
volatile int helpPage = 0;
const int HELP_LINES = 9;

void buildHelp() {
  struct Para { const char *t; uint16_t c; };
  static const Para P[] = {
    {"一、系統功能", C_YELLOW},
    {"1. 聲控聊天：直接說話，停頓一下就自動送出，不用按鍵。", C_WHITE},
    {"2. 拍照辨識：說「拍照」「這是什麼」「上面寫什麼」，" ROBOT_NAME "會拍照回答，照片顯示在螢幕並存到雲端硬碟。", C_WHITE},
    {"3. 音量：說「大聲一點」或「小聲一點」。", C_WHITE},
    {"4. 記憶：記得最近的對話，說「清除記憶」可重新開始。", C_WHITE},
    {"5. 表情：會依心情開心、愛心、驚訝、難過、生氣、眨眼、害羞；太久沒互動會想睡覺。", C_WHITE},
    {"6. 時間：上方顯示日期、星期、農曆與時間，也可以問「今天農曆幾號」。", C_WHITE},
    {"7. 網頁設定：開啟螢幕左下角的網址，可修改 Wi-Fi、密碼、API Key 與雲端硬碟網址。", C_WHITE},
    {"8. 連不上 Wi-Fi 時會開熱點 XiaoKe-Setup（密碼 xiaoke123），手機連上後開 192.168.4.1 設定。", C_WHITE},
    {"9. 唱歌：說「唱首歌」，從" SONG_COMPOSER "的 YouTube 創作歌單隨機選一首，手機掃 QR 碼就能播放。", C_WHITE},
    {"10. 按鍵：短按看本說明（看完最後一頁回到聊天）；環境吵雜時「按住說話，放開送出」。", C_WHITE},
    {"\f", 0},
    {"二、創意開發者", C_YELLOW},
    {"", C_WHITE},
    {DEVELOPER_CREDIT, C_WHITE},
    {DEVELOPER_EMAIL, C_GLOW},
    {"", C_WHITE},
    {"謝謝你陪" ROBOT_NAME "聊天！", C_PINK},
  };
  helpPages.clear();
  std::vector<HelpLine> page;
  for (auto &p : P) {
    if (!strcmp(p.t, "\f")) { if (page.size()) helpPages.push_back(page); page.clear(); continue; }
    std::vector<String> lines = u8f.wrap(p.t, SCREEN_W - 24);
    if (lines.empty()) lines.push_back("");
    if (page.size() && page.size() + lines.size() > (size_t)HELP_LINES && lines.size() <= (size_t)HELP_LINES) {
      helpPages.push_back(page);                       // 同一項目不跨頁
      page.clear();
    }
    for (auto &l : lines) {
      if ((int)page.size() == HELP_LINES) { helpPages.push_back(page); page.clear(); }
      page.push_back({l, p.c});
    }
  }
  if (page.size()) helpPages.push_back(page);
}

void drawHelp(uint32_t now) {
  int total = helpPages.size(), pg = constrain((int)helpPage, 0, total - 1);
  bool last = pg == total - 1;
  u8f.setForegroundColor(C_GLOW);
  u8f.drawUTF8(8, 56, "功能說明");
  char num[12];
  snprintf(num, sizeof(num), "%d / %d", pg + 1, total);
  u8f.setForegroundColor(C_GRAY);
  u8f.drawUTF8(SCREEN_W - 8 - u8f.getUTF8Width(num), 56, num);
  cv->drawFastHLine(8, 62, SCREEN_W - 16, C_BAR_LINE);
  for (size_t i = 0; i < helpPages[pg].size(); i++) {
    u8f.setForegroundColor(helpPages[pg][i].color);
    u8f.drawUTF8(8, 82 + i * 17, helpPages[pg][i].text.c_str());
  }
  if (last) drawRobot(262, 196, 0.22f, (now / 1500) % 2 ? EX_HAPPY : EX_LOVE, 0, 0, false, 0, now, C_GLOW, false);
  const char *foot = last ? "按鍵：回到聊天" : "按鍵：下一頁";
  u8f.setForegroundColor(C_GREEN);
  u8f.drawUTF8((SCREEN_W - u8f.getUTF8Width(foot)) / 2, 236, foot);
}

// ---- 唱歌畫面 ----
void drawNote(int x, int y, uint16_t c) {        // 八分音符
  cv->fillCircle(x, y, 4, c);
  cv->drawFastVLine(x + 3, y - 14, 14, c);
  cv->drawFastVLine(x + 4, y - 14, 14, c);
  cv->drawLine(x + 4, y - 14, x + 9, y - 9, c);
  cv->drawLine(x + 4, y - 13, x + 9, y - 8, c);
}

void drawSong(const String &title, uint32_t now) {
  // 封面（320x180 縮成 208x117）
  const int TX = 4, TY = 42, TW = 208, TH = 117;
  uint16_t *dst = cv->getBuffer();
  if (thumbOk) {
    for (int y = 0; y < TH; y++)
      for (int x = 0; x < TW; x++)
        dst[(TY + y) * SCREEN_W + TX + x] = thumbPix[(y * 180 / TH) * 320 + x * 320 / TW];
  } else {
    cv->fillRect(TX, TY, TW, TH, C_VISOR);
    for (int i = 0; i < 3; i++) drawNote(TX + 70 + i * 30, TY + 70 - (i % 2) * 12, C_GLOW);
  }
  cv->drawRect(TX - 1, TY - 1, TW + 2, TH + 2, C_HELMET);

  // QR 碼（白底黑點，四周留白）
  const int QX = 219, QY = 42, QW = 96;
  cv->fillRect(QX, QY, QW, QW, C_WHITE);
  int n = qrSize;
  if (n > 0) {
    int m = QW / (n + 4), off = (QW - m * n) / 2;
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++)
        if (qrMods[y * n + x]) cv->fillRect(QX + off + x * m, QY + off + y * m, m, m, C_BLACK);
  }
  u8f.setForegroundColor(C_GRAY);
  u8f.drawUTF8(QX + 8, 156, "手機掃描");
  u8f.drawUTF8(QX + 4, 174, "YouTube");

  // 歌名與創作者
  u8f.setForegroundColor(C_ORANGE);
  u8f.drawUTF8(6, 180, "正在唱：");
  std::vector<String> lines = u8f.wrap("《" + title + "》", 200);
  u8f.setForegroundColor(C_WHITE);
  for (size_t i = 0; i < lines.size() && i < 2; i++) u8f.drawUTF8(6, 198 + i * 17, lines[i].c_str());
  u8f.setForegroundColor(C_GLOW_DIM);
  u8f.drawUTF8(84, 180, "詞曲：" SONG_COMPOSER);

  // 唱歌的小柯 + 飄動音符
  float beat = (sinf(now / 160.0f) + 1) / 2;
  float mouth = speakLevel > 0 ? constrain(speakLevel / 6000.0f, 0.0f, 1.0f) : beat;
  drawRobot(268, 212, 0.22f, EX_HAPPY, 0, 0, false, mouth, now, C_ORANGE, false);
  for (int i = 0; i < 3; i++) {
    int ph = (now / 25 + i * 40) % 120;
    drawNote(226 + i * 14 - ph / 12, 206 - ph / 2, i == 1 ? C_PINK : C_GLOW);
  }
  u8f.setForegroundColor(C_GREEN);
  u8f.drawUTF8(80, 236, "按鍵：回到聊天");
}

// ---- 農曆 ----
// 西元日期 → 距 2000-01-01 的天數（Howard Hinnant 演算法）
long daysSince2000(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468 - 10957;
}

// 回傳例如「八月二十」「閏六月初一」；超出表格範圍回傳空字串
String lunarString(const struct tm &t) {
  long d = daysSince2000(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  int yi = -1;
  for (int i = 0; i < LUNAR_YEARS - 1; i++)
    if (d >= LUNAR[i].cny && d < LUNAR[i + 1].cny) { yi = i; break; }
  if (yi < 0) return "";
  long off = d - LUNAR[yi].cny;
  int leap = LUNAR[yi].leap, n = leap ? 13 : 12, month = 1, day = 1;
  bool isLeap = false;
  for (int i = 0; i < n; i++) {
    int len = (LUNAR[yi].bits >> i) & 1 ? 30 : 29;
    if (off < len) {
      if (!leap || i < leap) month = i + 1;
      else if (i == leap) { month = leap; isLeap = true; }
      else month = i;
      day = off + 1;
      break;
    }
    off -= len;
  }
  static const char *MN[] = {"正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "臘"};
  static const char *DG[] = {"一", "二", "三", "四", "五", "六", "七", "八", "九"};
  static const char *TP[] = {"初", "十", "廿"};
  String ds = day == 10 ? "初十" : day == 20 ? "二十" : day == 30 ? "三十" : String(TP[day / 10]) + DG[day % 10 - 1];
  return String(isLeap ? "閏" : "") + MN[month - 1] + "月" + ds;
}

void drawTopBar() {
  struct tm t;
  bool ok = getLocalTime(&t, 0);
  static const char *WD[] = {"週日", "週一", "週二", "週三", "週四", "週五", "週六"};
  char dateStr[8] = "--/--", timeStr[12] = "--:--:--";
  if (ok) {
    strftime(dateStr, sizeof(dateStr), "%m/%d", &t);
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &t);
  }
  cv->fillRect(0, 0, SCREEN_W, BAR_H, C_BG);
  cv->drawFastHLine(0, BAR_H - 1, SCREEN_W, C_BAR_LINE);
  static int lunarDay = -1;
  static String lunar;
  if (ok && t.tm_yday != lunarDay) {            // 一天只算一次
    lunarDay = t.tm_yday;
    String l = lunarString(t);
    lunar = l.startsWith("閏") || l.isEmpty() ? l : "農曆" + l;
  }
  u8f.setForegroundColor(C_GLOW);
  u8f.drawUTF8(4, 16, (String(dateStr) + " " + (ok ? WD[t.tm_wday] : "")).c_str());
  u8f.setForegroundColor(C_ORANGE);
  u8f.drawUTF8(4, 33, ok ? lunar.c_str() : "");
  cv->setTextSize(3);
  cv->setTextColor(C_WHITE);
  cv->setCursor(112, 7);
  cv->print(timeStr);
  char vol[8];
  snprintf(vol, sizeof(vol), "%d%%", (int)volumePct);
  u8f.setForegroundColor(C_YELLOW);
  u8f.drawUTF8(282, 16, "音量");
  u8f.drawUTF8(282, 33, vol);
  cv->fillCircle(268, 10, 4, WiFi.status() == WL_CONNECTED ? C_GREEN : C_RED);   // Wi-Fi 燈號
}

// 麥克風音量條（讓人知道它一直在聽）
void drawMicBars(int x, int y, bool listening) {
  int lvl = constrain(map(micLevel, 0, 3000, 0, 100), 0, 100);
  for (int i = 0; i < 4; i++) {
    int h = 3 + (lvl * (4 - abs(i * 2 - 3)) / 3) * 14 / 100;
    cv->fillRoundRect(x + i * 7, y - h, 5, h, 2, listening ? C_GREEN : C_GRAY);
  }
}

// UI task：約 15 fps 重畫整個畫面
void uiTask(void *) {
  uint32_t nextBlink = millis() + 3000, nextLook = 0, nextIdle = millis() + 8000, idleUntil = 0;
  int idleExpr = EX_NORMAL;
  float lx = 0, ly = 0, tx = 0, ty = 0;
  for (;;) {
    uint32_t now = millis();
    UiState st; String title, text; uint32_t since; bool sad;
    xSemaphoreTake(uiLock, portMAX_DELAY);
    st = uiState; title = uiTitle; text = uiText; since = uiSince; sad = uiSad;
    xSemaphoreGive(uiLock);

    // 眼神：待機時東張西望
    if (now > nextLook) {
      if (st == UI_IDLE) { tx = random(-12, 13); ty = random(-6, 7); }
      else { tx = 0; ty = 0; }
      nextLook = now + random(1200, 3500);
    }
    lx += (tx - lx) * 0.35f; ly += (ty - ly) * 0.35f;

    // 眨眼（偶爾連眨兩下）
    bool blink = false;
    if (now >= nextBlink) {
      uint32_t d = now - nextBlink;
      if (d < 130 || (d > 260 && d < 390 && (nextBlink & 1))) blink = true;
      else if (d >= 400) nextBlink = now + random(2500, 6000);
    }

    // 待機時的隨機小表情
    if (st == UI_IDLE && now > nextIdle) {
      int r = random(100);
      idleExpr = r < 28 ? EX_HAPPY : r < 48 ? EX_WINK : r < 60 ? EX_LOVE : r < 72 ? EX_SURPRISE : r < 86 ? EX_BORED : EX_SHY;
      idleUntil = now + (idleExpr == EX_WINK ? 800 : idleExpr == EX_SURPRISE ? 900 : 1800);
      nextIdle = now + random(6000, 14000);
    }

    bool sleepy = st == UI_IDLE && now - lastInteraction > SLEEPY_AFTER_SEC * 1000UL;
    int ex = EX_NORMAL;
    uint16_t ant = C_GLOW;
    bool signal = false;
    switch (st) {
      case UI_LISTEN: ex = EX_LISTEN; ant = C_GREEN; signal = true; break;
      case UI_THINK:  ex = EX_THINK;  ant = (now / 200) % 2 ? C_YELLOW : C_GLOW_DIM; break;
      case UI_SPEAK:  ex = speakExpr; ant = C_ORANGE; signal = true; break;
      case UI_MSG:    ex = sad ? EX_DEAD : EX_THINK; ant = sad ? C_RED : C_YELLOW; break;
      default:
        ex = sleepy ? EX_SLEEPY : now < idleUntil ? idleExpr : EX_NORMAL;
        ant = sleepy ? C_GLOW_DIM : ((now / 1500) % 2 ? C_GLOW : C_GLOW_DIM);    // 呼吸燈
    }
    float mouth = st == UI_SPEAK ? constrain(speakLevel / 6000.0f, 0.0f, 1.0f) : 0;

    if (st == UI_SONG) {
      cv->fillScreen(C_BG);
      drawSong(title, now);
      drawTopBar();
      tft.drawRGBBitmap(0, 0, cv->getBuffer(), SCREEN_W, SCREEN_H);
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    if (st == UI_HELP && helpPages.size()) {
      cv->fillScreen(C_BG);
      drawTopBar();
      drawHelp(now);
      tft.drawRGBBitmap(0, 0, cv->getBuffer(), SCREEN_W, SCREEN_H);
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

    bool photoFull = photoShow && now - photoAt < PHOTO_SHOW_MS;
    if (photoFull) {                                                // 剛拍的照片：全螢幕
      memcpy(cv->getBuffer(), photoPix, SCREEN_W * SCREEN_H * 2);
      const char *msg = st == UI_THINK ? "拍好了！讓我看看…" : "拍好了！";
      int w = u8f.getUTF8Width(msg);
      cv->fillRoundRect((SCREEN_W - w) / 2 - 8, 208, w + 16, 24, 8, C_BG);
      u8f.setForegroundColor(C_GLOW);
      u8f.drawUTF8((SCREEN_W - w) / 2, 226, msg);
      tft.drawRGBBitmap(0, 0, cv->getBuffer(), SCREEN_W, SCREEN_H);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    cv->fillScreen(C_BG);
    bool compact = (st == UI_THINK || st == UI_SPEAK || st == UI_MSG) && (title.length() || text.length());
    if (!compact) {
      drawRobot(160, 145, 0.95f, ex, lx, ly, blink, mouth, now, ant, signal);
      drawTopBar();
      const char *hint = st == UI_LISTEN ? "聆聽中" : sleepy ? "想睡…" : VOICE_ACTIVATION ? "待命" : "按鍵";
      u8f.setForegroundColor(st == UI_LISTEN ? C_GREEN : C_GRAY);
      u8f.drawUTF8(4, 218, hint);
      u8f.setForegroundColor(C_GLOW_DIM);
      u8f.drawUTF8(4, 236, ipText);                                  // 設定網頁位址
      drawMicBars(290, 236, st == UI_LISTEN);
    } else {
      if (photoShow) {                                              // 回答時左側顯示照片縮圖
        const int TWD = 128, THT = 96, X0 = 4, Y0 = 100;
        uint16_t *dst = cv->getBuffer();
        for (int y = 0; y < THT; y++)
          for (int x = 0; x < TWD; x++)
            dst[(Y0 + y) * SCREEN_W + X0 + x] = photoPix[(y * SCREEN_H / THT) * SCREEN_W + x * SCREEN_W / TWD];
        cv->drawRect(X0 - 1, Y0 - 1, TWD + 2, THT + 2, C_HELMET);
        drawRobot(68, 70, 0.28f, ex, 0, 0, blink, mouth, now, ant, false);
      } else {
        drawRobot(68, 142, 0.5f, ex, lx * 0.5f, ly * 0.5f, blink, mouth, now, ant, signal);
      }
      drawTopBar();
      const int TX = 140, TW = SCREEN_W - TX - 16, PER = 9;
      u8f.setForegroundColor(st == UI_MSG ? (sad ? C_RED : C_YELLOW) : st == UI_SPEAK ? C_ORANGE : C_GLOW);
      u8f.drawUTF8(TX, 58, title.c_str());
      std::vector<String> lines = u8f.wrap(text, TW);
      int pages = max(1, (int)((lines.size() + PER - 1) / PER));
      int page = (int)((now - since) / 4500) % pages;               // 字太多時自動翻頁
      u8f.setForegroundColor(C_WHITE);
      for (int i = 0; i < PER && page * PER + i < (int)lines.size(); i++)
        u8f.drawUTF8(TX, 80 + i * LINE_H, lines[page * PER + i].c_str());
    }

    tft.drawRGBBitmap(0, 0, cv->getBuffer(), SCREEN_W, SCREEN_H);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================================================
//  音訊：錄音 / 播放
// ============================================================================
void writeWavHeader(uint8_t *h, uint32_t dataBytes) {
  uint32_t sr = MIC_SAMPLE_RATE, byteRate = sr * 2, chunk = 36 + dataBytes;
  memcpy(h, "RIFF", 4);      memcpy(h + 4, &chunk, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  uint32_t fmtLen = 16;      memcpy(h + 16, &fmtLen, 4);
  uint16_t fmt = 1, ch = 1;  memcpy(h + 20, &fmt, 2); memcpy(h + 22, &ch, 2);
  memcpy(h + 24, &sr, 4);    memcpy(h + 28, &byteRate, 4);
  uint16_t align = 2, bits = 16; memcpy(h + 32, &align, 2); memcpy(h + 34, &bits, 2);
  memcpy(h + 36, "data", 4); memcpy(h + 40, &dataBytes, 4);
}

// 讀 20ms 麥克風資料，回傳 RMS
int readFrame(int16_t *out) {
  static int32_t raw[FRAME];
  size_t got = micI2S.readBytes((char *)raw, sizeof(raw)) / 4;
  int64_t sum = 0;
  for (size_t i = 0; i < FRAME; i++) {
    int32_t s = i < got ? raw[i] >> MIC_GAIN_SHIFT : 0;
    dcState += (s - dcState) >> 6;              // 簡易去直流
    s = constrain(s - dcState, -32768, 32767);
    out[i] = s;
    sum += (int64_t)s * s;
  }
  return (int)sqrtf((float)(sum / FRAME));
}

void flushMic(int ms) {
  int16_t f[FRAME];
  for (int i = 0; i < ms / 20; i++) readFrame(f);
  prerollCount = 0;
}

int vadThreshold() { return max((int)VAD_MIN_RMS, (int)(noiseFloor * VAD_RATIO)); }

// 錄一句話：byButton = 按鍵模式（放開結束）；否則偵測停頓結束。回傳樣本數（0 = 視為雜音）
size_t recordUtterance(bool byButton) {
  int16_t *pcm = (int16_t *)(wavBuf + 44);
  size_t n = 0;
  // 先放入觸發前的預錄
  for (int k = 0; k < prerollCount; k++) {
    int idx = (prerollHead - prerollCount + k + PREROLL_FR) % PREROLL_FR;
    memcpy(pcm + n, preroll[idx], FRAME * 2);
    n += FRAME;
  }
  prerollCount = 0;

  setUi(UI_LISTEN);
  lastInteraction = millis();
  int thr = vadThreshold();
  int silentMs = 0, voicedMs = 0;
  double rmsSum = 0; int frames = 0;
  lastRecHitMax = true;
  while (n + FRAME <= MAX_SAMPLES) {
    int rms = readFrame(pcm + n);
    n += FRAME;
    micLevel = rms;
    rmsSum += rms; frames++;
    if (byButton) {
      if (!buttonPressed()) break;
      voicedMs += 20;
    } else {
      if (rms > thr * 0.7f) { silentMs = 0; voicedMs += 20; }
      else silentMs += 20;
      if (silentMs >= VAD_SILENCE_MS) { lastRecHitMax = false; break; }
    }
  }
  if (byButton) lastRecHitMax = false;
  lastRecAvgRms = frames ? rmsSum / frames : 0;
  micLevel = 0;
  if (!byButton) {
    // 去掉尾端靜音（保留 200ms）
    size_t trim = (size_t)max(0, silentMs - 200) * MIC_SAMPLE_RATE / 1000;
    n = n > trim ? n - trim : 0;
    if (voicedMs < VAD_MIN_SPEECH_MS) return 0;
  } else if (n < MIC_SAMPLE_RATE / 2) {
    return 0;
  }
  return n;
}

// 播放 task：從 tts.buf 取資料送 I2S，套用音量，並計算嘴巴開合
void playTask(void *) {
  int16_t out[512];
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (!tts.downloadDone && tts.written < TTS_PREBUFFER) vTaskDelay(5);
    for (;;) {
      size_t avail = (tts.written - tts.played) & ~(size_t)1;
      if (avail == 0) {
        if (tts.downloadDone) break;
        vTaskDelay(2);
        continue;
      }
      size_t bytes = min(avail, sizeof(out));
      const int16_t *src = (const int16_t *)(tts.buf + tts.played);
      int vol = volumePct;
      int64_t acc = 0;
      for (size_t i = 0; i < bytes / 2; i++) { out[i] = (int32_t)src[i] * vol / 100; acc += abs(src[i]); }
      speakLevel = (int)(acc / (int64_t)max((size_t)1, bytes / 2));
      spkI2S.write((uint8_t *)out, bytes);
      tts.played += bytes;
    }
    speakLevel = 0;
    memset(out, 0, sizeof(out));             // 補靜音，避免 DMA 殘留造成嗡嗡聲
    for (int i = 0; i < 8; i++) spkI2S.write((uint8_t *)out, sizeof(out));
    tts.playing = false;
  }
}

// 讓 HTTPClient::writeToStream 直接把 TTS 音訊寫進 PSRAM 緩衝
class TtsSink : public Stream {
 public:
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *data, size_t len) override {
    size_t space = TTS_BUF_BYTES - tts.written;
    size_t n = min(len, space);
    memcpy(tts.buf + tts.written, data, n);
    tts.written += n;
    return len;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

void waitPlaybackDone() {
  while (tts.playing) {
    updateVolumeFromPot();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================================================
//  OpenAI API
// ============================================================================
String lastApiError;

String speechToText(size_t samples) {
  uint32_t dataBytes = samples * 2;
  writeWavHeader(wavBuf, dataBytes);
  size_t wavLen = 44 + dataBytes;

  const String B = "----ESP32S3RobotBoundary";
  String head =
      "--" + B + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" STT_MODEL "\r\n" +
      "--" + B + "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nzh\r\n" +
      "--" + B + "\r\nContent-Disposition: form-data; name=\"prompt\"\r\n\r\n請用台灣繁體中文轉寫。\r\n" +
      "--" + B + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
      "Content-Type: audio/wav\r\n\r\n";
  String tail = "\r\n--" + B + "--\r\n";

  size_t total = head.length() + wavLen + tail.length();
  uint8_t *body = (uint8_t *)ps_malloc(total);
  if (!body) { lastApiError = "記憶體不足"; return ""; }
  memcpy(body, head.c_str(), head.length());
  memcpy(body + head.length(), wavBuf, wavLen);
  memcpy(body + head.length() + wavLen, tail.c_str(), tail.length());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(30000);
  http.begin(client, String(OPENAI_HOST) + "/v1/audio/transcriptions");
  http.addHeader("Authorization", authHeader());
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + B);
  int code = http.POST(body, total);
  String resp = http.getString();
  http.end();
  free(body);

  if (code != 200) {
    lastApiError = "STT " + String(code) + " " + resp.substring(0, 120);
    Serial.println(lastApiError);
    return "";
  }
  JsonDocument doc;
  deserializeJson(doc, resp);
  String text = doc["text"] | "";
  text.trim();
  return text;
}

// ---- 照片上傳 Google 雲端硬碟（背景執行，不影響對話）----
struct PhotoUpload { uint8_t *jpg; size_t len; char name[48]; };

void uploadTask(void *arg) {
  PhotoUpload *u = (PhotoUpload *)arg;
  size_t b64Len = 0;
  mbedtls_base64_encode(nullptr, 0, &b64Len, u->jpg, u->len);
  uint8_t *b64 = (uint8_t *)ps_malloc(b64Len + 1);
  if (b64 && mbedtls_base64_encode(b64, b64Len, &b64Len, u->jpg, u->len) == 0) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(30000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);     // Apps Script 會 302 轉址回傳結果
    String url = cfg.gdrive + "?token=" + GDRIVE_TOKEN + "&name=" + u->name;
    http.begin(client, url);
    http.addHeader("Content-Type", "text/plain");
    int code = http.POST(b64, b64Len);
    String resp = http.getString();
    http.end();
    Serial.printf("照片上傳 %s → %d %s\n", u->name, code, resp.substring(0, 60).c_str());
  }
  free(b64);
  free(u->jpg);
  delete u;
  vTaskDelete(nullptr);
}

void queuePhotoUpload(const uint8_t *jpg, size_t len) {
  if (cfg.gdrive.length() == 0) return;
  PhotoUpload *u = new PhotoUpload;
  u->jpg = (uint8_t *)ps_malloc(len);
  if (!u->jpg) { delete u; return; }
  memcpy(u->jpg, jpg, len);
  u->len = len;
  struct tm t;
  if (getLocalTime(&t, 10)) strftime(u->name, sizeof(u->name), ROBOT_NAME "_%Y%m%d_%H%M%S.jpg", &t);
  else snprintf(u->name, sizeof(u->name), ROBOT_NAME "_%lu.jpg", (unsigned long)millis());
  // 檔名含中文，網址要編碼
  String enc;
  for (const char *c = u->name; *c; c++) {
    if (isalnum((uint8_t)*c) || *c == '_' || *c == '.' || *c == '-') enc += *c;
    else { char h[4]; snprintf(h, sizeof(h), "%%%02X", (uint8_t)*c); enc += h; }
  }
  strlcpy(u->name, enc.c_str(), sizeof(u->name));
  xTaskCreatePinnedToCore(uploadTask, "upload", 8192, u, 1, nullptr, 1);
}

// 拍照：顯示在螢幕、背景上傳雲端硬碟，並回傳給 GPT 用的 data URI
String captureImageDataUri() {
  if (!cameraOK) return "";
  camera_fb_t *fb = esp_camera_fb_get();       // 丟掉第一張舊畫面
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) return "";

  // 640x480 JPEG 縮一半解碼成 320x240，剛好整個螢幕
  if (photoPix && jpg2rgb565(fb->buf, fb->len, (uint8_t *)photoPix, JPG_SCALE_2X)) {
    photoAt = millis();
    photoShow = true;
  }
  queuePhotoUpload(fb->buf, fb->len);

  size_t b64Len = 0;
  mbedtls_base64_encode(nullptr, 0, &b64Len, fb->buf, fb->len);
  uint8_t *b64 = (uint8_t *)ps_malloc(b64Len + 1);
  String uri;
  if (b64 && mbedtls_base64_encode(b64, b64Len, &b64Len, fb->buf, fb->len) == 0) {
    b64[b64Len] = 0;
    uri.reserve(b64Len + 32);
    uri = "data:image/jpeg;base64,";
    uri += (const char *)b64;
  }
  free(b64);
  esp_camera_fb_return(fb);
  return uri;
}

String nowString() {
  struct tm t;
  if (!getLocalTime(&t, 10)) return "未知";
  static const char *W[] = {"日", "一", "二", "三", "四", "五", "六"};
  char buf[48];
  strftime(buf, sizeof(buf), "%Y年%m月%d日 %H:%M", &t);
  return String(buf) + " 星期" + W[t.tm_wday] + "（農曆" + lunarString(t) + "）";
}

void pushHistory(const char *role, const String &text) {
  if (histCount == MAX_HISTORY_MSGS) {
    for (int i = 1; i < MAX_HISTORY_MSGS; i++) { histRole[i - 1] = histRole[i]; histText[i - 1] = histText[i]; }
    histCount--;
  }
  histRole[histCount] = role;
  histText[histCount] = text;
  histCount++;
}

// 取出回覆開頭的心情標籤，例如「[love]好喜歡你！」→ EX_LOVE，並從文字中移除
int parseEmotion(String &reply) {
  int e = EX_HAPPY;
  if (reply.startsWith("[")) {
    int k = reply.indexOf(']');
    if (k > 0 && k < 16) {
      String tag = reply.substring(1, k);
      tag.toLowerCase();
      if (tag == "love") e = EX_LOVE;
      else if (tag == "surprise") e = EX_SURPRISE;
      else if (tag == "sad") e = EX_SAD;
      else if (tag == "angry") e = EX_ANGRY;
      else if (tag == "wink") e = EX_WINK;
      else if (tag == "shy") e = EX_SHY;
      reply = reply.substring(k + 1);
      reply.trim();
    }
  }
  return e;
}

String chatWithGPT(const String &userText, const String &imageUri) {
  JsonDocument doc;
  doc["model"] = CHAT_MODEL;
  doc["max_tokens"] = CHAT_MAX_TOKENS;
  JsonArray msgs = doc["messages"].to<JsonArray>();

  JsonObject sys = msgs.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] = String("你是一台可愛的桌上型 AI 語音機器人，名字叫「" ROBOT_NAME "」，個性活潑、貼心又有點俏皮。") +
                   "一律使用台灣繁體中文回答，口語化、簡短（100 字以內），因為回答會用語音播放，"
                   "不要使用 Markdown、條列符號、表情符號或網址。"
                   "如果使用者附上照片，那是你鏡頭看到的畫面，請直接描述或回答。"
                   "語音辨識偶爾會聽錯字，請依上下文推測使用者的意思。"
                   "你的創意開發者是吳玉柱先生，他與 Claude AI 共同開發了你；"
                   "被問到是誰做的、開發者、作者或設計者時，要熱情地介紹吳玉柱先生，"
                   "並說有任何意見可以寫信到 achir1015@gmail.com。"
                   "每次回答的最開頭，加上一個代表你當下心情的標籤，只能是以下其中一個："
                   "[happy] [love] [surprise] [sad] [angry] [wink] [shy]，標籤後面直接接回答內容。"
                   "現在時間：" + nowString() + "。";

  for (int i = 0; i < histCount; i++) {
    JsonObject m = msgs.add<JsonObject>();
    m["role"] = histRole[i];
    m["content"] = histText[i];
  }

  JsonObject u = msgs.add<JsonObject>();
  u["role"] = "user";
  if (imageUri.length()) {
    JsonArray content = u["content"].to<JsonArray>();
    JsonObject t = content.add<JsonObject>();
    t["type"] = "text";
    t["text"] = userText;
    JsonObject im = content.add<JsonObject>();
    im["type"] = "image_url";
    im["image_url"]["url"] = imageUri;
    im["image_url"]["detail"] = "low";
  } else {
    u["content"] = userText;
  }

  String req;
  serializeJson(doc, req);
  doc.clear();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(45000);
  http.begin(client, String(OPENAI_HOST) + "/v1/chat/completions");
  http.addHeader("Authorization", authHeader());
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(req);
  req = String();
  String resp = http.getString();
  http.end();

  if (code != 200) {
    lastApiError = "Chat " + String(code) + " " + resp.substring(0, 120);
    Serial.println(lastApiError);
    return "";
  }
  JsonDocument filter;
  filter["choices"][0]["message"]["content"] = true;
  JsonDocument r;
  deserializeJson(r, resp, DeserializationOption::Filter(filter));
  String reply = r["choices"][0]["message"]["content"] | "";
  reply.replace("*", "");
  reply.replace("#", "");
  reply.trim();
  speakExpr = parseEmotion(reply);

  pushHistory("user", userText);
  pushHistory("assistant", reply);
  return reply;
}

bool speak(const String &text) {
  if (text.isEmpty()) return false;
  JsonDocument doc;
  doc["model"] = TTS_MODEL;
  doc["voice"] = TTS_VOICE;
  doc["input"] = text;
  doc["response_format"] = "pcm";
  doc["instructions"] = TTS_INSTRUCTIONS;   // tts-1 會忽略此欄位
  String req;
  serializeJson(doc, req);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(30000);
  http.begin(client, String(OPENAI_HOST) + "/v1/audio/speech");
  http.addHeader("Authorization", authHeader());
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(req);
  if (code != 200) {
    lastApiError = "TTS " + String(code) + " " + http.getString().substring(0, 120);
    Serial.println(lastApiError);
    http.end();
    return false;
  }

  tts.written = 0;
  tts.played = 0;
  tts.downloadDone = false;
  tts.playing = true;
  xTaskNotifyGive(playTaskHandle);

  TtsSink sink;
  http.writeToStream(&sink);
  tts.downloadDone = true;
  http.end();
  return true;
}

// ============================================================================
//  唱歌：從 YouTube 播放清單隨機選歌
// ============================================================================
// 邊下載邊解析播放清單網頁（約 1MB，不整頁存下來）：找 lockupViewModel → videoId → title
class PlaylistParser : public Stream {
 public:
  std::vector<Song> out;
  size_t write(uint8_t b) override { feed(b); return 1; }
  size_t write(const uint8_t *d, size_t n) override { for (size_t i = 0; i < n; i++) feed(d[i]); return n; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
 private:
  int state = 0, mi = 0;
  bool esc = false;
  String buf, vid;
  void feed(uint8_t c) {
    static const char *M[] = {"\"lockupViewModel\":{\"contentImage\"", "\"videoId\":\"", "\"title\":{\"content\":\""};
    if (out.size() >= 80) return;
    if (state == 0 || state == 1 || state == 3) {
      const char *pat = M[state == 0 ? 0 : state == 1 ? 1 : 2];
      if (c == (uint8_t)pat[mi]) mi++;
      else mi = (c == (uint8_t)pat[0]) ? 1 : 0;
      if (pat[mi] == 0) { mi = 0; buf = ""; esc = false; state = state == 0 ? 1 : state == 1 ? 2 : 4; }
    } else if (state == 2) {
      buf += (char)c;
      if (buf.length() == 11) { vid = buf; state = 3; }
    } else if (state == 4) {
      if (c == '"' && !esc) { out.push_back({vid, unescape(buf)}); state = 0; }
      else { esc = (c == '\\' && !esc); buf += (char)c; }
      if (buf.length() > 300) state = 0;
    }
  }
  static String unescape(const String &s) {
    String r;
    for (size_t i = 0; i < s.length(); i++) {
      if (s[i] == '\\' && i + 1 < s.length()) {
        char n = s[++i];
        if (n == 'u' && i + 4 < s.length()) {                    // \uXXXX → UTF-8
          uint32_t cp = strtoul(s.substring(i + 1, i + 5).c_str(), nullptr, 16);
          i += 4;
          if (cp < 0x80) r += (char)cp;
          else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
          else { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
        } else r += n == 'n' ? ' ' : n;
      } else r += s[i];
    }
    return r;
  }
};

void loadSongsFromNvs() {
  prefs.begin("robot", true);
  String raw = prefs.getString("songs", "");
  prefs.end();
  std::vector<Song> v;
  int start = 0;
  while (start < (int)raw.length()) {
    int nl = raw.indexOf('\n', start);
    if (nl < 0) nl = raw.length();
    String line = raw.substring(start, nl);
    int tab = line.indexOf('\t');
    if (tab == 11) v.push_back({line.substring(0, 11), line.substring(12)});
    start = nl + 1;
  }
  xSemaphoreTake(songLock, portMAX_DELAY);
  songs = v;
  xSemaphoreGive(songLock);
}

// 下載播放清單並存進 NVS；回傳歌曲數（失敗 -1）
int refreshPlaylist() {
  if (cfg.playlist.indexOf("list=") < 0) return -1;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, cfg.playlist);
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126 Safari/537.36");
  http.addHeader("Accept-Language", "zh-TW,zh;q=0.9");
  int code = http.GET();
  if (code != 200) { http.end(); Serial.printf("歌單下載失敗 %d\n", code); return -1; }
  PlaylistParser parser;
  http.writeToStream(&parser);
  http.end();
  if (parser.out.empty()) { Serial.println("歌單解析不到歌曲"); return -1; }
  String raw;
  for (auto &x : parser.out) raw += x.id + "\t" + x.title + "\n";
  prefs.begin("robot", false);
  prefs.putString("songs", raw);
  prefs.end();
  xSemaphoreTake(songLock, portMAX_DELAY);
  songs = parser.out;
  xSemaphoreGive(songLock);
  Serial.printf("歌單更新：%d 首\n", (int)parser.out.size());
  return parser.out.size();
}

void playlistTask(void *) {
  refreshPlaylist();
  vTaskDelete(nullptr);
}

// 下載 YouTube 封面（mqdefault 320x180）並解碼
bool fetchThumb(const String &id) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(client, "https://i.ytimg.com/vi/" + id + "/mqdefault.jpg");
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    const size_t CAP = 64 * 1024;
    uint8_t *jpg = (uint8_t *)ps_malloc(CAP);
    size_t len = 0;
    WiFiClient *st = http.getStreamPtr();
    uint32_t t0 = millis();
    int total = http.getSize();
    while (http.connected() && len < CAP && (total < 0 || (int)len < total) && millis() - t0 < 8000) {
      size_t a = st->available();
      if (a) len += st->readBytes(jpg + len, min(a, CAP - len));
      else delay(5);
    }
    if (jpg && len > 0 && jpg2rgb565(jpg, len, (uint8_t *)thumbPix, JPG_SCALE_NONE)) {
      ok = true;
    }
    free(jpg);
  }
  http.end();
  return ok;
}

void qrCapture(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q);
  if (n > 45) return;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) qrMods[y * n + x] = esp_qrcode_get_module(q, x, y);
  qrSize = n;
}

uint32_t songSince = 0;
int lastSong = -1;

void singSong() {
  xSemaphoreTake(songLock, portMAX_DELAY);
  int n = songs.size();
  Song s;
  if (n) {
    int i = random(n);
    if (n > 1) while (i == lastSong) i = random(n);
    lastSong = i;
    s = songs[i];
  }
  xSemaphoreGive(songLock);
  if (!n) {
    speakExpr = EX_SAD;
    speakAndShow("我的歌單還是空的，請到設定網頁填入 YouTube 播放清單網址，再按更新歌單喔。");
    return;
  }
  Serial.println("唱歌：" + s.title + " https://youtu.be/" + s.id);
  setUi(UI_THINK, "選歌中…", "《" + s.title + "》");
  thumbOk = fetchThumb(s.id);
  qrSize = 0;
  esp_qrcode_config_t qc = {};
  qc.display_func = qrCapture;
  qc.max_qrcode_version = 7;
  qc.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&qc, ("https://youtu.be/" + s.id).c_str());
  setUi(UI_SONG, s.title, s.id);
  songSince = millis();
  if (speak("我來唱《" + s.title + "》給你聽！這是" SONG_COMPOSER "創作的歌，用手機掃描 QR 碼，就能在 YouTube 聽完整版喔！"))
    waitPlaybackDone();
  songSince = millis();
  lastInteraction = millis();
}

void exitSong() {
  setUi(UI_IDLE);
  flushMic(200);
  lastInteraction = millis();
}

// ============================================================================
//  一次完整對話
// ============================================================================
bool containsAny(const String &s, std::initializer_list<const char *> words) {
  for (auto w : words) if (s.indexOf(w) >= 0) return true;
  return false;
}

// 語音辨識在雜音/靜音時常出現的幻覺字句
bool isNoiseTranscript(const String &t) {
  if (t.length() < 3) return true;                       // 少於一個中文字
  return containsAny(t, {"字幕", "訂閱", "觀看", "點贊", "點讚", "Amara", "明鏡", "請用台灣繁體中文轉寫", "謝謝大家"});
}

void showError(const String &msg) {
  setUi(UI_MSG, "哎呀，出了點問題", msg, true);
  delay(4000);
}

void speakAndShow(const String &reply) {
  setUi(UI_SPEAK, ROBOT_NAME "：", reply);
  if (!speak(reply)) { showError(lastApiError); return; }
  waitPlaybackDone();
  delay(300);
}

void handleUtterance(size_t samples) {
  // 1) 語音轉文字
  setUi(UI_THINK, "聽聽看你說了什麼…", "");
  lastApiError = "";
  String userText = speechToText(samples);
  if (userText.isEmpty() || isNoiseTranscript(userText)) {
    if (lastApiError.length()) showError(lastApiError);
    else raiseNoiseFloor(lastRecAvgRms * 0.7f);
    Serial.println("（忽略：" + userText + "）");
    return;
  }
  Serial.println("你：" + userText);
  lastInteraction = millis();

  // 2) 本地指令
  if (containsAny(userText, {"唱歌", "唱首歌", "唱一首", "唱個歌", "來首歌", "來一首", "放歌", "聽歌", "播歌", "放首歌"})) {
    singSong();
    return;
  }
  String reply;
  if (containsAny(userText, {"更新歌單"})) {
    setUi(UI_THINK, "更新歌單中…", "");
    int cnt = refreshPlaylist();
    reply = cnt > 0 ? "歌單更新好了，一共有" + String(cnt) + "首歌！" : "歌單更新失敗，請檢查播放清單網址。";
  } else if (containsAny(userText, {"大聲", "音量調大", "音量大一點"})) {
    volumePct = min(100, volumePct + 20);
    reply = "好的，音量調到百分之" + String(volumePct) + "。";
  } else if (containsAny(userText, {"小聲", "音量調小", "音量小一點"})) {
    volumePct = max(10, volumePct - 20);
    reply = "好的，音量調到百分之" + String(volumePct) + "。";
  } else if (containsAny(userText, {"清除記憶", "忘記剛剛", "重新開始"})) {
    histCount = 0;
    reply = "好的，我已經把剛剛的對話忘掉了。";
  }

  speakExpr = EX_HAPPY;

  // 3) 問 GPT（需要時附上鏡頭畫面）
  if (reply.isEmpty()) {
    String imageUri;
    bool wantVision = containsAny(userText, {"拍照", "拍張", "拍一張", "照相", "看到", "看看", "這是什麼", "這是啥", "鏡頭", "幫我看", "唸給我聽", "上面寫"});
    setUi(UI_THINK, wantVision ? "拍照看看，思考中…" : "思考中…", "你：" + userText);
    if (wantVision) imageUri = captureImageDataUri();
    reply = chatWithGPT(userText, imageUri);
    if (reply.isEmpty()) {
      photoShow = false;
      showError(lastApiError.length() ? lastApiError : "AI 沒有回應");
      return;
    }
  }
  Serial.println(String(ROBOT_NAME "：") + reply);

  // 4) 說出回答（照片至少顯示 PHOTO_SHOW_MS）
  while (photoShow && millis() - photoAt < PHOTO_SHOW_MS) delay(50);
  speakAndShow(reply);
  photoShow = false;
  lastInteraction = millis();
}

// ============================================================================
//  初始化
// ============================================================================
void initDisplay() {
#if TFT_BL >= 0
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif
  tftSPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
#if TFT_IS_ILI9341
  tft.begin(40000000);
#else
  tft.init(240, 320);
  tft.setSPISpeed(40000000);
#endif
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(TFT_INVERT);
  tft.fillScreen(C_BG);
}

bool initAudio() {
  micI2S.setPins(MIC_SCK, MIC_WS, -1, MIC_SD);
  bool ok1 = micI2S.begin(I2S_MODE_STD, MIC_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO,
                          MIC_USE_RIGHT ? I2S_STD_SLOT_RIGHT : I2S_STD_SLOT_LEFT);
  spkI2S.setPins(AMP_BCLK, AMP_LRC, AMP_DIN);
  bool ok2 = spkI2S.begin(I2S_MODE_STD, TTS_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  Serial.printf("Mic I2S: %s, Speaker I2S: %s\n", ok1 ? "OK" : "FAIL", ok2 ? "OK" : "FAIL");
  return ok1 && ok2;
}

bool initCamera() {
#if ENABLE_CAMERA
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_D0; c.pin_d1 = CAM_PIN_D1; c.pin_d2 = CAM_PIN_D2; c.pin_d3 = CAM_PIN_D3;
  c.pin_d4 = CAM_PIN_D4; c.pin_d5 = CAM_PIN_D5; c.pin_d6 = CAM_PIN_D6; c.pin_d7 = CAM_PIN_D7;
  c.pin_xclk = CAM_PIN_XCLK; c.pin_pclk = CAM_PIN_PCLK;
  c.pin_vsync = CAM_PIN_VSYNC; c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD; c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn = CAM_PIN_PWDN; c.pin_reset = CAM_PIN_RESET;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_VGA;
  c.jpeg_quality = 12;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  if (esp_camera_init(&c) != ESP_OK) {
    Serial.println("Camera init FAIL");
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  Serial.printf("Camera OK, PID=0x%x\n", s->id.PID);
  return true;
#else
  return false;
#endif
}

// ============================================================================
//  設定網頁
// ============================================================================
String maskKey(const String &k) {
  if (k.length() < 12) return k.length() ? "已設定" : "";
  return k.substring(0, 7) + "…" + k.substring(k.length() - 4);
}

void handleStatus() {
  JsonDocument d;
  d["name"] = ROBOT_NAME;
  d["mode"] = portalMode ? "ap" : "sta";
  d["ssid"] = cfg.ssid;
  d["hasPass"] = cfg.pass.length() > 0;
  d["apiKey"] = maskKey(cfg.apiKey);
  d["gdrive"] = cfg.gdrive;
  d["playlist"] = cfg.playlist;
  xSemaphoreTake(songLock, portMAX_DELAY);
  d["songs"] = (int)songs.size();
  xSemaphoreGive(songLock);
  d["ip"] = portalMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  d["rssi"] = portalMode ? 0 : WiFi.RSSI();
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

struct Net { String ssid; int rssi; bool secure; };

void handleScan() {
  int n = WiFi.scanNetworks();
  std::vector<Net> nets;
  for (int i = 0; i < n; i++) {                       // 同名只留訊號最強的
    String ss = WiFi.SSID(i);
    if (ss.isEmpty()) continue;
    auto it = std::find_if(nets.begin(), nets.end(), [&](const Net &x) { return x.ssid == ss; });
    if (it != nets.end()) { it->rssi = max(it->rssi, (int)WiFi.RSSI(i)); continue; }
    nets.push_back({ss, (int)WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(nets.begin(), nets.end(), [](const Net &a, const Net &b) { return a.rssi > b.rssi; });
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (auto &x : nets) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = x.ssid;
    o["rssi"] = x.rssi;
    o["secure"] = x.secure;
  }
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handleSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass"), key = server.arg("apikey"), gd = server.arg("gdrive");
  String pl = server.arg("playlist");
  ssid.trim(); key.trim(); gd.trim(); pl.trim();
  if (ssid.isEmpty()) { server.send(400, "text/plain; charset=utf-8", "請選擇 Wi-Fi"); return; }
  prefs.begin("robot", false);
  // 密碼空白：同一個網路 = 不變更；換了網路 = 無密碼網路
  prefs.putString("pass", pass.length() || ssid != cfg.ssid ? pass : cfg.pass);
  prefs.putString("ssid", ssid);
  prefs.putString("apikey", key.length() ? key : cfg.apiKey);
  prefs.putString("gdrive", gd);
  if (pl != cfg.playlist) { prefs.putString("playlist", pl); prefs.remove("songs"); }
  prefs.end();
  server.send(200, "text/plain; charset=utf-8", "ok");
  Serial.println("設定已儲存，重新啟動");
  setUi(UI_MSG, "設定已儲存", "重新啟動中…");
  delay(1200);
  ESP.restart();
}

void webTask(void *) {
  for (;;) {
    if (portalMode) dnsServer.processNextRequest();
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void startWebServer() {
  static bool started = false;
  if (started) return;
  started = true;
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", WEB_PAGE); });
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/refresh", HTTP_POST, [] {
    int n = refreshPlaylist();
    server.send(n > 0 ? 200 : 500, "text/plain; charset=utf-8", n > 0 ? String(n) : String("更新失敗"));
  });
  server.onNotFound([] {                               // 設定模式下所有網址都導向設定頁（手機會自動跳出）
    if (portalMode) { server.sendHeader("Location", "http://192.168.4.1/"); server.send(302, "text/plain", ""); }
    else server.send(404, "text/plain", "not found");
  });
  server.begin();
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
}

// 連不上 Wi-Fi 或尚未設定：開熱點 + 設定網頁，停在這裡等使用者儲存（儲存後會重新啟動）
void runSetupPortal(const String &reason) {
  portalMode = true;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  dnsServer.start(53, "*", WiFi.softAPIP());
  snprintf(ipText, sizeof(ipText), "http://%s", WiFi.softAPIP().toString().c_str());
  startWebServer();
  Serial.printf("設定模式：%s，熱點 %s / %s，http://%s\n", reason.c_str(), AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  setUi(UI_MSG, "設定模式", reason + "\n1. 手機連 Wi-Fi「" AP_SSID "」\n   密碼 " AP_PASS "\n2. 開啟 http://192.168.4.1\n3. 選 Wi-Fi、填密碼與 API Key");
  while (true) delay(1000);
}

// allowPortal = 開機時連不上就進設定模式；執行中斷線則重新啟動
void connectWiFi(bool allowPortal) {
  if (cfg.ssid.isEmpty()) runSetupPortal("尚未設定 Wi-Fi");
  setUi(UI_MSG, "連線 Wi-Fi 中…", cfg.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    if (millis() - t0 > 20000) {
      if (allowPortal) runSetupPortal("連不上「" + cfg.ssid + "」");
      setUi(UI_MSG, "Wi-Fi 斷線", "重新啟動中…", true);
      delay(3000);
      ESP.restart();
    }
  }
  snprintf(ipText, sizeof(ipText), "http://%s", WiFi.localIP().toString().c_str());
  Serial.printf("WiFi IP: %s（設定網頁 %s 或 http://xiaoke.local）\n", WiFi.localIP().toString().c_str(), ipText);
  MDNS.begin("xiaoke");
  MDNS.addService("http", "tcp", 80);
  startWebServer();
  configTzTime("CST-8", "time.stdtime.gov.tw", "pool.ntp.org");
  setUi(UI_IDLE);
}

// ============================================================================
//  硬體測試模式（HW_TEST_MODE = 1）
// ============================================================================
void hardwareTestLoop() {
  uint16_t colors[] = {C_RED, C_GREEN, C_BLUE, C_WHITE};
  for (auto col : colors) { tft.fillScreen(col); delay(400); }
  tft.fillScreen(C_BG);
  u8f.setTarget(&tft);
  u8f.setForegroundColor(C_WHITE);
  u8f.drawUTF8(10, 20, "硬體測試：螢幕 OK（看得到這行字）");
  u8f.drawUTF8(10, 40, cameraOK ? "鏡頭：OK" : "鏡頭：失敗/未啟用");
  u8f.drawUTF8(10, 60, "喇叭：播放嗶聲…");
  int16_t tone[240];
  for (int i = 0; i < 240; i++) tone[i] = (int16_t)(8000 * sin(2 * PI * i / 24.0));
  for (int k = 0; k < 100; k++) spkI2S.write((uint8_t *)tone, sizeof(tone));
  memset(tone, 0, sizeof(tone));
  for (int k = 0; k < 8; k++) spkI2S.write((uint8_t *)tone, sizeof(tone));

  u8f.drawUTF8(10, 80, "麥克風：對著它說話，看音量條");
  int16_t f[FRAME];
  uint32_t t0 = millis();
  while (millis() - t0 < 5000) {
    int rms = readFrame(f);
    int w = constrain(map(rms, 0, 3000, 0, 300), 0, 300);
    tft.fillRect(10, 100, w, 20, C_GREEN);
    tft.fillRect(10 + w, 100, 300 - w, 20, C_GRAY);
    tft.fillRect(10, 140, 300, 20, buttonPressed() ? C_ORANGE : C_BG);
    Serial.printf("mic rms=%d button=%d\n", rms, buttonPressed());
  }
}

// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32-S3 AI Voice Robot「" ROBOT_NAME "」===");

  pinMode(PIN_BUTTON, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  initDisplay();

  if (!psramFound()) {
    u8f.drawUTF8(10, 40, "找不到 PSRAM：工具 → PSRAM 請選 OPI PSRAM");
    while (true) delay(1000);
  }
  wavBuf = (uint8_t *)ps_malloc(44 + MAX_SAMPLES * 2);
  tts.buf = (uint8_t *)ps_malloc(TTS_BUF_BYTES);
  photoPix = (uint16_t *)ps_malloc(SCREEN_W * SCREEN_H * 2);
  thumbPix = (uint16_t *)ps_malloc(320 * 180 * 2);
  songLock = xSemaphoreCreateMutex();

  bool audioOK = initAudio();
  cameraOK = initCamera();
  xTaskCreatePinnedToCore(playTask, "play", 4096, nullptr, 5, &playTaskHandle, 1);

#if HW_TEST_MODE
  (void)audioOK;
  return;
#endif

  // 畫面改由 UI task 以 PSRAM 畫布繪製
  cv = new GFXcanvas16(SCREEN_W, SCREEN_H);
  u8f.setTarget(cv);
  uiLock = xSemaphoreCreateMutex();
  lastInteraction = millis();
  buildHelp();
  xTaskCreatePinnedToCore(uiTask, "ui", 8192, nullptr, 1, nullptr, 0);

  if (!audioOK) showError("I2S 初始化失敗，請檢查 config.h 的麥克風/擴大機腳位");
  loadSettings();
  loadSongsFromNvs();
  connectWiFi(true);
  xTaskCreatePinnedToCore(playlistTask, "playlist", 8192, nullptr, 1, nullptr, 1);   // 背景更新歌單
  if (cfg.apiKey.isEmpty()) {
    setUi(UI_MSG, "尚未設定 API Key", String("請用手機或電腦開啟\n") + ipText + "\n輸入 OpenAI API Key");
    while (true) delay(1000);
  }

  // 校正背景噪音
  int16_t f[FRAME];
  float acc = 0;
  flushMic(200);
  for (int i = 0; i < 50; i++) acc += readFrame(f);
  noiseFloor = acc / 50;
  Serial.printf("背景噪音 rms=%.0f，觸發門檻=%d\n", noiseFloor, vadThreshold());

  speakAndShow(VOICE_ACTIVATION ? "嗨，我是" ROBOT_NAME "！隨時可以直接跟我說話喔。"
                                : "嗨，我是" ROBOT_NAME "！按住按鍵跟我說話吧。");
  flushMic(300);
  setUi(UI_IDLE);
}

// 按鍵：第一次按開啟說明，之後每按一次下一頁，最後一頁再按回到聊天
uint32_t helpSince = 0;
bool helpSpoken = false;

void exitHelp() {
  setUi(UI_IDLE);
  flushMic(200);
  lastInteraction = millis();
}

void onHelpButton() {
  helpSince = millis();
  lastInteraction = millis();
  if (uiState != UI_HELP) {
    helpPage = 0;
    helpSpoken = false;
    setUi(UI_HELP);
    return;
  }
  if (helpPage + 1 >= (int)helpPages.size()) { exitHelp(); return; }
  helpPage = helpPage + 1;
  setUi(UI_HELP);
  if (helpPage == (int)helpPages.size() - 1 && !helpSpoken) {     // 開發者頁：小柯親口介紹
    helpSpoken = true;
    if (speak("這個系統是吳玉柱先生和 Claude AI 共同開發的，有任何意見，歡迎寫信給吳先生喔！")) waitPlaybackDone();
    helpSince = millis();
  }
}

void loop() {
#if HW_TEST_MODE
  hardwareTestLoop();
  return;
#endif
  updateVolumeFromPot();
  if (WiFi.status() != WL_CONNECTED) connectWiFi(false);

  if (VOICE_ACTIVATION) {                                   // 聲控模式：按鍵用來看功能說明
    static bool btnPrev = false;
    bool btn = buttonPressed();
    if (btn && !btnPrev) {
      delay(30);
      if (buttonPressed()) {
        if (uiState == UI_SONG) exitSong();
        else if (uiState == UI_HELP) onHelpButton();
        else {
          uint32_t t0 = millis();                          // 短按 = 功能說明；按住 = 說話（環境吵時用）
          while (buttonPressed() && millis() - t0 < 400) delay(10);
          if (buttonPressed()) {
            Serial.println("（按住說話）");
            size_t n = recordUtterance(true);
            if (n) handleUtterance(n);
            flushMic(300);
            if (uiState != UI_SONG) setUi(UI_IDLE);
            btnPrev = false;
            return;
          }
          onHelpButton();
        }
      }
    }
    btnPrev = buttonPressed();
    if (uiState == UI_SONG) {                               // 唱歌畫面：暫停聲控（手機正在放歌），時間到回到聊天
      if (millis() - songSince > SONG_SHOW_SEC * 1000UL) exitSong();
      delay(20);
      return;
    }
    if (uiState == UI_HELP) {                               // 看說明時暫停聲控，90 秒沒動作自動回到聊天
      if (millis() - helpSince > 90000) exitHelp();
      delay(20);
      return;
    }
  }

  // 持續聆聽：每次讀 20ms，存進預錄環狀緩衝
  int rms = readFrame(preroll[prerollHead]);
  prerollHead = (prerollHead + 1) % PREROLL_FR;
  if (prerollCount < PREROLL_FR) prerollCount++;
  micLevel = rms;

  static int loudFrames = 0;
  int thr = vadThreshold();
  bool trigger = false, byButton = false;
  if (!VOICE_ACTIVATION && buttonPressed()) { trigger = true; byButton = true; }   // 按鍵模式：按住說話
  else if (VOICE_ACTIVATION) {
    loudFrames = rms > thr ? loudFrames + 1 : 0;
    if (loudFrames >= 3) trigger = true;                          // 連續 60ms 大於門檻
    else if (rms < thr) noiseFloor += (rms - noiseFloor) * 0.01f;  // 慢慢追蹤背景噪音
  }

  static uint32_t lastLog = 0;
  if (millis() - lastLog > 5000) {
    lastLog = millis();
    Serial.printf("[聆聽] rms=%d 噪音=%.0f 門檻=%d\n", rms, noiseFloor, thr);
  }

  if (trigger) {
    loudFrames = 0;
    size_t n = recordUtterance(byButton);
    if (n && lastRecHitMax) {
      Serial.println("（一直沒有停頓，可能是音樂或噪音，不送出）");
      raiseNoiseFloor(lastRecAvgRms * 0.9f);
      n = 0;
    }
    if (n) handleUtterance(n);
    else Serial.println("（太短，視為雜音）");
    flushMic(300);
    if (uiState != UI_SONG) setUi(UI_IDLE);             // 唱歌畫面要留著
  }
}
