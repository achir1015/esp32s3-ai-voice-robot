// 自動產生：tools/make_font.py，來源 GNU Unifont（GPL-2.0+ w/ font embedding exception / OFL-1.1）
// 16px 點陣字，全形 16x16、半形 8x16。每字 32 bytes（半形只用每列左邊 1 byte）
#pragma once
#include <stdint.h>

#define CJK_FONT_COUNT 22351
#ifdef __cplusplus
extern "C" {
#endif
extern const uint16_t cjkCodes[CJK_FONT_COUNT];
extern const uint8_t cjkWide[(CJK_FONT_COUNT + 7) / 8];
extern const uint8_t cjkBitmaps[CJK_FONT_COUNT * 32];
#ifdef __cplusplus
}
#endif
