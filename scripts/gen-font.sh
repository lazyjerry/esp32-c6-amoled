#!/usr/bin/env bash
# 產生 LVGL 中文子集字型到 src/fonts/。
# LVGL 內建字型只有 ASCII，中文要自己轉；整套 CJK 太大，只收 UI 實際會出現的字。
# 用法：./scripts/gen-font.sh [--extra "額外要收的字"] [--font <ttf 路徑>]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="$ROOT/src/fonts"
TTF="/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
EXTRA=""

while [ $# -gt 0 ]; do
    case "$1" in
        --extra) EXTRA="${2-}"; shift 2 ;;
        --font)  TTF="${2-}";   shift 2 ;;
        -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf '未知參數：%s\n' "$1" >&2; exit 1 ;;
    esac
done

[ -f "$TTF" ] || { printf '找不到字型檔：%s\n' "$TTF" >&2; exit 1; }
command -v npx >/dev/null || { echo "需要 node/npx 才能跑 lv_font_conv" >&2; exit 1; }

mkdir -p "$OUTDIR"

# UI 固定字串 + 天氣狀態 + 台灣常見地名，涵蓋 app_config.h 預設值
UI_CHARS='天氣溫度濕體感風速更新連線中失敗休眠電池未取得資料錯誤重試無網路秒分時前剛才第次設定手機掃描配熱點密碼'
WX_CHARS='晴多雲陰有霧淞毛細雨凍陣小大雪冰雹珠雷暴強沙塵'
GEO_CHARS='台臺北新市桃園竹苗栗中彰化南投雲林嘉義高雄屏東宜蘭花蓮澎湖金門馬祖基隆鄉鎮區縣'

# 逐字去重。macOS 的 en_US.UTF-8 對 CJK 沒有 collation 權重，sort -u／awk 比較
# 會把不同的中文字判定相等而靜默丟字，改用 python 逐 code point 處理。
SYMBOLS="$(python3 -c '
import sys
s = sys.argv[1]
seen = {}
sys.stdout.write("".join(seen.setdefault(c, c) for c in s if c not in seen))
' "${UI_CHARS}${WX_CHARS}${GEO_CHARS}${EXTRA}")"

gen() {
    local name="$1" size="$2" bpp="$3" symbols="$4" ranges="$5"
    local out="$OUTDIR/${name}.c"
    printf '產生 %s（%spx, %sbpp）\n' "$name" "$size" "$bpp"
    # shellcheck disable=SC2086
    npx --yes lv_font_conv@1.5.3 \
        --font "$TTF" \
        --size "$size" \
        --bpp "$bpp" \
        --format lvgl \
        --lv-font-name "$name" \
        --lv-include lvgl.h \
        --no-compress \
        $ranges \
        ${symbols:+--symbols "$symbols"} \
        -o "$out"
}

# 內文與標題：ASCII + 中文子集
gen font_zh_16 16 4 "$SYMBOLS" "-r 0x20-0x7E -r 0xB0"
gen font_zh_28 28 4 "$SYMBOLS" "-r 0x20-0x7E -r 0xB0"
# 大字溫度只出現數字，收整套中文會白白吃掉幾十 KB
gen font_num_56 56 4 "" "-r 0x2D -r 0x2E -r 0x30-0x39 -r 0x43 -r 0xB0"
# 天氣圖示。Arial Unicode 沒有 U+26A1 閃電，雷雨改用 U+2607
gen font_icon_72 72 4 "" "-r 0x2248 -r 0x2600-0x2603 -r 0x2607 -r 0x2744"

printf '完成，輸出於 %s\n' "$OUTDIR"
