#!/usr/bin/env bash
# 非互動式讀序列埠。pio device monitor 需要 TTY，在腳本／CI 裡會炸 termios。
# 板子重置時原生 USB 會重新列舉，所以掉線後要自己重連，否則抓不到開機日誌。
# 用法：./scripts/monitor.sh [秒數] [埠]
set -euo pipefail

SECS="${1:-20}"
PORT="${2:-/dev/cu.usbmodem1101}"
PY="$HOME/.platformio/penv/bin/python"

exec "$PY" - "$PORT" "$SECS" <<'PY'
import sys, time, serial

port, secs = sys.argv[1], float(sys.argv[2])
end = time.time() + secs
while time.time() < end:
    try:
        with serial.Serial(port, 115200, timeout=0.2) as s:
            while time.time() < end:
                data = s.read(4096)
                if data:
                    sys.stdout.write(data.decode("utf-8", "replace"))
                    sys.stdout.flush()
    except (OSError, serial.SerialException):
        time.sleep(0.2)
PY
