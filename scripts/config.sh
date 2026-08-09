#!/usr/bin/env bash
# 產生 src/app_config.h（地點設定）。
# WiFi 憑證不在這裡 —— 走 SoftAP 配網，第一次開機掃螢幕上的 QR 設定，存在 NVS。
# 用法：./scripts/config.sh [--lat 25.0330] [--lon 121.5654] [--tz Asia/Taipei] [--name 台北]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/src/app_config.h"

LAT="25.0330"
LON="121.5654"
TZ="Asia/Taipei"
NAME="台北"

while [ $# -gt 0 ]; do
    case "$1" in
        --lat)  LAT="${2-}";  shift 2 ;;
        --lon)  LON="${2-}";  shift 2 ;;
        --tz)   TZ="${2-}";   shift 2 ;;
        --name) NAME="${2-}"; shift 2 ;;
        -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf '未知參數：%s\n' "$1" >&2; exit 1 ;;
    esac
done

# Open-Meteo 的 timezone 參數走 query string，斜線要編碼
TZ_ENC="${TZ//\//%2F}"

esc() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }

cat > "$OUT" <<EOF
// 由 scripts/config.sh 產生，請勿手動編輯
#pragma once

#define APP_WEATHER_LAT     "$(esc "$LAT")"
#define APP_WEATHER_LON     "$(esc "$LON")"
#define APP_WEATHER_TZ_ENC  "$(esc "$TZ_ENC")"
#define APP_LOCATION_NAME   "$(esc "$NAME")"
EOF

# 變數展開緊接全形字元會被 bash 3.2 吃掉一個位元組，一律走 printf
printf '已寫入 %s\n' "$OUT"
printf '  地點=%s (%s, %s)  時區=%s\n' "$NAME" "$LAT" "$LON" "$TZ"
printf '提醒：地點名稱若含字型子集以外的字，需跑 ./scripts/gen-font.sh --extra %s\n' "$NAME"
