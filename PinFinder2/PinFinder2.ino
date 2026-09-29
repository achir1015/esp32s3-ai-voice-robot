// PinFinder2 — 假設麥克風 SD=GPIO42，麥克風與擴大機共用 BCLK/WS（同一個 I2S 全雙工）
// 找出 BCLK、WS、擴大機 DIN。過程中喇叭會發出嗶聲。
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

const int CAND[] = {1, 2, 3, 14, 19, 20, 21, 41, 45, 46, 47, 48};
const int NCAND = sizeof(CAND) / sizeof(int);
const int MIC_SD = 42;

i2s_chan_handle_t txCh = nullptr, rxCh = nullptr;

void resetPin(int p) { gpio_reset_pin((gpio_num_t)p); pinMode(p, INPUT); }

bool startDuplex(int bclk, int ws, int dout) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &txCh, &rxCh) != ESP_OK) return false;
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)bclk;
  sc.gpio_cfg.ws = (gpio_num_t)ws;
  sc.gpio_cfg.dout = (gpio_num_t)dout;
  sc.gpio_cfg.din = (gpio_num_t)MIC_SD;
  if (i2s_channel_init_std_mode(txCh, &sc) != ESP_OK) return false;
  if (i2s_channel_init_std_mode(rxCh, &sc) != ESP_OK) return false;
  gpio_pulldown_en((gpio_num_t)MIC_SD);
  i2s_channel_enable(txCh);
  i2s_channel_enable(rxCh);
  return true;
}
void stopDuplex() {
  if (txCh) { i2s_channel_disable(txCh); i2s_del_channel(txCh); txCh = nullptr; }
  if (rxCh) { i2s_channel_disable(rxCh); i2s_del_channel(rxCh); rxCh = nullptr; }
  for (int i = 0; i < NCAND; i++) resetPin(CAND[i]);
  resetPin(MIC_SD);
}

float goertzel(const int32_t *x, int n) {
  double mean = 0;
  for (int i = 0; i < n; i++) mean += x[i];
  mean /= n;
  double c = 2 * cos(2 * PI * 1000.0 / 16000.0), s1 = 0, s2 = 0, tot = 0;
  for (int i = 0; i < n; i++) {
    double v = x[i] - mean, s0 = v + c * s1 - s2; s2 = s1; s1 = s0; tot += v * v;
  }
  double p = s1 * s1 + s2 * s2 - c * s1 * s2;
  return (float)(p / n) / (float)(tot / n * 2 + 1.0);
}

int32_t L[2048], R[2048];
float measure(bool tone, float *rmsOut) {
  static int32_t tx[256 * 2];
  for (int i = 0; i < 256; i++) {
    int32_t v = tone ? (int32_t)(0.35 * 2147483647.0 * sin(2 * PI * 1000.0 * i / 16000.0)) : 0;
    tx[i * 2] = tx[i * 2 + 1] = v;
  }
  int32_t raw[256 * 2];
  size_t got; int n = 0, skip = 0;
  uint32_t t0 = millis();
  while (n < 2048 && millis() - t0 < 1500) {
    i2s_channel_write(txCh, tx, sizeof(tx), &got, 50);
    if (i2s_channel_read(rxCh, raw, sizeof(raw), &got, 100) != ESP_OK) continue;
    for (size_t i = 0; i < got / 8; i++) {
      if (skip < 6000) { skip++; continue; }       // 麥克風/擴大機啟動
      if (n < 2048) { L[n] = raw[i * 2] >> 8; R[n] = raw[i * 2 + 1] >> 8; n++; }
    }
  }
  if (n < 512) return 0;
  float a = goertzel(L, n), b = goertzel(R, n);
  if (rmsOut) {
    double e = 0; const int32_t *x = a > b ? L : R;
    for (int i = 0; i < n; i++) e += (double)x[i] * x[i];
    *rmsOut = sqrt(e / n);
  }
  return max(a, b);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n===== PinFinder2 開始 =====");
  int bestB = -1, bestW = -1; float best = 0;

  // 1) BCLK/WS：資料同時送到其它所有腳
  for (int a = 0; a < NCAND; a++) for (int b = 0; b < NCAND; b++) {
    int bclk = CAND[a], ws = CAND[b];
    if (bclk == ws) continue;
    int first = -1;
    for (int i = 0; i < NCAND; i++) if (CAND[i] != bclk && CAND[i] != ws) { first = CAND[i]; break; }
    if (!startDuplex(bclk, ws, first)) { stopDuplex(); continue; }
    for (int i = 0; i < NCAND; i++) {
      int p = CAND[i];
      if (p == bclk || p == ws || p == first) continue;
      esp_rom_gpio_pad_select_gpio(p);
      gpio_set_direction((gpio_num_t)p, GPIO_MODE_OUTPUT);
      esp_rom_gpio_connect_out_signal(p, I2S0O_SD_OUT_IDX, false, false);
    }
    float rms;
    float r = measure(true, &rms);
    stopDuplex();
    if (r > 0.15) Serial.printf("  BCLK=%d WS=%d → 1kHz 比例 %.3f rms %.0f\n", bclk, ws, r, rms);
    if (r > best) { best = r; bestB = bclk; bestW = ws; }
  }
  Serial.printf("最佳 BCLK=%d WS=%d（%.3f）\n", bestB, bestW, best);
  if (best < 0.15) { Serial.println("✗ 共用匯流排假設不成立"); Serial.println("===== PinFinder2 結束 ====="); return; }

  // 2) DIN
  int bestD = -1; best = 0;
  for (int i = 0; i < NCAND; i++) {
    int p = CAND[i];
    if (p == bestB || p == bestW) continue;
    if (!startDuplex(bestB, bestW, p)) { stopDuplex(); continue; }
    float r = measure(true, nullptr);
    stopDuplex();
    Serial.printf("  DIN=%d → %.3f\n", p, r);
    if (r > best) { best = r; bestD = p; }
  }
  // 3) 麥克風聲道與無聲時的底噪
  startDuplex(bestB, bestW, bestD);
  float rms; measure(false, &rms);
  stopDuplex();
  Serial.printf("✓ 結果：BCLK=%d WS=%d AMP_DIN=%d MIC_SD=%d（安靜時 rms=%.0f）\n", bestB, bestW, bestD, MIC_SD, rms);
  Serial.println("===== PinFinder2 結束 =====");
}

void loop() { delay(1000); }
