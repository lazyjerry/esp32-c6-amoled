#!/usr/bin/env bash
# 從 data/ 的原始語料產生 storage 分區的 SPIFFS 內容與字集檔。
# 產物在 spiffs_content/，不進版控也不要手改；要改就改 data/ 再重跑。
# 用法：./scripts/gen-content.sh [--check-only]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/data"
OUT="$ROOT/spiffs_content"
CHECK_ONLY=0

while [ $# -gt 0 ]; do
    case "$1" in
        --check-only) CHECK_ONLY=1; shift ;;
        -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf '未知參數：%s\n' "$1" >&2; exit 1 ;;
    esac
done

[ -f "$SRC/poems.json" ] || { printf '找不到 %s\n' "$SRC/poems.json" >&2; exit 1; }

python3 - "$SRC" "$OUT" "$CHECK_ONLY" <<'PY'
import json, io, os, re, sys

src, out, check_only = sys.argv[1], sys.argv[2], sys.argv[3] == "1"

# 允許 ASCII、全形標點、CJK 漢字。任何落在這之外的都當成產出污染擋下來——
# AI 產的語料實際出現過混入西里爾字母的情況，肉眼很難在幾百句裡發現
ALLOWED = re.compile(r'[\x00-\x7F  -⁯■-◿　-〿＀-￯一-鿿]')

def scan_bad(text, where):
    bad = sorted(set(c for c in text if not ALLOWED.match(c)))
    if bad:
        sys.exit(f"✗ {where} 含可疑字元 {bad}——疑似產出污染，中止")

poems_doc = json.load(io.open(os.path.join(src, "poems.json"), encoding="utf-8"))
if poems_doc.get("schema") not in ("poems/v1", "poems/v2"):
    sys.exit(f"✗ poems.json 的 schema 不認得（讀到 {poems_doc.get('schema')!r}）")

# 白話解讀。缺這個檔不算錯——解讀是分批補的，籤詩本身可以先上
readings_path = os.path.join(src, "readings.json")
readings_doc = None
if os.path.isfile(readings_path):
    readings_doc = json.load(io.open(readings_path, encoding="utf-8"))
    if readings_doc.get("schema") != "readings/v1":
        sys.exit(f"✗ readings.json 的 schema 不認得（讀到 {readings_doc.get('schema')!r}）")

poems = poems_doc["poems"]
missing = [p["no"] for p in poems if not p.get("text")]
unverified = [p["no"] for p in poems if p.get("text") and not p.get("verified")]

for p in poems:
    if p.get("text"):
        scan_bad(p["text"], f"第 {p['no']} 籤")

print(f"籤詩：共 {len(poems)} 首")
print(f"  已填寫    {len(poems) - len(missing)} 首")
print(f"  留空待補  {len(missing)} 首" + (f"：{missing[:20]}{' …' if len(missing) > 20 else ''}" if missing else ""))
print(f"  待校對    {len(unverified)} 首" + (f"：{unverified}" if unverified else ""))

# 解讀的類別順序就是裝置端 ritual_cat_t 的順序，錯位會讓「問事業」顯示姻緣的解讀。
# 這是靜默錯誤——畫面照樣好好的，只有內容不對——所以在這裡硬性比對
EXPECTED_CATS = ["事業", "姻緣", "財運", "健康", "學業", "家宅"]
readings = []
if readings_doc:
    if readings_doc.get("categories") != EXPECTED_CATS:
        sys.exit(f"✗ readings.json 的 categories 與 src/ritual.c 的順序不符\n"
                 f"   讀到 {readings_doc.get('categories')}\n"
                 f"   應為 {EXPECTED_CATS}")
    readings = readings_doc["readings"]
    filled = [r for r in readings if r.get("texts")]
    for r in filled:
        if len(r["texts"]) != len(EXPECTED_CATS):
            sys.exit(f"✗ 第 {r['no']} 籤的解讀有 {len(r['texts'])} 段，應為 {len(EXPECTED_CATS)} 段")
        for i, t in enumerate(r["texts"]):
            scan_bad(t, f"第 {r['no']} 籤的{EXPECTED_CATS[i]}解讀")
    print()
    print(f"白話解讀：{len(filled)} / {len(readings)} 首已寫"
          f"（{len(filled) * len(EXPECTED_CATS)} / {len(readings) * len(EXPECTED_CATS)} 段）")
    if len(filled) < len(readings):
        todo = [r["no"] for r in readings if not r.get("texts")]
        print(f"  待補：{todo[:20]}{' …' if len(todo) > 20 else ''}")
