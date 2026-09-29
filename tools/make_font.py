# 由 GNU Unifont (.hex) 產生 AI_Voice_Robot/cjk_font.h + cjk_font.c
# 用法：python make_font.py unifont_all-18.0.01.hex
# 字型授權：GNU Unifont — GPL-2.0-or-later with font embedding exception / SIL OFL 1.1
import sys, os

RANGES = [
    (0x0020, 0x007E),   # ASCII
    (0x00A0, 0x00FF),   # Latin-1（°、× 等）
    (0x2000, 0x206F),   # 一般標點（“ ” … —）
    (0x2100, 0x218F),   # 字母符號（℃ 等）
    (0x2190, 0x21FF),   # 箭頭
    (0x2460, 0x24FF),   # 圈字數字
    (0x25A0, 0x25FF),   # 幾何符號
    (0x3000, 0x303F),   # CJK 標點（，。「」）
    (0x3040, 0x30FF),   # 日文假名
    (0x3100, 0x312F),   # 注音
    (0x4E00, 0x9FFF),   # CJK 統一漢字（繁簡都有）
    (0xFF00, 0xFFEF),   # 全形符號（，！？：）
]

src = sys.argv[1]
outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'AI_Voice_Robot')

glyphs = {}
for line in open(src, encoding='ascii'):
    cp, bits = line.strip().split(':')
    cp = int(cp, 16)
    if any(a <= cp <= b for a, b in RANGES):
        glyphs[cp] = bits
codes = sorted(glyphs)

wbits = bytearray((len(codes) + 7) // 8)       # 每字 1 bit：1 = 全形 16px
bitmap = bytearray()
for i, c in enumerate(codes):
    h = glyphs[c]
    if len(h) == 64:
        wbits[i // 8] |= 1 << (i % 8)
    else:                                      # 8x16 → 每列補一個 0 byte
        h = ''.join(h[j:j + 2] + '00' for j in range(0, 32, 2))
    bitmap += bytes.fromhex(h)

NL = '\n'
BS = '\\'


def cstr(data):
    # 用字串常數而非數字列表，編譯快很多
    return NL.join('"' + ''.join(BS + 'x%02x' % b for b in data[i:i + 64]) + '"'
                   for i in range(0, len(data), 64))


header = [
    '// 自動產生：tools/make_font.py，來源 GNU Unifont（GPL-2.0+ w/ font embedding exception / OFL-1.1）',
    '// 16px 點陣字，全形 16x16、半形 8x16。每字 32 bytes（半形只用每列左邊 1 byte）',
    '#pragma once',
    '#include <stdint.h>',
    '',
    '#define CJK_FONT_COUNT %d' % len(codes),
    '#ifdef __cplusplus',
    'extern "C" {',
    '#endif',
    'extern const uint16_t cjkCodes[CJK_FONT_COUNT];',
    'extern const uint8_t cjkWide[(CJK_FONT_COUNT + 7) / 8];',
    'extern const uint8_t cjkBitmaps[CJK_FONT_COUNT * 32];',
    '#ifdef __cplusplus',
    '}',
    '#endif',
    '',
]
open(os.path.join(outdir, 'cjk_font.h'), 'w', encoding='utf-8').write(NL.join(header))

with open(os.path.join(outdir, 'cjk_font.c'), 'w', encoding='utf-8') as f:
    f.write('// 自動產生：tools/make_font.py（GNU Unifont 16px 點陣資料）' + NL)
    f.write('#include "cjk_font.h"' + NL + NL)
    f.write('const uint16_t cjkCodes[CJK_FONT_COUNT] = {' + NL)
    for i in range(0, len(codes), 16):
        f.write(','.join('0x%04X' % c for c in codes[i:i + 16]) + ',' + NL)
    f.write('};' + NL + NL)
    f.write('const uint8_t cjkWide[(CJK_FONT_COUNT + 7) / 8] =' + NL + cstr(wbits) + ';' + NL + NL)
    f.write('const uint8_t cjkBitmaps[CJK_FONT_COUNT * 32] =' + NL + cstr(bitmap) + ';' + NL)

print('glyphs:', len(codes), '->', os.path.abspath(outdir))
