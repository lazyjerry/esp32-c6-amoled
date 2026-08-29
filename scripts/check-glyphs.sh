#!/usr/bin/env bash
# 檢查 src/ 裡寫死的 UI 字串是否全都在子集字型的字集內。
# 漏一個字，畫面上就是一個豆腐方塊，而且要燒進去才看得到——用腳本在編譯前擋掉。
# 字集來源與 gen-font.sh 相同：腳本內的固定字串 + spiffs_content/charset.txt。
# 用法：./scripts/check-glyphs.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - "$ROOT" <<'PY'
import io, os, re, sys

root = sys.argv[1]

# gen-font.sh 裡的各組固定字串，變數名要和該腳本一致
sh = io.open(os.path.join(root, "scripts/gen-font.sh"), encoding="utf-8").read()
charset = ""
names = re.findall(r"^([A-Z_]+_CHARS)='", sh, re.M)
for name in names:
    m = re.search(name + r"='([^']*)'", sh)
    charset += m.group(1)

corpus = os.path.join(root, "spiffs_content/charset.txt")
if os.path.isfile(corpus):
    charset += io.open(corpus, encoding="utf-8").read()
else:
    print("⚠  找不到 spiffs_content/charset.txt，先跑 ./scripts/gen-content.sh", file=sys.stderr)

# gen-font.sh 的 -r 範圍：ASCII、度、全形空白、全形逗號
# -r 範圍直接從 gen-font.sh 的 gen font_zh_16 那行解析。硬編一份「已知的標點」
# 就是兩邊各走各的開始——全形括號漏掉過一次就是這樣來的
ranges = set()
m = re.search(r'gen font_zh_16[^\n]*"((?:-r [^"]*))"', sh)
if not m:
    print("✗ 讀不到 gen-font.sh 的 -r 範圍", file=sys.stderr)
    sys.exit(1)
for item in re.findall(r"-r (0x[0-9A-Fa-f]+)(?:-(0x[0-9A-Fa-f]+))?", m.group(1)):
    lo = int(item[0], 16)
    hi = int(item[1], 16) if item[1] else lo
    ranges.update(chr(c) for c in range(lo, hi + 1))

have = set(charset) | ranges

# 會把字串送上畫面的入口。**新增這類 helper 時要一起加進來**——
# 只認 lv_label_set_text 的話，經過自己寫的 section()／line() 的字串全都沒被檢查到；
# 反過來把所有字串都掃又會抓到一堆只進 log 的字（"左右晃動" 之類），
# 那些收進字型只是白白撐大檔案
UI_CALLS = (
    "lv_label_set_text", "lv_label_set_text_fmt",
    "section", "line",                                  # records_screen.c 自己的
    "cast_ui_set_prompt", "cast_ui_show_note",          # 擲筊畫面的提示與說明
    "error_screen_set",
)
pat = re.compile(r"\b(?:" + "|".join(UI_CALLS) + r")\s*\(([^;]*?)\)\s*;", re.S)
lit = re.compile(r'"((?:[^"\\]|\\.)*)"')

missing = {}
for dirpath, _dirs, files in os.walk(os.path.join(root, "src")):
    for fn in files:
        if not fn.endswith(".c"):
            continue
        path = os.path.join(dirpath, fn)
        for args in pat.findall(io.open(path, encoding="utf-8").read()):
            for text in lit.findall(args):
                text = text.replace("\\n", "").replace("\\t", "")
                for ch in text:
                    if ch not in have:
                        missing.setdefault(ch, set()).add(os.path.relpath(path, root))

if missing:
    print("✗ 這些字不在字型子集裡，畫面上會是豆腐方塊：", file=sys.stderr)
    for ch, where in sorted(missing.items()):
        print(f"    {ch}  U+{ord(ch):04X}  {', '.join(sorted(where))}", file=sys.stderr)
    print("  把它們加進 scripts/gen-font.sh 的對應 *_CHARS，再跑 ./scripts/gen-font.sh", file=sys.stderr)
    sys.exit(1)

print("✓ src/ 的 UI 字串全部在字型子集內")
PY
