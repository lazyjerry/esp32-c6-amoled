# 任務請求：以 Python 改寫燒錄流程可行性研究

## 原始請求

```
請研究目前專案內容，是否可以用 python 改寫燒錄？
```

## 期望產出

- [x] 可行性結論：目前燒錄鏈路的實際組成，以及改用 Python 是否成立
- [x] 實作設計：若可行，提出符合本專案既有慣例的腳本結構與參數
- [x] 風險與陷阱清單：改寫後可能踩到的坑（版本、路徑、reset 行為）

**產出類型：**
- 文件：`docs/knowledge-skill/以_Python_改寫燒錄可行性研究-001/report.md`
- 程式碼：本次僅設計，不落地。若採用則新增 `scripts/flash.sh` + `scripts/flash.py`
- 其他：候選知識沉澱至公用知識庫

## 參考文件

| 檔案路徑 | 引用範圍描述 |
|----------|--------------|
| `platformio.ini` | upload_port / board_upload.flash_size 設定 |
| `partitions.csv` | app 分區偏移（0x10000）與 storage 分區 |
| `sdkconfig.esp32-c6-devkitc-1` | ESPTOOLPY_FLASHMODE / FLASHFREQ / FLASHSIZE / BOOTLOADER_OFFSET |
| `scripts/monitor.sh` | 既有「shell 包 Python」寫法與 USB Serial/JTAG reset 序列 |
| `scripts/gen-sound.sh` + `scripts/gen_sound.py` | 既有「shell 入口 + Python 實作」慣例 |
| `.pio/build/esp32-c6-devkitc-1/flasher_args.json` | ESP-IDF 產出的燒錄參數 |
| `~/.platformio/platforms/espressif32/builder/main.py` | PlatformIO 實際組出的 esptool 上傳指令 |
| `~/.platformio/platforms/espressif32/builder/frameworks/espidf.py` | FLASH_EXTRA_IMAGES 與 app offset 決定邏輯 |
| `docs/notes/esp32c6-usb-serial-jtag-port.md` | 序列埠命名與原生 USB 事實 |
| `docs/notes/pio-flash-size-via-sdkconfig-defaults.md` | flash size 設定來源 |
