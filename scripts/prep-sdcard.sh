#!/usr/bin/env bash
# 把 M0/S1 驗證要用的測試檔寫進記憶卡（卡插在 Mac 上，不是板子上）。
# 產出 /temple/manifest.json 與 /temple/bulk.bin，後者用來量 sdspi 的讀取吞吐。
# 用法：./scripts/prep-sdcard.sh /Volumes/<卡片名稱> [--size-mb 4] [--yes]
set -euo pipefail

VOL=""
SIZE_MB=4
ASSUME_YES=0

while [ $# -gt 0 ]; do
    case "$1" in
        --size-mb) SIZE_MB="${2-}"; shift 2 ;;
        --yes|-y)  ASSUME_YES=1;    shift ;;
        -h|--help) sed -n '2,4p' "${BASH_SOURCE[0]}"; exit 0 ;;
        -*) printf '未知參數：%s\n' "$1" >&2; exit 1 ;;
        *)  VOL="$1"; shift ;;
    esac
done

[ -n "$VOL" ] || { printf '要給目標路徑，例如 /Volumes/UNTITLED\n' >&2; exit 1; }
VOL="${VOL%/}"
[ -d "$VOL" ] || { printf '找不到 %s\n' "$VOL" >&2; exit 1; }

# 這支腳本會往實體磁碟寫東西，寫錯就是砸別人的資料。先確認它真的是掛載中的可移除 FAT 卷
MOUNT_LINE="$(/sbin/mount | grep -F " on $VOL " || true)"
[ -n "$MOUNT_LINE" ] || { printf '%s 不是掛載點\n' "$VOL" >&2; exit 1; }

case "$MOUNT_LINE" in
    *msdos*|*exfat*) ;;
    *) printf '%s 不是 FAT／exFAT：%s\n' "$VOL" "$MOUNT_LINE" >&2
       printf 'ESP-IDF 的 fatfs 只認 FAT，先在磁碟工具程式格式化成 MS-DOS (FAT)\n' >&2
       exit 1 ;;
esac

DEV="${MOUNT_LINE%% *}"
printf '目標：%s\n' "$VOL"
printf '裝置：%s\n' "$DEV"
/usr/sbin/diskutil info "$DEV" 2>/dev/null | grep -E "Volume Name|File System Personality|Removable Media|Disk Size" || true
printf '\n將寫入 %s/temple/（bulk.bin 約 %s MB）\n' "$VOL" "$SIZE_MB"

if [ "$ASSUME_YES" -ne 1 ]; then
    printf '確定？[y/N] '
    read -r ans
    case "$ans" in [yY]*) ;; *) printf '取消\n'; exit 1 ;; esac
fi

DEST="$VOL/temple"
mkdir -p "$DEST"

# bulk.bin：內容不重要，量的是吞吐。/dev/urandom 避免卡片或檔案系統對整片零做壓縮
dd if=/dev/urandom "of=$DEST/bulk.bin" bs=1m "count=$SIZE_MB" 2>/dev/null
BULK_BYTES="$(stat -f %z "$DEST/bulk.bin")"
BULK_SHA="$(shasum -a 256 "$DEST/bulk.bin" | cut -d' ' -f1)"

# 欄位對應 v3 企劃 §6 的內容包規格；S1 只驗掛載與讀取，語料檔還沒有
cat > "$DEST/manifest.json" <<JSON
{
  "content_version": 0,
  "min_firmware": 0,
  "charset_hash": "",
  "note": "M0/S1 驗證用測試包，非正式內容包",
  "files": [
    { "name": "bulk.bin", "bytes": $BULK_BYTES, "sha256": "$BULK_SHA" }
  ]
}
JSON

sync
printf '\n完成：\n'
ls -la "$DEST"
printf '\nbulk.bin sha256 = %s\n' "$BULK_SHA"
printf '退出卡片：diskutil eject %s\n' "$DEV"
