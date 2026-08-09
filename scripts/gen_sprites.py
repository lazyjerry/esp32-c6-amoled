#!/usr/bin/env python3
"""產生筊杯的 LVGL ARGB8888 圖檔。由 scripts/gen-sprites.sh 呼叫，不單獨使用。

外形是半月：一個圓被另一個半徑大很多的圓切掉上緣，所以內緣幾乎是直的、只微微內凹，
兩端再用圓角相交把尖角磨鈍——實物是車出來再磨過的木塊，不是薄月牙。

平面朝上與凸面朝上共用同一個外形，靠三件事分辨：
  平面是磨開的斷面，顏色較淺、幾乎沒有高光，只有一圈倒角；
  凸面是拋光過的弧面，顏色較深，沿脊線有一道寬而柔的木質光澤。
"""
import argparse
import math

from PIL import Image

SS = 4          # 超取樣倍率，縮回去就是抗鋸齒

R_OUT = 0.5      # 外圓，就是筊的圓弧那一側
R_IN = 1.20      # 切掉上緣的圓，半徑越大內緣越平
THICK = 0.42     # 中央最厚處，佔單位正方形的比例
ROUND_K = 0.038  # 兩端的圓角半徑，越大端點越鈍
ASPECT = 0.53    # 成品高寬比。俯視不吃透視，就是半月本身的長寬比

BULGE = 0.135   # 凸面的隆起高度（單位正方形座標）
BEVEL = 0.15    # 平面版的倒角寬度，佔厚度比例
GRAIN = 0.030   # 木紋深淺。太重會看起來像等高線而不是木頭

LIGHT = (-0.45, -0.75, 0.55)

C_IN_Y = 1.0 - THICK - R_IN


def _unit(x, y, z=0.0):
    n = math.sqrt(x * x + y * y + z * z) or 1.0
    return x / n, y / n, z / n


LX, LY, LZ = _unit(*LIGHT)
# Blinn-Phong 的半角向量，視線固定 (0,0,1)
HX, HY, HZ = _unit(LX, LY, LZ + 1.0)


def _grain(sx, t):
    """木紋。紋路沿長邊延伸，所以變化主要在厚度方向，沿長邊只加擾動免得太規律。"""
    w = t * 5.5 + 0.8 * math.sin(sx * 5.1 + 1.7) + 0.3 * math.sin(sx * 13.0 + 0.4)
    return math.sin(2.0 * math.pi * w) * 0.5 + math.sin(2.0 * math.pi * (w * 3.3 + 0.3)) * 0.35


def _shade(u, v, sx, convex, y_gain):
    """回傳 (亮度, 邊界距離)。u,v 為單位正方形座標，sx 是沿長邊的 0~1 位置。"""
    dx_o, dy_o = u - 0.5, v - 0.5
    dx_i, dy_i = u - 0.5, v - C_IN_Y
    r_o = math.hypot(dx_o, dy_o)
    r_i = math.hypot(dx_i, dy_i)

    b = R_OUT - r_o        # 進入外圓的深度
    a = r_i - R_IN         # 離開內圓的深度

    # 圓角化的交集：兩條邊界相交的尖角會被磨成半徑 ROUND_K 的圓弧
    ex = max(ROUND_K - b, 0.0)
    ey = max(ROUND_K - a, 0.0)
    sdf = min(max(-b, -a), 0.0) + math.hypot(ex, ey) - ROUND_K

    s = a + b
    t = 0.0 if s <= 0.0 else min(1.0, max(0.0, a / s))

    # ∇t：外形邊界都是圓，梯度就是徑向單位向量
    gix, giy, _ = _unit(dx_i, dy_i)
    gox, goy, _ = _unit(-dx_o, -dy_o)
    inv = 1.0 / (s * s) if s > 1e-6 else 0.0
    tx = (b * gix - a * gox) * inv
    ty = (b * giy - a * goy) * inv
    gx, gy, _ = _unit(tx, ty)   # 由內緣指向外緣

    if convex:
        slope = BULGE * math.pi * math.cos(math.pi * t)
        nx, ny, nz = _unit(-slope * tx, -slope * ty * y_gain, 1.0)
    else:
        if t < BEVEL:
            tilt = -1.15 * (BEVEL - t) / BEVEL
        elif t > 1.0 - BEVEL:
            tilt = 1.15 * (t - (1.0 - BEVEL)) / BEVEL
        else:
            tilt = 0.0
        nx, ny, nz = _unit(tilt * gx, tilt * gy * y_gain, 1.0)

    lam = max(0.0, nx * LX + ny * LY + nz * LZ)
    spec = max(0.0, nx * HX + ny * HY + nz * HZ)

    if convex:
        # 拋光木頭的光澤寬而柔，不是塑膠那種細小亮點
        light = 0.26 + 0.66 * lam + 0.24 * spec ** 12
    else:
        light = 0.32 + 0.52 * lam + 0.05 * spec ** 30

    light *= 1.0 + GRAIN * _grain(sx, t)

    # 貼著邊界壓暗，等於幫外形描一圈輪廓
    edge = -sdf
    if 0.0 < edge < 0.008:
        light *= 0.58 + 0.42 * (edge / 0.008)

    return light, sdf


