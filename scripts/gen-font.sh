#!/usr/bin/env bash
# 產生 LVGL 中文子集字型到 src/fonts/。
# LVGL 內建字型只有 ASCII，中文要自己轉；整套 CJK 太大，只收 UI 實際會出現的字。
# 字集來源有三層，全部聯集：腳本內的 UI 固定字串、gen-content.sh 產的 charset.txt、--extra。
# 語料的字一定要走 charset.txt——手動維護字串遲早會漏，而漏一個字畫面上就是一個豆腐方塊。
# 用法：./scripts/gen-font.sh [--extra "額外要收的字"] [--font <ttf 路徑>] [--no-corpus]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="$ROOT/src/fonts"
TTF="/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
EXTRA=""
CHARSET="$ROOT/spiffs_content/charset.txt"
USE_CORPUS=1

while [ $# -gt 0 ]; do
    case "$1" in
        --extra)     EXTRA="${2-}"; shift 2 ;;
        --font)      TTF="${2-}";   shift 2 ;;
        --no-corpus) USE_CORPUS=0;  shift ;;
        -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf '未知參數：%s\n' "$1" >&2; exit 1 ;;
    esac
done

[ -f "$TTF" ] || { printf '找不到字型檔：%s\n' "$TTF" >&2; exit 1; }
command -v npx >/dev/null || { echo "需要 node/npx 才能跑 lv_font_conv" >&2; exit 1; }

mkdir -p "$OUTDIR"

# UI 固定字串 + 天氣狀態 + 台灣常見地名，涵蓋 app_config.h 預設值
UI_CHARS='天氣溫度濕體感風速更新連線中失敗休眠電池未取得資料錯誤重試無網路秒分時前剛才第次設定手機掃描配熱點密碼'
# 開機期錯誤畫面（error_screen.c）
ERR_CHARS='語料未燒錄接上執行'
# 導覽：正殿的兩個側翼（stub_screen.c）
NAV_CHARS='參拜簿尚開放'
# 正殿的匾額與香爐（shrine_screen.c）
SHRINE_CHARS='成功廟有求必應'
# 參拜簿統計（records_screen.c）
STAT_CHARS='累計統稟告求中的擲筊聖笑陰立次數參拜簿尚無紀錄回'
# 設定頁（settings_screen.c）
SETTING_CHARS='音量亮度電'
# 參拜紀錄：參拜簿與禮畢頁（records_screen.c、end_screen.c）
RECORD_CHARS='累計紀錄無禮畢平安'
# 儀式流程：每日參拜、稟告、求籤、解籤閣
RITUAL_CHARS='事業姻緣財運健康學業家宅向神明行三拜把機身前傾下再起一二禮成所求何搖籤問回正殿詩讀到：這首的白話解尚寫入。有必應收後閣答示得此允不極罕見'
# 擲筊畫面
CAST_CHARS='擲筊聖笑陰立搖動請誠心默念所求之事神明允可依此而行一不問過再宜極為罕見慎重擲出結果'
WX_CHARS='晴多雲陰有霧淞毛細雨凍陣小大雪冰雹珠雷暴強沙塵'
GEO_CHARS='台臺北新市桃園竹苗栗中彰化南投雲林嘉義高雄屏東宜蘭花蓮澎湖金門馬祖基隆鄉鎮區縣'

# 語料字集。gen-content.sh 從 data/ 掃出來，語料一改就重產，不必手動同步
CORPUS_CHARS=""
if [ "$USE_CORPUS" -eq 1 ]; then
    if [ -f "$CHARSET" ]; then
        CORPUS_CHARS="$(cat "$CHARSET")"
        printf '收錄語料字集：%s（%s 字）\n' "${CHARSET#$ROOT/}" \
            "$(python3 -c 'import sys,io; print(len(io.open(sys.argv[1],encoding="utf-8").read().strip()))' "$CHARSET")"
    else
        printf '⚠  找不到 %s，這次不含語料字集——解籤畫面會出現豆腐方塊。\n' "${CHARSET#$ROOT/}" >&2
        printf '   先跑 ./scripts/gen-content.sh 再回來。\n' >&2
    fi
fi

# 把所有 *_CHARS 變數收齊。這裡刻意不手寫清單——check-glyphs.sh 是自動掃描
# 所有 *_CHARS，兩邊規則一旦不同步，新增的字集會漏進不了字型而守門還是放行，
# 畫面上就是豆腐方塊。實際發生過：STAT/SETTING/RECORD 三組字全部漏掉。
ALL_CHARS=""
for _v in $(compgen -v | grep '_CHARS$'); do
    ALL_CHARS+="${!_v}"
done

# 逐字去重。macOS 的 en_US.UTF-8 對 CJK 沒有 collation 權重，sort -u／awk 比較
# 會把不同的中文字判定相等而靜默丟字，改用 python 逐 code point 處理。
SYMBOLS="$(python3 -c '
import sys
s = sys.argv[1]
seen = {}
sys.stdout.write("".join(seen.setdefault(c, c) for c in s if c not in seen))
' "${ALL_CHARS}${EXTRA}")"

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
    # lv_font_conv 會在檔尾多留一行空白，git 的 whitespace 檢查會擋下來
    python3 -c 'import sys; f=sys.argv[1]; d=open(f).read().rstrip("\n")+"\n"; open(f,"w").write(d)' "$out"
}

# 內文與標題：ASCII + 中文子集
gen font_zh_16 16 4 "$SYMBOLS" "-r 0x20-0x7E -r 0xB0 -r 0x3000 -r 0xFF0C"
gen font_zh_28 28 4 "$SYMBOLS" "-r 0x20-0x7E -r 0xB0 -r 0x3000 -r 0xFF0C"
# 大字溫度只出現數字，收整套中文會白白吃掉幾十 KB
gen font_num_56 56 4 "" "-r 0x2D -r 0x2E -r 0x30-0x39 -r 0x43 -r 0xB0"
# 天氣圖示。Arial Unicode 沒有 U+26A1 閃電，雷雨改用 U+2607
gen font_icon_72 72 4 "" "-r 0x2248 -r 0x2600-0x2603 -r 0x2607 -r 0x2744"

printf '完成，輸出於 %s（共 %s 個相異字符）\n' "$OUTDIR" \
    "$(python3 -c 'import sys; print(len(sys.argv[1]))' "$SYMBOLS")"
