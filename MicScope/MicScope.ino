// MicScope — 麥克風診斷：判斷雜音來源（接線不良 / 電氣干擾 / 真實聲音）
// INMP441 為 24-bit 資料放在 32-bit 左對齊，最低 8 位元應該全是 0；若亂跳 = 資料線接觸不良
#include <Arduino.h>
#include "driver/i2s_std.h"

const int SCK_PIN = 2, WS_PIN = 1, SD_PIN = 42;
i2s_chan_handle_t rx;

void setup() {
  Serial.begin(115200);
  delay(1500);
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&cc, nullptr, &rx);
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)SCK_PIN;
  sc.gpio_cfg.ws = (gpio_num_t)WS_PIN;
  sc.gpio_cfg.dout = I2S_GPIO_UNUSED;
  sc.gpio_cfg.din = (gpio_num_t)SD_PIN;
  i2s_channel_init_std_mode(rx, &sc);
  i2s_channel_enable(rx);
  Serial.println("\n===== MicScope 開始（請保持安靜 20 秒）=====");
}

int32_t raw[512 * 2];
int16_t x[512];
uint32_t t0 = millis();

void loop() {
  size_t got;
  i2s_channel_read(rx, raw, sizeof(raw), &got, 1000);
  int n = got / 8;
  int lowNonZero = 0, rightNonZero = 0;
  double mean = 0;
  for (int i = 0; i < n; i++) {
    if (raw[i * 2] & 0xFF) lowNonZero++;
    if (raw[i * 2 + 1]) rightNonZero++;
    x[i] = raw[i * 2] >> 14;
    mean += x[i];
  }
  mean /= n;
  double e = 0; int zc = 0, peak = 0;
  for (int i = 0; i < n; i++) {
    double v = x[i] - mean;
    e += v * v;
    peak = max(peak, (int)fabs(v));
    if (i && ((x[i] - mean) > 0) != ((x[i - 1] - mean) > 0)) zc++;
  }
  // 60Hz / 120Hz / 1kHz 能量（Goertzel）
  auto g = [&](double f) {
    double c = 2 * cos(2 * PI * f / 16000), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s0 = (x[i] - mean) + c * s1 - s2; s2 = s1; s1 = s0; }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
  };
  static int line = 0;
  if (line++ % 4 == 0)
    Serial.printf("rms=%6.0f peak=%6d 過零=%3d/%d 低8位非0=%3d/%d 右聲道非0=%3d | 60Hz=%5.0f 120Hz=%5.0f 1k=%5.0f\n",
                  sqrt(e / n), peak, zc, n, lowNonZero, n, rightNonZero, g(60), g(120), g(1000));
  if (millis() - t0 > 20000) { Serial.println("===== MicScope 結束 ====="); while (true) delay(1000); }
}
