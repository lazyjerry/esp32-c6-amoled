#!/usr/bin/env bash
# 產生擲筊用的筊杯圖檔到 src/sprites/（LVGL 9 的 ARGB8888 image dsc）。
# 兩張：平面朝上（blk_flat）與凸面朝上（blk_round），姿態差異只靠打光表現。
# 用法：./scripts/gen-sprites.sh [--width 132] [--color A81A20]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="$ROOT/src/sprites"
WIDTH=132
COLOR="A81A20"

while [ $# -gt 0 ]; do
    case "$1" in
        --width) WIDTH="${2-}"; shift 2 ;;
        --color) COLOR="${2-}"; shift 2 ;;
        -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf '未知參數：%s\n' "$1" >&2; exit 1 ;;
    esac
done

python3 -c 'import PIL' 2>/dev/null || { echo "需要 Pillow：python3 -m pip install pillow" >&2; exit 1; }

mkdir -p "$OUTDIR"
python3 "$ROOT/scripts/gen_sprites.py" --outdir "$OUTDIR" --width "$WIDTH" --color "$COLOR"
printf '完成，輸出於 %s\n' "$OUTDIR"
