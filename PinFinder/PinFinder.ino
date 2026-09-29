// ============================================================================
//  PinFinder — 自動找出擴展板上 INMP441 / MAX98357A / TFT / 按鍵 的 GPIO
//  上傳後開序列埠 115200，約 1~2 分鐘完成；過程中喇叭會發出嗶聲
// ============================================================================
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

const int CAND[] = {1, 2, 3, 14, 19, 20, 21, 41, 42, 45, 46, 47, 48};
const int NCAND = sizeof(CAND) / sizeof(int);
bool used[49];

int micSck = -1, micWs = -1, micSd = -1, micSlotRight = 0;
int ampBclk = -1, ampLrc = -1, ampDin = -1;

i2s_chan_handle_t rxCh = nullptr, txCh = nullptr;

void resetPin(int p) {
  gpio_reset_pin((gpio_num_t)p);
  pinMode(p, INPUT);
}

// ---------------------------------------------------------------- I2S ----
bool startRx(int sck, int ws, int din, uint32_t rate) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&cc, nullptr, &rxCh) != ESP_OK) return false;
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)sck;
  sc.gpio_cfg.ws = (gpio_num_t)ws;
  sc.gpio_cfg.dout = I2S_GPIO_UNUSED;
  sc.gpio_cfg.din = din < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)din;
  if (i2s_channel_init_std_mode(rxCh, &sc) != ESP_OK) return false;
  if (din >= 0) gpio_pulldown_en((gpio_num_t)din);
  return i2s_channel_enable(rxCh) == ESP_OK;
}
void stopRx(int sck, int ws, int din) {
  if (rxCh) { i2s_channel_disable(rxCh); i2s_del_channel(rxCh); rxCh = nullptr; }
  resetPin(sck); resetPin(ws); if (din >= 0) resetPin(din);
}

bool startTx(int bclk, int lrc, int dout) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &txCh, nullptr) != ESP_OK) return false;
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)bclk;
  sc.gpio_cfg.ws = (gpio_num_t)lrc;
  sc.gpio_cfg.dout = (gpio_num_t)dout;
  sc.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(txCh, &sc) != ESP_OK) return false;
  return i2s_channel_enable(txCh) == ESP_OK;
}
void stopTx() {
  if (txCh) { i2s_channel_disable(txCh); i2s_del_channel(txCh); txCh = nullptr; }
}

// 讀麥克風 n 個樣本（取有資料的聲道），回傳 1kHz 能量比
int32_t micBuf[2048];
float toneRatio(bool playTone) {
  static int16_t tone[256 * 2];
  for (int i = 0; i < 256; i++) {
    int16_t v = playTone ? (int16_t)(12000 * sin(2 * PI * 1000.0 * i / 16000.0)) : 0;
    tone[i * 2] = tone[i * 2 + 1] = v;
  }
  int32_t raw[256 * 2];
  size_t got;
  int n = 0;
  uint32_t t0 = millis();
  while (n < 2048 && millis() - t0 < 1500) {
    if (txCh) i2s_channel_write(txCh, tone, sizeof(tone), &got, 50);
    if (i2s_channel_read(rxCh, raw, sizeof(raw), &got, 100) != ESP_OK) continue;
    for (size_t i = 0; i < got / 8 && n < 2048; i++) micBuf[n++] = raw[i * 2 + micSlotRight] >> 8;
  }
  // 丟掉前 512 點（擴大機啟動），Goertzel 1kHz @16kHz
  double s1 = 0, s2 = 0, total = 0, mean = 0;
  for (int i = 512; i < n; i++) mean += micBuf[i];
  mean /= max(1, n - 512);
  double coeff = 2 * cos(2 * PI * 1000.0 / 16000.0);
  for (int i = 512; i < n; i++) {
    double x = micBuf[i] - mean;
    double s0 = x + coeff * s1 - s2; s2 = s1; s1 = s0;
    total += x * x;
  }
  double p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  int N = max(1, n - 512);
  return (float)(p / N) / (float)(total / N * 2 + 1.0);   // 1kHz 佔總能量比例（0~1）
}

// ---------------------------------------------------------------- 步驟 ----
void stepPassive() {
  Serial.println("\n[1] 腳位狀態（pulldown / pullup）");
  for (int i = 0; i < NCAND; i++) {
    int p = CAND[i];
    pinMode(p, INPUT_PULLDOWN); delay(3); int d = digitalRead(p);
    pinMode(p, INPUT_PULLUP);   delay(3); int u = digitalRead(p);
    pinMode(p, INPUT);
    const char *st = d ? "被外部拉 HIGH（按鍵模組？）" : (!u ? "被外部拉 LOW" : "浮接");
    Serial.printf("  GPIO%-2d : %s\n", p, st);
    if (d || !u) used[p] = true;
  }
  pinMode(0, INPUT_PULLUP);
}

