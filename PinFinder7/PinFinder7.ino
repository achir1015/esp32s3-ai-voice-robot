// PinFinder7 — TFT 腳位視覺掃描 + 按鍵偵測
// 逐一嘗試 SCK/MOSI/CS/DC 組合；只有正確的組合能點亮螢幕，螢幕上會顯示該組腳位。
// 掃描結束後持續偵測按鍵，序列埠每秒回報。
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>

const int POOL[] = {3, 14, 21, 45, 46, 47, 48};
const int NP = sizeof(POOL) / sizeof(int);
SPIClass spi(HSPI);
GFXcanvas1 canvas(240, 320);
int sck, mosi, cs, dc;

void cmd(uint8_t c) { digitalWrite(dc, LOW); digitalWrite(cs, LOW); spi.transfer(c); digitalWrite(cs, HIGH); }
void dat(uint8_t d) { digitalWrite(dc, HIGH); digitalWrite(cs, LOW); spi.transfer(d); digitalWrite(cs, HIGH); }
void cmd4(uint8_t c, uint16_t a, uint16_t b) { cmd(c); dat(a >> 8); dat(a); dat(b >> 8); dat(b); }

void tryCombo(int idx) {
  for (int i = 0; i < NP; i++) { pinMode(POOL[i], OUTPUT); digitalWrite(POOL[i], HIGH); }   // RST/背光/其它 CS 都拉 HIGH
  spi.begin(sck, -1, mosi, -1);
  spi.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  cmd(0x11); delay(10);                  // Sleep out
  cmd(0x3A); dat(0x55);                  // 16-bit color
  cmd(0x36); dat(0x00);                  // 直向
  cmd(0x29);                             // Display on
  cmd4(0x2A, 0, 239);
  cmd4(0x2B, 0, 319);
  cmd(0x2C);

  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setTextSize(3);
  canvas.setCursor(10, 20);  canvas.printf("#%d", idx);
  canvas.setCursor(10, 80);  canvas.printf("SCK =%d", sck);
  canvas.setCursor(10, 120); canvas.printf("MOSI=%d", mosi);
  canvas.setCursor(10, 160); canvas.printf("CS  =%d", cs);
  canvas.setCursor(10, 200); canvas.printf("DC  =%d", dc);
  canvas.fillRect(0, 290, 240, 30, 1);

  digitalWrite(dc, HIGH); digitalWrite(cs, LOW);
  const uint8_t *buf = canvas.getBuffer();
  uint8_t line[240 * 2];
  for (int y = 0; y < 320; y++) {
    for (int x = 0; x < 240; x++) {
      bool on = buf[(y * 240 + x) / 8] & (0x80 >> (x & 7));
      uint16_t c = on ? 0xFFE0 : 0x001F;  // 黃字藍底（反相時也看得出來）
      line[x * 2] = c >> 8; line[x * 2 + 1] = c;
    }
    spi.writeBytes(line, sizeof(line));
  }
  digitalWrite(cs, HIGH);
  spi.endTransaction();
  spi.end();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n===== PinFinder7 開始 =====");
  int idx = 0;
  for (int a = 0; a < NP; a++) for (int b = 0; b < NP; b++) for (int c = 0; c < NP; c++) for (int d = 0; d < NP; d++) {
    sck = POOL[a]; mosi = POOL[b]; cs = POOL[c]; dc = POOL[d];
    if (sck == mosi || sck == cs || sck == dc || mosi == cs || mosi == dc || cs == dc) continue;
    idx++;
    Serial.printf("#%d SCK=%d MOSI=%d CS=%d DC=%d\n", idx, sck, mosi, cs, dc);
    tryCombo(idx);
  }
  Serial.println("===== 掃描完成，開始偵測按鍵 =====");
  for (int i = 0; i < NP; i++) pinMode(POOL[i], INPUT_PULLUP);
  pinMode(0, INPUT_PULLUP);
}

int presses[49];
int lastV[49];
void loop() {
  static bool init = false;
  if (!init) { for (int i = 0; i < NP; i++) lastV[POOL[i]] = digitalRead(POOL[i]); lastV[0] = digitalRead(0); init = true; }
  static uint32_t tPrint = 0;
  for (int i = -1; i < NP; i++) {
    int p = i < 0 ? 0 : POOL[i];
    int v = digitalRead(p);
    if (v != lastV[p]) { if (v == LOW) presses[p]++; lastV[p] = v; }
  }
  if (millis() - tPrint > 1000) {
    tPrint = millis();
    Serial.print("按鍵計數：");
    for (int i = -1; i < NP; i++) { int p = i < 0 ? 0 : POOL[i]; Serial.printf("G%d=%d ", p, presses[p]); }
    Serial.println();
  }
  delay(5);
}