def render(width, rgb, convex):
    # 兩圓的交點決定外框；上緣被 R_IN 切掉，剩下的就是半月
    y_tip = (C_IN_Y * C_IN_Y - R_IN * R_IN) / (2.0 * C_IN_Y - 1.0)
    half_w = math.sqrt(R_OUT * R_OUT - (y_tip - 0.5) ** 2)
    box_w, box_h = 2.0 * half_w, 1.0 - y_tip

    height = int(round(width * ASPECT))
    # 高寬比和幾何比不同時 y 方向被壓縮，同樣的高低差對應更陡的斜率
    y_gain = (box_h / box_w) / (float(height) / width)

    x0 = 0.5 - half_w
    w_ss, h_ss = width * SS, height * SS
    du = box_w / w_ss
    dv = box_h / h_ss

    buf = bytearray(w_ss * h_ss * 4)
    r0, g0, b0 = rgb
    k = 0
    for j in range(h_ss):
        v = y_tip + (j + 0.5) * dv
        for i in range(w_ss):
            u = x0 + (i + 0.5) * du
            light, sdf = _shade(u, v, (i + 0.5) / w_ss, convex, y_gain)
            buf[k] = min(255, int(r0 * light))
            buf[k + 1] = min(255, int(g0 * light))
            buf[k + 2] = min(255, int(b0 * light))
            buf[k + 3] = 255 if sdf < 0.0 else 0
            k += 4

    img = Image.frombytes("RGBA", (w_ss, h_ss), bytes(buf))
    return img.resize((width, height), Image.BOX)


def emit(path, name, img):
    w, h = img.size
    data = img.tobytes()   # RGBA；LVGL 的 ARGB8888 在小端是 B,G,R,A
    out = bytearray(len(data))
    for i in range(0, len(data), 4):
        out[i] = data[i + 2]
        out[i + 1] = data[i + 1]
        out[i + 2] = data[i]
        out[i + 3] = data[i + 3]

    lines = []
    for i in range(0, len(out), 16):
        lines.append("    " + " ".join("0x%02X," % byte for byte in out[i:i + 16]))

    with open(path, "w") as fh:
        fh.write("// 由 scripts/gen-sprites.sh 產生，請勿手動編輯\n")
        fh.write('#include "lvgl.h"\n\n')
        fh.write("static const uint8_t %s_map[] = {\n%s\n};\n\n" % (name, "\n".join(lines)))
        fh.write("const lv_image_dsc_t %s = {\n" % name)
        fh.write("    .header = {\n")
        fh.write("        .magic = LV_IMAGE_HEADER_MAGIC,\n")
        fh.write("        .cf = LV_COLOR_FORMAT_ARGB8888,\n")
        fh.write("        .flags = 0,\n")
        fh.write("        .w = %d,\n        .h = %d,\n" % (w, h))
        fh.write("        .stride = %d,\n" % (w * 4))
        fh.write("        .reserved_2 = 0,\n")
        fh.write("    },\n")
        fh.write("    .data_size = sizeof(%s_map),\n" % name)
        fh.write("    .data = %s_map,\n};\n" % name)
    print("產生 %s（%dx%d, %d bytes）" % (name, w, h, len(out)))


def wood_colours(base):
    """凸面是拋光過的表皮，用原色；平面是磨開的斷面，比較淺也比較不飽和。"""
    flat = tuple(min(255, int(c * f)) for c, f in zip(base, (1.16, 1.24, 1.30)))
    return flat, base


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--width", type=int, default=132)
    ap.add_argument("--color", default="8A4028", help="紅木底色，凸面用；平面由它推出較淺的版本")
    args = ap.parse_args()

    c = args.color.lstrip("#")
    base = (int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16))
    flat_rgb, round_rgb = wood_colours(base)

    emit("%s/blk_flat.c" % args.outdir, "blk_flat", render(args.width, flat_rgb, False))
    emit("%s/blk_round.c" % args.outdir, "blk_round", render(args.width, round_rgb, True))


if __name__ == "__main__":
    main()
