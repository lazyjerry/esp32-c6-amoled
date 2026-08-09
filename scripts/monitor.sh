#!/usr/bin/env bash
# 非互動式讀序列埠。pio device monitor 需要 TTY，在腳本／CI 裡會炸 termios。
# 板子重置時原生 USB 會重新列舉，所以掉線後要自己重連，否則抓不到開機日誌。
# 用法：./scripts/monitor.sh [秒數] [埠] [--reset]
set -euo pipefail

SECS="20"
PORT="/dev/cu.usbmodem1101"
RESET="0"

for arg in "$@"; do
    case "$arg" in
        --reset) RESET="1" ;;
        /dev/*)  PORT="$arg" ;;
        *)       SECS="$arg" ;;
    esac
done

PY="$HOME/.platformio/penv/bin/python"

exec "$PY" - "$PORT" "$SECS" "$RESET" <<'PY'
import sys, time, serial

port, secs, do_reset = sys.argv[1], float(sys.argv[2]), sys.argv[3] == "1"

# USB Serial/JTAG 沒有實體 DTR/RTS，但週邊會把這兩條線的組合解讀成重置請求，
# 順序和 esptool 的 usb_jtag_serial reset 相同：先拉住 EN 再放開
if do_reset:
    with serial.Serial(port, 115200) as s:
        s.dtr = False; s.rts = False; time.sleep(0.1)
        s.dtr = True;  s.rts = False; time.sleep(0.1)
        s.dtr = False; s.rts = True;  time.sleep(0.1)
        s.dtr = False; s.rts = False
    time.sleep(0.3)

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
