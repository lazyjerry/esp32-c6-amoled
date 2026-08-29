#!/usr/bin/env bash
# 只燒 storage 分區的語料映像，不動韌體。
# PlatformIO 的 upload 只燒 bootloader/partitions/firmware，不會燒 SPIFFS，所以要這一支。
# 用法：./scripts/flash-content.sh [--port /dev/cu.usbmodemXXX]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="$ROOT/.pio/storage.bin"
PORT=""

while [ $# -gt 0 ]; do
    case "$1" in
        --port) PORT="${2-}"; shift 2 ;;
        -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf '未知參數：%s\n' "$1" >&2; exit 1 ;;
    esac
done

[ -f "$IMAGE" ] || { printf '找不到 %s，先跑 ./scripts/gen-content.sh\n' "$IMAGE" >&2; exit 1; }
[ -f "$IMAGE.offset" ] || { printf '找不到 %s.offset\n' "$IMAGE" >&2; exit 1; }

OFFSET="$(cat "$IMAGE.offset")"

if [ -z "$PORT" ]; then
    # 尾碼由 USB 孔位決定，不寫死
    PORT="$(ls -1 /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
    [ -n "$PORT" ] || { printf '找不到 /dev/cu.usbmodem*，板子接上了嗎？\n' >&2; exit 1; }
fi

ET="$HOME/.platformio/packages/tool-esptoolpy"
printf '燒錄 %s → %s @ %s\n' "$(basename "$IMAGE")" "$PORT" "$OFFSET"

PYTHONPATH="$ET/_contrib:$ET" "$HOME/.platformio/penv/bin/python" -m esptool \
    --chip esp32c6 --port "$PORT" --baud 460800 \
    --before default_reset --after hard_reset \
    write_flash -z "$OFFSET" "$IMAGE"
