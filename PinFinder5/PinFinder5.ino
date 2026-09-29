// PinFinder5 — 驗證麥克風 SCK/WS/SD，再用麥克風找擴大機（共用或獨立 I2S）
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

const int CAND[] = {1, 2, 3, 14, 19, 20, 21, 41, 42, 45, 46, 47, 48};
const int NCAND = sizeof(CAND) / sizeof(int);
int mSck, mWs, mSd = 42, mSlot = 0;

i2s_chan_handle_t rx = nullptr, tx = nullptr;
void resetPin(int p) { gpio_reset_pin((gpio_num_t)p); pinMode(p, INPUT); }

i2s_std_config_t cfg(int bclk, int ws, int dout, int din, i2s_data_bit_width_t bits) {
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)bclk;
  sc.gpio_cfg.ws = (gpio_num_t)ws;
  sc.gpio_cfg.dout = dout < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)dout;
  sc.gpio_cfg.din = din < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)din;
  return sc;
}
void closeAll() {
  if (tx) { i2s_channel_disable(tx); i2s_del_channel(tx); tx = nullptr; }
  if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); rx = nullptr; }
  for (int i = 0; i < NCAND; i++) resetPin(CAND[i]);
}
// 只開麥克風（I2S0）
void openMic() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&cc, nullptr, &rx);
  i2s_std_config_t sc = cfg(mSck, mWs, -1, mSd, I2S_DATA_BIT_WIDTH_32BIT);
  i2s_channel_init_std_mode(rx, &sc);
  i2s_channel_enable(rx);
}
// 擴大機獨立在 I2S1
bool openAmp(int bclk, int lrc, int din) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &tx, nullptr) != ESP_OK) return false;
  i2s_std_config_t sc = cfg(bclk, lrc, din, -1, I2S_DATA_BIT_WIDTH_32BIT);
  if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK) return false;
  return i2s_channel_enable(tx) == ESP_OK;
}
// 麥克風與擴大機共用 I2S0 全雙工
bool openDuplex(int din) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &tx, &rx) != ESP_OK) return false;
  i2s_std_config_t sc = cfg(mSck, mWs, din, mSd, I2S_DATA_BIT_WIDTH_32BIT);
  i2s_channel_init_std_mode(tx, &sc);
  i2s_channel_init_std_mode(rx, &sc);
  i2s_channel_enable(tx);
  i2s_channel_enable(rx);
  return true;
}

int32_t S[2][2048];
// 回傳各聲道 rms 與 1kHz 比例
void capture(bool tone, float rms[2], float ratio[2]) {
  static int32_t t[256 * 2];
  for (int i = 0; i < 256; i++) t[i * 2] = t[i * 2 + 1] = tone ? (int32_t)(0.3 * 2147483647.0 * sin(2 * PI * 1000.0 * i / 16000.0)) : 0;
  int32_t raw[256 * 2]; size_t got; int n = 0, skip = 0;
  uint32_t t0 = millis();
  while (n < 2048 && millis() - t0 < 2000) {
    if (tx) i2s_channel_write(tx, t, sizeof(t), &got, 50);
    if (i2s_channel_read(rx, raw, sizeof(raw), &got, 100) != ESP_OK) continue;
    for (size_t i = 0; i < got / 8; i++) {
      if (skip < 4800) { skip++; continue; }
      if (n < 2048) { S[0][n] = raw[i * 2] >> 8; S[1][n] = raw[i * 2 + 1] >> 8; n++; }
    }
  }
  for (int ch = 0; ch < 2; ch++) {
    double mean = 0; for (int i = 0; i < n; i++) mean += S[ch][i]; mean /= max(n, 1);
    double c = 2 * cos(2 * PI * 1000.0 / 16000.0), s1 = 0, s2 = 0, tot = 0;
    for (int i = 0; i < n; i++) { double v = S[ch][i] - mean, s0 = v + c * s1 - s2; s2 = s1; s1 = s0; tot += v * v; }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    rms[ch] = sqrt(tot / max(n, 1));
    ratio[ch] = tot > 0 ? (float)(p / (tot * n / 2.0)) : 0;     // 純 1kHz ≈ 1
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n===== PinFinder5 開始 =====");
  float rms[2], ratio[2];

  mSck = 2; mWs = 1; mSd = 42; mSlot = 0;
  // 3) 獨立匯流排：I2S1 的 BCLK/LRC 組合，資料廣播到其它腳
  Serial.println("  [獨立 I2S] 測擴大機 BCLK/LRC：");
  for (int a = 0; a < NCAND; a++) for (int b = 0; b < NCAND; b++) {
    int bc = CAND[a], lr = CAND[b];
    if (bc == lr || bc == mSck || bc == mWs || bc == mSd || lr == mSck || lr == mWs || lr == mSd) continue;
    int first = -1;
    for (int i = 0; i < NCAND; i++) { int p = CAND[i]; if (p != bc && p != lr && p != mSck && p != mWs && p != mSd) { first = p; break; } }
    openMic();
    if (!openAmp(bc, lr, first)) { closeAll(); continue; }
    for (int i = 0; i < NCAND; i++) {
      int p = CAND[i];
      if (p == bc || p == lr || p == first || p == mSck || p == mWs || p == mSd) continue;
      esp_rom_gpio_pad_select_gpio(p);
      gpio_set_direction((gpio_num_t)p, GPIO_MODE_OUTPUT);
      esp_rom_gpio_connect_out_signal(p, I2S1O_SD_OUT_IDX, false, false);
    }
    delay(100);
    capture(true, rms, ratio);
    closeAll();
    if (ratio[mSlot] > 0.05) Serial.printf("    BCLK=%d LRC=%d → 1kHz 比例 %.3f rms %.0f\n", bc, lr, ratio[mSlot], rms[mSlot]);
  }
  Serial.println("===== PinFinder5 結束 =====");
}
void loop() { delay(1000); }
