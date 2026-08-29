# 語料原始檔

這裡是進版控的**原始語料**，不是產物。`scripts/gen-content.sh` 從這裡產生
`storage` 分區的 SPIFFS image 與字型子集，兩者都不手改。

| 檔案 | 內容 | 狀態 |
|------|------|------|
| `poems.json` | 六十甲子籤籤詩本文、干支、吉凶 | **待校對**，見下 |
| `readings.json` | 傳統解籤（籤號 × 問題類別） | 未建立 |
| `ai_readings.json` | AI 預產解讀（籤號 × 問題類別） | 未建立 |

## poems.json 的校對狀態

籤詩本文屬公有領域傳統文本，各宮廟通用。目前檔案由 AI 依既有認識產出，
**每一首都帶 `verified` 欄位**：

- `verified: false` — AI 產出，**尚未經人工校對**，可能有用字出入
- `verified: true` — 已由人校對過
- `text: null` — AI 對這首沒有把握，**刻意留空**，不編造

**上產品前所有 `verified` 必須為 `true`。** `gen-content.sh` 會在遇到
`verified: false` 或 `text: null` 時發出警告。

校對來源建議由人自行查閱宮廟籤詩資料（例如 temples.tw），
**不要讓自動工具去抓**——該站 robots.txt 明確 `Disallow: ClaudeBot`，
且站方編寫的籤義屬其著作，不可內嵌進本專案。
