# 掌上宮廟

Waveshare ESP32-C6-Touch-AMOLED-1.8 上的**單機參拜裝置**。開機進正殿，
行三拜、稟告所求、搖籤筒求籤、擲筊請示，最後在解籤閣讀籤詩與白話解讀。

**全程不連網。** 所有語料與字型都燒在機身 flash 裡，換內容要接 USB 重燒。
換來的是開機一秒到正殿、沒有後端、沒有金鑰、沒有隱私問題。

| | |
|---|---|
| 硬體 | ESP32-C6（無 PSRAM）、SH8601 QSPI AMOLED 368×448、FT5x06 觸控、QMI8658 IMU、ES8311 音訊、AXP2101 電源 |
| 工具鏈 | macOS + PlatformIO + ESP-IDF 6.0 + LVGL 9.3 |
| 開機到正殿 | 約 1.0 秒 |
| 佔用 | Flash 19.5%（1.22MB / 6MB）、RAM 19.3% |

## 文件在哪裡

| 要找什麼 | 去哪裡 |
|---|---|
| **開發約定、硬體實測事實、建置指令** | [CLAUDE.md](CLAUDE.md) — 動工前先讀這份 |
| 產品企劃、里程碑、驗證結果 | [docs/plans/ESP32-C6-Touch-AMOLED-開發計畫-v3.md](docs/plans/ESP32-C6-Touch-AMOLED-開發計畫-v3.md) |
| **踩過的坑與結論** | [docs/notes/index.md](docs/notes/index.md) — 掃索引，命中才開該篇 |
| 語料與著作權界線 | [data/README.md](data/README.md) |
| 驗證程式（已通過並歸檔） | [docs/examples/](docs/examples/) |
| 研究過程全文 | [docs/knowledge-skill/](docs/knowledge-skill/) |

## 快速上手

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"   # pio 不在預設 PATH

pio run                                  # 編譯
pio run -t upload                        # 燒韌體
./scripts/flash-content.sh               # 只燒語料（storage 分區）
./scripts/monitor.sh 20 --reset          # 讀序列埠（非互動式）
```

**PlatformIO 不會產生也不會燒 SPIFFS 映像**，語料一定要走
`gen-content.sh` + `flash-content.sh`，不是 `pio run -t buildfs/uploadfs`。
理由見 [筆記](docs/notes/pio-does-not-build-or-flash-spiffs.md)。

## 產物一律由腳本產生，不手改

```bash
./scripts/gen-content.sh      # data/*.json → spiffs_content/ + storage.bin + charset.txt
./scripts/gen-font.sh         # → src/fonts/*.c（自動吃 charset.txt）
./scripts/check-glyphs.sh     # UI 字串漏字檢查（漏字 = 畫面上的豆腐方塊）
./scripts/gen-sprites.sh      # → src/sprites/*.c
./scripts/gen-sound.sh        # → src/sounds/*
./scripts/note.sh find <關鍵字> # 動手前查已知的坑
```

跑完就沒有下次的腳本收在 `scripts/one-time/`，**不進版控**——留在本機當紀錄，
但不該混在日常工具裡。各支的用途與「為什麼不會再跑」寫在該目錄的 README。

## 進度

M0（硬體驗證）與 M1（畫面骨架）完成，M2（儀式閉環）程式與語料到位。
逐項狀態見企劃書 §7。

**目前的美術是 LVGL 幾何佔位造型，不是最終視覺**——正殿的神像神龕、
求籤的籤筒竹籤都是。換成真圖只要改各畫面的 `build_screen()`。

## 著作權

籤詩本文、干支、卦象、屬性方位屬公有領域的傳統籤譜資料。
白話解讀是本專案自行撰寫的。**不收錄任何來源網站編寫的語譯與籤意**，
理由與界線見 [data/README.md](data/README.md)。
