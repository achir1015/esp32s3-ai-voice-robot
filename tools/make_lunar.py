# 產生 AI_Voice_Robot/lunar_table.h：2000~2098 年農曆資料（最後一列只當 2099 年正月初一的邊界）
# 用法：pip install lunardate && python make_lunar.py
import datetime, os
from lunardate import LunarDate

BASE = datetime.date(2000, 1, 1)
rows = []
for y in range(2000, 2099):
    start = LunarDate(y, 1, 1).to_solar_date()
    end = LunarDate(y + 1, 1, 1).to_solar_date()
    months = []          # [(month, isLeap, days)]
    d = start
    while d < end:
        ld = LunarDate.from_solar_date(d.year, d.month, d.day)
        key = (ld.month, ld.isLeapMonth)
        if not months or tuple(months[-1][:2]) != key:
            months.append([ld.month, ld.isLeapMonth, 0])
        months[-1][2] += 1
        d += datetime.timedelta(days=1)
    leap = next((m for m, lp, _ in months if lp), 0)
    bits = 0
    for i, (_, _, n) in enumerate(months):   # 依實際順序（閏月緊接在該月之後），大月=1
        assert n in (29, 30)
        if n == 30:
            bits |= 1 << i
    rows.append(((start - BASE).days, bits, leap))
rows.append(((LunarDate(2099, 1, 1).to_solar_date() - BASE).days, 0, 0))   # 邊界

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'AI_Voice_Robot', 'lunar_table.h')
with open(out, 'w', encoding='utf-8') as f:
    f.write('// 自動產生：tools/make_lunar.py（資料來源 python lunardate）\n')
    f.write('// 每年：{ 農曆正月初一距 2000-01-01 的天數, 各月大小(bit i=第 i 個月，1=30天), 閏幾月(0=無) }\n')
    f.write('#pragma once\n#include <stdint.h>\n\n#define LUNAR_FIRST_YEAR 2000\n#define LUNAR_YEARS %d\n' % len(rows))
    f.write('static const struct { uint16_t cny; uint16_t bits; uint8_t leap; } LUNAR[LUNAR_YEARS] = {\n')
    for i, (c, b, l) in enumerate(rows):
        f.write('  {%5d, 0x%04X, %2d},  // %d\n' % (c, b, l, 2000 + i))
    f.write('};\n')
print('ok', out)
