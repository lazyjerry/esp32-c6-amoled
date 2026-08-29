# 掌上宮廟

Waveshare ESP32-C6-Touch-AMOLED-1.8 上的**單機參拜裝置**。開機進正殿，
行三拜、稟告所求、搖籤筒求籤、擲筊請示，在解籤閣讀籤詩與白話解讀，
禮畢後留下一筆參拜紀錄。

**全程不連網。** 所有語料與字型都燒在機身 flash 裡，換內容要接 USB 重燒。
換來的是開機一秒到正殿、沒有後端、沒有金鑰、沒有隱私問題。

| | |
|---|---|
| 硬體 | ESP32-C6（無 PSRAM）、SH8601 QSPI AMOLED 368×448、FT5x06 觸控、QMI8658 IMU、ES8311 音訊、AXP2101 電源 |
| 工具鏈 | macOS + PlatformIO + ESP-IDF 6.0 + LVGL 9.3 |
| 開機到正殿 | 約 1.1 秒（掛載語料佔 300ms） |
| 佔用 | Flash 20.5%（1.29MB / 6MB）、RAM 19.5%、堆積餘裕 272KB |

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
./scripts/gen-font.sh         # → src/fonts/*.c（自動收齊所有 *_CHARS 與 charset.txt）
./scripts/check-glyphs.sh     # UI 字串漏字檢查（漏字 = 畫面上的豆腐方塊）
./scripts/gen-sprites.sh      # → src/sprites/*.c
./scripts/gen-sound.sh        # → src/sounds/*
./scripts/note.sh find <關鍵字> # 動手前查已知的坑
```

跑完就沒有下次的腳本收在 `scripts/one-time/`，**不進版控**——留在本機當紀錄，
但不該混在日常工具裡。各支的用途與「為什麼不會再跑」寫在該目錄的 README。

## 有什麼

| 畫面 | 做什麼 |
|---|---|
| 正殿 | 匾額「成功廟」、香爐上三支線香（每次進來各自隨機四級長度），煙從香頭飄起 |
| 三拜 | 把機身往前傾下再起身 ×3，或按 BOOT。禮成敲一聲鐘 |
| 稟告 | 事業／姻緣／財運／健康／學業／家宅，六選一 |
| 求籤 | 搖動 → 籤筒抖動 + 竹籤碰撞聲 → 出籤號 |
| 擲筊 | 兩片紅木筊落地彈跳，聖筊進解籤、笑筊重擲、陰筊回求籤 |
| 解籤閣 | 籤詩一句一行，加上該類別的白話解讀，可捲動 |
| 禮畢 | 「第 N 次參拜」，2.6 秒回正殿 |
| 參拜簿 | 累計參拜／擲筊／各筊象／各類別稟告次數，加最近求中的籤（點一列翻到該首） |
| 設定 | 直立格子條調音量與亮度（存 NVS），一行電池電量 |

操作規則只有三條：**短按 BOOT 做這個畫面的主要動作、長按回正殿、左右滑動換側翼**。

## 進度

**M0～M4 全部完成**，逐項狀態與每個決定的理由見企劃書 §7。
M4 只剩「手持狀態下重調 IMU 門檻」——那要拿在手上實測手感才調得準。

**目前的美術是 LVGL 幾何佔位造型，不是最終視覺**——正殿的神像神龕、
香爐線香、求籤的籤筒竹籤都是。換成真圖只要改各畫面的 `build_screen()`。

## 著作權

籤詩本文、干支、卦象、屬性方位屬公有領域的傳統籤譜資料。
白話解讀是本專案自行撰寫的。**不收錄任何來源網站編寫的語譯與籤意**，
理由與界線見 [data/README.md](data/README.md)。