else:
    print()
    print("白話解讀：沒有 data/readings.json，解籤閣只會顯示籤詩本文")

if missing or unverified:
    print()
    print("⚠  尚未可上產品。text=null 的要補齊，verified=false 的要人工校對後改成 true。")
    print("   籤詩本文屬公有領域，請自行查閱宮廟籤詩資料校對；")
    print("   不要讓自動工具去抓 temples.tw——該站 robots.txt 明確 Disallow ClaudeBot。")

if check_only:
    sys.exit(0)

os.makedirs(out, exist_ok=True)

# 裝置端只需要能查表的最小結構，欄位名縮短省 flash 與解析成本。
# 欄位名要和 src/content.c 的 copy_field 對得上，改這裡就要改那裡
slim = {"poems": [{"n": p["no"], "m": p.get("name"), "g": p.get("ganzhi"),
                   "k": p.get("trigram"), "a": p.get("attr"), "t": p["text"]}
                  for p in poems if p.get("text")]}
io.open(os.path.join(out, "poems.json"), "w", encoding="utf-8").write(
    json.dumps(slim, ensure_ascii=False, separators=(",", ":")) + "\n")

# 字集：掃過所有會顯示的字，交給 gen-font.sh 產子集字型。
# 漏一個字畫面上就是一個豆腐方塊，所以這一步不能靠人工維護字串
charset = set()
for p in poems:
    if p.get("text"):
        for key in ("text", "ganzhi", "trigram", "attr"):
            charset |= set(p.get(key) or "")
    if p.get("name"):
        charset |= set(p["name"])
for r in readings:
    for t in (r.get("texts") or []):
        charset |= set(t)
charset = {c for c in charset if c.strip() and ord(c) > 0x7F}
io.open(os.path.join(out, "charset.txt"), "w", encoding="utf-8").write(
    "".join(sorted(charset)) + "\n")

if readings:
    slim_r = {"readings": [{"n": r["no"], "t": r["texts"]}
                           for r in readings if r.get("texts")]}
    io.open(os.path.join(out, "readings.json"), "w", encoding="utf-8").write(
        json.dumps(slim_r, ensure_ascii=False, separators=(",", ":")) + "\n")

print()
print(f"產出 {out}/")
print(f"  poems.json    {os.path.getsize(os.path.join(out, 'poems.json'))} bytes"
      f"（{len(slim['poems'])} 首）")
if readings:
    print(f"  readings.json {os.path.getsize(os.path.join(out, 'readings.json'))} bytes"
          f"（{len(slim_r['readings'])} 首）")
print(f"  charset.txt   {len(charset)} 個相異中文字")
PY

[ "$CHECK_ONLY" -eq 1 ] && exit 0

# 產 storage 分區映像。參數一律從 sdkconfig 與 partitions.csv 讀——
# 寫死的話，改了設定就會產出掛不起來的 image，而且症狀是執行期才爆
ENV_NAME="${PIO_ENV:-esp32-c6-devkitc-1}"
SDKCONFIG="$ROOT/sdkconfig.$ENV_NAME"
[ -f "$SDKCONFIG" ] || { printf '找不到 %s，先跑一次 pio run\n' "$SDKCONFIG" >&2; exit 1; }

IMAGE="$ROOT/.pio/storage.bin"
python3 "$ROOT/scripts/mkspiffs.py" \
    --sdkconfig "$SDKCONFIG" \
    --partitions "$ROOT/partitions.csv" \
    --content "$OUT" \
    --output "$IMAGE"

printf '\n下一步：\n'
printf '  字型重產   ./scripts/gen-font.sh --extra "$(cat %s/charset.txt)"\n' "$OUT"
printf '  燒錄語料   ./scripts/flash-content.sh\n'