void stepMic() {
  Serial.println("\n[2] 找麥克風 INMP441（SCK, WS 組合 → 看哪支腳有資料輸出）");
  int bestCount = 0;
  for (int a = 0; a < NCAND; a++) for (int b = 0; b < NCAND; b++) {
    int sck = CAND[a], ws = CAND[b];
    if (sck == ws || used[sck] || used[ws]) continue;
    if (!startRx(sck, ws, -1, 48000)) { stopRx(sck, ws, -1); continue; }
    for (int i = 0; i < NCAND; i++) {
      int p = CAND[i];
      if (p != sck && p != ws && !used[p]) pinMode(p, INPUT_PULLDOWN);
    }
    delay(150);                                   // INMP441 啟動時間
    for (int i = 0; i < NCAND; i++) {
      int p = CAND[i];
      if (p == sck || p == ws || used[p]) continue;
      int last = digitalRead(p), tog = 0;
      for (int k = 0; k < 4000; k++) { int v = gpio_get_level((gpio_num_t)p); tog += (v != last); last = v; }
      if (tog > 50) {
        Serial.printf("  SCK=%d WS=%d → GPIO%d 有資料（變化 %d 次）\n", sck, ws, p, tog);
        if (tog > bestCount) { bestCount = tog; micSck = sck; micWs = ws; micSd = p; }
      }
      pinMode(p, INPUT);
    }
    stopRx(sck, ws, -1);
  }
  if (micSd < 0) { Serial.println("  ✗ 找不到麥克風"); return; }
  used[micSck] = used[micWs] = used[micSd] = true;

  // 判斷左右聲道
  startRx(micSck, micWs, micSd, 16000);
  delay(300);
  int32_t raw[512]; size_t got;
  int64_t eL = 0, eR = 0;
  for (int r = 0; r < 8; r++) {
    i2s_channel_read(rxCh, raw, sizeof(raw), &got, 200);
    for (size_t i = 0; i < got / 8; i++) { eL += llabs(raw[i * 2] >> 8); eR += llabs(raw[i * 2 + 1] >> 8); }
  }
  micSlotRight = eR > eL ? 1 : 0;
  Serial.printf("  ✓ 麥克風：SCK=%d WS=%d SD=%d 聲道=%s\n", micSck, micWs, micSd, micSlotRight ? "右" : "左");
}

void stepAmp() {
  if (micSd < 0) return;
  Serial.println("\n[3] 找擴大機 MAX98357A（播 1kHz，用麥克風聽）");
  float base = toneRatio(false);
  Serial.printf("  背景 1kHz 比例 = %.4f\n", base);

  // 先找 BCLK/LRC：資料同時送到所有其它腳
  float best = 0;
  for (int a = 0; a < NCAND && ampBclk < 0; a++) for (int b = 0; b < NCAND; b++) {
    int bclk = CAND[a], lrc = CAND[b];
    if (bclk == lrc || used[bclk] || used[lrc]) continue;
    int first = -1;
    for (int i = 0; i < NCAND; i++) {
      int p = CAND[i];
      if (p == bclk || p == lrc || used[p]) continue;
      if (first < 0) { first = p; continue; }
    }
    if (first < 0) continue;
    if (!startTx(bclk, lrc, first)) { stopTx(); continue; }
    for (int i = 0; i < NCAND; i++) {
      int p = CAND[i];
      if (p == bclk || p == lrc || p == first || used[p]) continue;
      esp_rom_gpio_pad_select_gpio(p);
      gpio_set_direction((gpio_num_t)p, GPIO_MODE_OUTPUT);
      esp_rom_gpio_connect_out_signal(p, I2S1O_SD_OUT_IDX, false, false);
    }
    float r = toneRatio(true);
    stopTx();
    for (int i = 0; i < NCAND; i++) if (!used[CAND[i]]) resetPin(CAND[i]);
    if (r > 0.2 && r > best) {
      best = r; ampBclk = bclk; ampLrc = lrc;
      Serial.printf("  BCLK=%d LRC=%d → 聽到嗶聲（比例 %.3f）\n", bclk, lrc, r);
    }
  }
  if (ampBclk < 0) { Serial.println("  ✗ 找不到擴大機"); return; }

  // 再逐一找 DIN
  best = 0;
  for (int i = 0; i < NCAND; i++) {
    int p = CAND[i];
    if (p == ampBclk || p == ampLrc || used[p]) continue;
    if (!startTx(ampBclk, ampLrc, p)) { stopTx(); continue; }
    float r = toneRatio(true);
    stopTx();
    resetPin(ampBclk); resetPin(ampLrc); resetPin(p);
    Serial.printf("  DIN=%d → %.3f\n", p, r);
    if (r > best) { best = r; ampDin = p; }
  }
  used[ampBclk] = used[ampLrc] = used[ampDin] = true;
  Serial.printf("  ✓ 擴大機：BCLK=%d LRC=%d DIN=%d\n", ampBclk, ampLrc, ampDin);
}

