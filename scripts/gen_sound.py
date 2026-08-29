#!/usr/bin/env python3
"""合成韌體內建的音效。由 scripts/gen-sound.sh 呼叫，不單獨使用。

兩種音都是「一段極短的寬頻起音（接觸的瞬態）+ 幾個指數衰減的共振模態」：
筊是木頭互敲，能量集中在低中頻、衰減很快；鐘是銅體，模態彼此不成整數倍、拖得久。
用固定亂數種子，重跑產出的位元完全相同，diff 才有意義。
"""
import argparse
import math
import random
import struct

DURATION = 0.14
# (頻率 Hz, 衰減時間常數 s, 權重)。木頭落在地板上，能量集中在低中頻，
# 而且衰減很快；高頻若拖太久或比例太高，聽起來就變成金屬。
MODES = ((188.0, 0.048, 0.90), (312.0, 0.030, 0.62), (620.0, 0.017, 0.42), (935.0, 0.010, 0.26))
CLICK_DECAY = 0.0045    # 接觸瞬間的脆聲，短到只是一個「叩」的頭
CLICK_LEVEL = 0.55
BODY_DECAY = 0.014      # 悶掉的寬頻成分，撐出木頭的厚度
BODY_LEVEL = 0.42
ATTACK = 0.0015
PEAK = 0.72             # 留餘裕，避免 codec 端再加增益就削頂

BELL_DURATION = 1.10
# 鐘的模態刻意不成整數倍——成整數倍就變成有音高的樂器音，不像鐘。
# 高模態衰減得比低模態快，那是「噹」之後只剩嗡嗡尾音的來源
BELL_MODES = ((523.0, 0.62, 1.00), (1046.0, 0.40, 0.52), (1263.0, 0.30, 0.44),
              (1580.0, 0.22, 0.30), (2355.0, 0.13, 0.18), (3140.0, 0.08, 0.10))
BELL_STRIKE_DECAY = 0.006   # 槌頭打上去的那一下
BELL_STRIKE_LEVEL = 0.40
BELL_ATTACK = 0.0012


def synth(rate):
    rng = random.Random(20260809)
    n = int(rate * DURATION)
    # 兩層噪音：亮的那層是接觸的脆聲，悶的那層是木頭本體。
    # 全頻寬白噪音聽起來像電流雜訊，兩層都先過一階低通
    click, body, p1, p2 = [], [], 0.0, 0.0
    for _ in range(n):
        white = rng.uniform(-1.0, 1.0)
        p1 = 0.35 * p1 + 0.65 * white
        p2 = 0.84 * p2 + 0.16 * white
        click.append(p1)
        body.append(p2)

    samples = []
    for i in range(n):
        t = i / rate
        v = CLICK_LEVEL * click[i] * math.exp(-t / CLICK_DECAY)
        v += BODY_LEVEL * body[i] * math.exp(-t / BODY_DECAY)
        for freq, tau, weight in MODES:
            v += weight * math.exp(-t / tau) * math.sin(2.0 * math.pi * freq * t)
        if t < ATTACK:
            v *= t / ATTACK                     # 沒有這段起音會有一聲爆音
        v = math.tanh(1.4 * v)                  # 輕微軟削，讓瞬態更結實
        samples.append(v)

    peak = max(abs(s) for s in samples) or 1.0
    gain = PEAK * 32767.0 / peak
    return [max(-32768, min(32767, int(s * gain))) for s in samples]


def synth_bell(rate):
    rng = random.Random(20260829)
    n = int(rate * BELL_DURATION)

    strike, p = [], 0.0
    for _ in range(n):
        p = 0.30 * p + 0.70 * rng.uniform(-1.0, 1.0)
        strike.append(p)

    samples = []
    for i in range(n):
        t = i / rate
        v = BELL_STRIKE_LEVEL * strike[i] * math.exp(-t / BELL_STRIKE_DECAY)
        for freq, tau, weight in BELL_MODES:
            v += weight * math.exp(-t / tau) * math.sin(2.0 * math.pi * freq * t)
        if t < BELL_ATTACK:
            v *= t / BELL_ATTACK
        samples.append(v)

    peak = max(abs(s) for s in samples) or 1.0
    gain = PEAK * 32767.0 / peak
    return [max(-32768, min(32767, int(s * gain))) for s in samples]


def emit(outdir, name, samples, rate):
    lines = []
    for i in range(0, len(samples), 12):
        lines.append("    " + " ".join("%6d," % s for s in samples[i:i + 12]))

    with open("%s/%s.c" % (outdir, name), "w") as fh:
        fh.write("// 由 scripts/gen-sound.sh 產生，請勿手動編輯\n")
        fh.write('#include "sounds.h"\n\n')
        fh.write("const int16_t %s[] = {\n%s\n};\n\n" % (name, "\n".join(lines)))
        fh.write("const size_t %s_len = sizeof(%s) / sizeof(%s[0]);\n" % (name, name, name))

    print("產生 %s（%d 取樣 @ %d Hz, %d bytes）" % (name, len(samples), rate, len(samples) * 2))


# 標頭一次寫齊。每個音各自覆寫的話，最後一個產生的音會把前面的宣告洗掉
def emit_header(outdir, names, rate):
    with open("%s/sounds.h" % outdir, "w") as fh:
        fh.write("// 由 scripts/gen-sound.sh 產生，請勿手動編輯\n#pragma once\n\n")
        fh.write("#include <stddef.h>\n#include <stdint.h>\n\n")
        fh.write("#define SND_SAMPLE_RATE %d\n\n" % rate)
        for name in names:
            fh.write("extern const int16_t %s[];\nextern const size_t %s_len;\n" % (name, name))


def write_wav(path, samples, rate):
    data = struct.pack("<%dh" % len(samples), *samples)
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        fh.write(struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
        fh.write(b"data" + struct.pack("<I", len(data)) + data)
    print("試聽檔：%s" % path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--rate", type=int, default=16000)
    ap.add_argument("--preview")
    args = ap.parse_args()

    clack = synth(args.rate)
    bell = synth_bell(args.rate)
    emit(args.outdir, "snd_clack", clack, args.rate)
    emit(args.outdir, "snd_bell", bell, args.rate)
    emit_header(args.outdir, ("snd_clack", "snd_bell"), args.rate)
    if args.preview:
        write_wav(args.preview, clack, args.rate)
        write_wav(args.preview.replace(".wav", "-bell.wav"), bell, args.rate)


if __name__ == "__main__":
    main()
