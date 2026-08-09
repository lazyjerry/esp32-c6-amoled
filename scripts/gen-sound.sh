#!/usr/bin/env bash
# 產生擲筊音效到 src/sounds/（16-bit 單聲道 PCM，直接編進韌體）。
# 合成而非取樣：木質筊落地是幾個共振模態加一段起始噪音，不值得為它帶一個 wav 檔。
# 用法：./scripts/gen-sound.sh [--rate 16000] [--preview out.wav]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="$ROOT/src/sounds"
RATE=16000
PREVIEW=""

while [ $# -gt 0 ]; do
    case "$1" in
        --rate)    RATE="${2-}";    shift 2 ;;
        --preview) PREVIEW="${2-}"; shift 2 ;;
        -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf '未知參數：%s\n' "$1" >&2; exit 1 ;;
    esac
done

mkdir -p "$OUTDIR"
python3 "$ROOT/scripts/gen_sound.py" --outdir "$OUTDIR" --rate "$RATE" ${PREVIEW:+--preview "$PREVIEW"}
printf '完成，輸出於 %s\n' "$OUTDIR"
