# 語料原始檔

這裡是進版控的**原始語料**，不是產物。`scripts/gen-content.sh` 從這裡產生
`storage` 分區的 SPIFFS image 與字型子集的字集檔，兩者都不手改。

| 檔案 | 內容 | 狀態 |
|------|------|------|
| `poems.json` | 六十甲子籤的籤詩本文、干支、卦象、屬性方位 | **63/63 已校對**（`poems/v2`） |
| `readings.json` | 白話解讀（籤號 × 6 個問事類別） | **63/63 已撰寫，未經人工校對**（`readings/v1`） |
| `temples-fs60.csv` | 校對籤詩用的擷取結果，由使用者提供 | 參考資料，不進裝置 |

`temples-fs60.csv` **只保留籤譜本身**：編號、籤名、干支、卦象、屬性方位、籤詩。
原始擷取結果另有籤詩語譯、籤意、故事、籤詩應驗故事、籤解五欄，那些是 temples.tw
編寫的內容，沒有進 `poems.json`，也不收在本檔；來源網址同樣不留。
裁切前逐首程式確認過那五欄未被引用，裁切後 63 首仍與 `poems.json` 完全相符。

## poems.json：已校對

籤詩本文、干支、卦象、屬性方位屬公有領域的傳統籤譜資料。63 首全部以
`temples-fs60.csv` 逐首程式比對過，`verified` 全為 `true`。

**這批資料曾經整批重來過。** 最初由 AI 憑記憶產出，實測 9 首裡只有 3 首完全正確，
錯誤有三種型態：用字出入、張冠李戴（第 27 首放的是第 47 首的內容）、
字元污染（中文裡混入西里爾字母）。教訓寫在全域知識庫的
`dev/ai-llm/ai-generated-corpus-needs-charset-guard.md`。

## readings.json：本專案自產，未經校對

白話解讀是**本專案依籤詩本文的意象與吉凶語氣自行撰寫**的，不是傳統籤解。

**來源網站（temples.tw）的語譯、籤意、故事、籤解是該站著作，一律不收錄。**
該站 `robots.txt` 明確 `Disallow: ClaudeBot`，並以 Content-Signal 聲明
`ai-train=no, use=reference`，不要讓自動工具去抓。

`categories` 的順序**必須**與 `src/ritual.c` 的 `ritual_cat_t` 一致：
事業、姻緣、財運、健康、學業、家宅。錯位會讓「問事業」顯示姻緣的解讀，
而畫面完全正常——所以 `gen-content.sh` 會硬性比對，不符就中止。

`verified` 目前全部標成 `true` 代表「已撰寫」，**不代表已經有人讀過**。
語氣與分寸還沒有人校對過，尤其凶籤那幾首容易寫得太滿。

## 守門機制

`gen-content.sh` 會擋下三件事，任一發生就中止而不是警告：

1. **字元白名單**——只允許 ASCII、全形標點、幾何符號、CJK 漢字。
   AI 產的語料實際混入過西里爾字母，肉眼在幾百句裡掃不出來。
2. **類別順序**與 `ritual_cat_t` 不符。
3. **解讀段數**不等於 6。

另有 `scripts/check-glyphs.sh` 檢查 `src/` 裡寫死的 UI 字串是否都在字型子集內——
漏一個字畫面上就是一個豆腐方塊，而且要燒進去才看得到。

## 改了語料之後

```bash
./scripts/gen-content.sh      # → spiffs_content/ 與 .pio/storage.bin
./scripts/gen-font.sh         # 自動吃 spiffs_content/charset.txt
./scripts/check-glyphs.sh     # 確認 UI 字串沒有漏字
~/.platformio/penv/bin/pio run -t upload   # 字型在 app 分區，要重燒韌體
./scripts/flash-content.sh                 # 只燒 storage 分區
```

**字型與語料綁在一起**：語料新增了字，字型子集就要重產，而字型在 app 分區，
所以換內容要連韌體一起重燒。
