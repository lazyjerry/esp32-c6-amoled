#!/usr/bin/env python3
"""把目錄打包成 SPIFFS 映像，參數取自 sdkconfig 與 partitions.csv。

不寫死參數的理由：page size、obj name len、meta len 只要和韌體的 sdkconfig 不一致，
image 就掛不起來，而且症狀要到裝置執行期才出現（esp_vfs_spiffs_register 回 ESP_FAIL），
從錯誤訊息完全看不出是打包參數錯了。

被 scripts/gen-content.sh 呼叫，一般不直接執行。
"""
import argparse
import os
import re
import subprocess
import sys

IDF_SPIFFSGEN = os.path.expanduser(
    "~/.platformio/packages/framework-espidf/components/spiffs/spiffsgen.py"
)


def read_sdkconfig(path):
    cfg = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^(CONFIG_\w+)=(.*)$", line.strip())
            if m:
                cfg[m.group(1)] = m.group(2).strip('"')
    return cfg


def parse_size(text):
    """partitions.csv 的大小欄可以是 0x1000、4M、1024 等寫法"""
    text = text.strip()
    if text.lower().endswith("m"):
        return int(text[:-1], 0) * 1024 * 1024
    if text.lower().endswith("k"):
        return int(text[:-1], 0) * 1024
    return int(text, 0)


def find_storage_partition(path):
    """回傳 (offset, size)。offset 欄留空時要自己往前累加算出來"""
    cursor = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) < 5:
                continue
            name, _type, _sub, off, size = cols[:5]
            size = parse_size(size)
            offset = int(off, 0) if off else cursor
            cursor = offset + size
            if name == "storage":
                return offset, size
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdkconfig", required=True)
    ap.add_argument("--partitions", required=True)
    ap.add_argument("--content", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    if not os.path.isfile(IDF_SPIFFSGEN):
        sys.exit(f"找不到 spiffsgen.py：{IDF_SPIFFSGEN}")

    offset, size = find_storage_partition(args.partitions)
    if offset is None:
        sys.exit(f"{args.partitions} 裡沒有 storage 分區")

    cfg = read_sdkconfig(args.sdkconfig)
    page = cfg.get("CONFIG_SPIFFS_PAGE_SIZE", "256")
    name_len = cfg.get("CONFIG_SPIFFS_OBJ_NAME_LEN", "32")
    meta_len = cfg.get("CONFIG_SPIFFS_META_LENGTH", "4")
    use_magic = cfg.get("CONFIG_SPIFFS_USE_MAGIC") == "y"
    magic_len = cfg.get("CONFIG_SPIFFS_USE_MAGIC_LENGTH") == "y"

    cmd = [
        sys.executable, IDF_SPIFFSGEN,
        str(size), args.content, args.output,
        "--page-size", page,
        "--obj-name-len", name_len,
        "--meta-len", meta_len,
        "--use-magic" if use_magic else "--no-magic",
        "--use-magic-len" if magic_len else "--no-magic-len",
    ]
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    subprocess.run(cmd, check=True)

    files = sorted(os.listdir(args.content))
    used = sum(os.path.getsize(os.path.join(args.content, f)) for f in files)
    print(f"  storage 映像 {args.output}")
    print(f"    分區 offset 0x{offset:X}、容量 {size // 1024 // 1024} MB")
    print(f"    收錄 {len(files)} 個檔案共 {used} bytes（{used * 100.0 / size:.2f}%）")
    print(f"    參數 page={page} obj_name_len={name_len} meta_len={meta_len} "
          f"magic={'y' if use_magic else 'n'} magic_len={'y' if magic_len else 'n'}")
    # 燒錄位址寫進同名 .offset，flash-content.sh 讀它，避免兩支腳本各自算一次
    with open(args.output + ".offset", "w", encoding="utf-8") as f:
        f.write(hex(offset) + "\n")


if __name__ == "__main__":
    main()
