#!/usr/bin/env python3
"""產生 S4 驗證用的全螢幕測試圖（368x448 RGB565）。

用真圖而非小圖重複送：全螢幕背景要量的是「從 flash 讀 330KB 再經 QSPI 送出」的成本，
拿小圖重複送會被 flash 快取命中，量出來的數字偏樂觀。

面板吃 byte-swap 過的 RGB565（紅是 0x00F8 不是 0xF800），這裡先 swap 好，
量測時就不必在迴圈裡轉換，免得把 CPU 時間算進傳輸成本。

用法：python3 scripts/gen-testcard.py > src/testcard.c
"""
import sys

W, H = 368, 448


def rgb565_swapped(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v & 0xFF) << 8) | (v >> 8)   # byte-swap


def main():
    out = sys.stdout
    out.write("// 由 scripts/gen-testcard.py 產生，不要手改。\n")
    out.write("// S4 驗證用的全螢幕測試圖：368x448 RGB565（已 byte-swap）。\n")
    out.write("// 橫向漸層加縱向色帶，畫面撕裂、位移或錯色都看得出來。\n")
    out.write('#include "testcard.h"\n\n')
    out.write("const uint16_t testcard_368x448[%d] = {\n" % (W * H))

    vals = []
    for y in range(H):
        band = (y // 56) % 8          # 8 條橫帶
        for x in range(W):
            t = x * 255 // (W - 1)     # 橫向漸層
            r = t if band & 1 else 255 - t
            g = (t * 2) % 256 if band & 2 else 128
            b = 255 - t if band & 4 else t
            vals.append(rgb565_swapped(r, g, b))

    for i in range(0, len(vals), 12):
        out.write("    " + ",".join("0x%04X" % v for v in vals[i:i + 12]) + ",\n")
    out.write("};\n")


if __name__ == "__main__":
    main()