// ---- TFT：軟體 SPI 讀晶片 ID ----
int rem[16], nrem = 0;
uint64_t bbRead(int sck, int mosi, int miso, int cs, int dc, uint8_t cmd, int bits) {
  for (int i = 0; i < nrem; i++) if (rem[i] != miso) { pinMode(rem[i], OUTPUT); digitalWrite(rem[i], HIGH); }
  pinMode(miso, INPUT_PULLDOWN);
  digitalWrite(sck, LOW);
  if (cs >= 0) digitalWrite(cs, LOW);
  digitalWrite(dc, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(mosi, (cmd >> i) & 1);
    delayMicroseconds(1); digitalWrite(sck, HIGH); delayMicroseconds(1); digitalWrite(sck, LOW);
  }
  digitalWrite(dc, HIGH);
  uint64_t v = 0;
  for (int i = 0; i < bits; i++) {
    delayMicroseconds(1); digitalWrite(sck, HIGH); delayMicroseconds(1);
    v = (v << 1) | digitalRead(miso);
    digitalWrite(sck, LOW);
  }
  if (cs >= 0) digitalWrite(cs, HIGH);
  return v;
}
bool hasPattern(uint64_t v, int bits, uint32_t pat, int patBits) {
  uint32_t mask = (1UL << patBits) - 1;
  for (int s = 0; s + patBits <= bits; s++) if (((v >> s) & mask) == pat) return true;
  return false;
}

void stepTft() {
  Serial.println("\n[4] 找 TFT（讀 ST7789 / ILI9341 晶片 ID，需要 SDO/MISO 有接）");
  nrem = 0;
  for (int i = 0; i < NCAND; i++) if (!used[CAND[i]]) rem[nrem++] = CAND[i];
  Serial.print("  剩餘腳位：");
  for (int i = 0; i < nrem; i++) Serial.printf("%d ", rem[i]);
  Serial.println();

  for (int a = 0; a < nrem; a++) for (int b = 0; b < nrem; b++) for (int c = 0; c < nrem; c++)
  for (int d = 0; d < nrem; d++) for (int e = -1; e < nrem; e++) {
    int sck = rem[a], mosi = rem[b], miso = rem[c], dc = rem[d], cs = e < 0 ? -1 : rem[e];
    if (sck == mosi || sck == miso || sck == dc || mosi == miso || mosi == dc || miso == dc) continue;
    if (cs == sck || cs == mosi || cs == miso || cs == dc) continue;
    uint64_t st = bbRead(sck, mosi, miso, cs, dc, 0x04, 32);   // ST7789 RDDID → 85 85 52
    uint64_t il = bbRead(sck, mosi, miso, cs, dc, 0xD3, 40);   // ILI9341 RDID4 → 00 93 41
    const char *type = hasPattern(st, 32, 0x8552, 16) ? "ST7789" : hasPattern(il, 40, 0x9341, 16) ? "ILI9341" : nullptr;
    if (type) {
      Serial.printf("  ✓ TFT %s：SCK=%d MOSI=%d MISO=%d DC=%d CS=%d\n", type, sck, mosi, miso, dc, cs);
      Serial.print("    其它剩餘腳位（可能是 RST / 背光）：");
      for (int i = 0; i < nrem; i++) {
        int p = rem[i];
        if (p != sck && p != mosi && p != miso && p != dc && p != cs) Serial.printf("%d ", p);
      }
      Serial.println();
      for (int i = 0; i < nrem; i++) resetPin(rem[i]);
      return;
    }
  }
  for (int i = 0; i < nrem; i++) resetPin(rem[i]);
  Serial.println("  ✗ 讀不到 TFT ID（可能 SDO 沒接，需要用畫面測試）");
}

void stepButton() {
  Serial.println("\n[5] 按鍵測試：請在 15 秒內按幾下按鍵");
  int last[49];
  for (int i = 0; i < NCAND; i++) { pinMode(CAND[i], INPUT); last[CAND[i]] = digitalRead(CAND[i]); }
  pinMode(0, INPUT_PULLUP); last[0] = digitalRead(0);
  uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    for (int i = -1; i < NCAND; i++) {
      int p = i < 0 ? 0 : CAND[i];
      if (p == micSd) continue;
      int v = digitalRead(p);
      if (v != last[p]) { Serial.printf("  GPIO%d → %d\n", p, v); last[p] = v; }
    }
    delay(5);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n===== PinFinder 開始 =====");
  stepPassive();
  stepMic();
  stepAmp();
  stepTft();
  stepButton();
  Serial.println("\n===== PinFinder 結束 =====");
}

void loop() { delay(1000); }
