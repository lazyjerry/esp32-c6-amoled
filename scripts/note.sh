#!/usr/bin/env bash
# 操作筆記工具：建立、檢查、重建索引、查詢
# 規格：note/v1（筆記）、note-index/v1（索引）
set -euo pipefail

NOTE_SPEC="note/v1"
INDEX_SPEC="note-index/v1"
MAX_LINES=60   # 超過視為流水帳嫌疑，僅警告

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOTES_DIR="$ROOT/docs/notes"
INDEX="$NOTES_DIR/index.md"

REQUIRED_KEYS="規格 標題 分類 觸發時機 摘要 狀態 建立日期"
VALID_STATUS="生效 已取代"

die() { printf '錯誤：%s\n' "$*" >&2; exit 1; }
warn() { printf '警告：%s\n' "$*" >&2; }

usage() {
  cat <<'EOF'
用法：
  scripts/note.sh new <slug> -c 分類 -t 標題 -w 觸發時機 -s 摘要
      建立筆記骨架（frontmatter 填好，正文留佔位待填）

  scripts/note.sh check
      檢查所有筆記是否符合 note/v1 規格

  scripts/note.sh index
      重建 docs/notes/index.md（會先跑 check）

  scripts/note.sh sync
      等同 check + index

  scripts/note.sh find <關鍵字>
      從索引查詢既有筆記，動手前先跑這個避免重複踩雷

分類建議：hardware / toolchain / build / flash / driver / lvgl / misc
EOF
}

# 取 frontmatter 欄位值：field <檔案> <鍵>
# 不用 awk：macOS 內建 awk 20200816 比較兩個非 ASCII 字串一律回真，會誤配欄位鍵
field() {
  sed -n '2,/^---$/p' "$1" | sed -n "s/^$2:[[:space:]]*//p" | head -1
}

note_files() {
  find "$NOTES_DIR" -maxdepth 1 -name '*.md' ! -name 'index.md' 2>/dev/null | sort
}

# 表格欄位跳脫：| 會破壞 Markdown 表格
esc() { printf '%s' "$1" | sed 's/|/\\|/g'; }

cmd_check() {
  [ -d "$NOTES_DIR" ] || die "找不到 $NOTES_DIR"
  local rc=0 count=0 titles="" f key val status lines title

  while IFS= read -r f; do
    [ -n "$f" ] || continue
    count=$((count + 1))

    if ! head -1 "$f" | grep -q '^---$'; then
      printf '  ✗ %s：缺 frontmatter 起始 ---\n' "${f#$ROOT/}"; rc=1; continue
    fi

    for key in $REQUIRED_KEYS; do
      val="$(field "$f" "$key" || true)"
      if [ -z "$val" ]; then
        printf '  ✗ %s：frontmatter 缺欄位「%s」\n' "${f#$ROOT/}" "$key"; rc=1
      fi
    done

    val="$(field "$f" 規格 || true)"
    if [ -n "$val" ] && [ "$val" != "$NOTE_SPEC" ]; then
      printf '  ✗ %s：規格為「%s」，本腳本只認「%s」\n' "${f#$ROOT/}" "$val" "$NOTE_SPEC"; rc=1
    fi

    status="$(field "$f" 狀態 || true)"
    if [ -n "$status" ] && ! printf '%s' " $VALID_STATUS " | grep -q " $status "; then
      printf '  ✗ %s：狀態「%s」不在 {%s}\n' "${f#$ROOT/}" "$status" "$VALID_STATUS"; rc=1
    fi

    if ! grep -q '^## 結論$' "$f"; then
      printf '  ✗ %s：缺「## 結論」段落\n' "${f#$ROOT/}"; rc=1
    fi
    if grep -q '待填' "$f"; then
      printf '  ✗ %s：仍有「待填」佔位未處理\n' "${f#$ROOT/}"; rc=1
    fi

    title="$(field "$f" 標題 || true)"
    if [ -n "$title" ]; then
      if printf '%s\n' "$titles" | grep -Fxq "$title"; then
        printf '  ✗ %s：標題「%s」與其他筆記重複\n' "${f#$ROOT/}" "$title"; rc=1
      fi
      titles="$titles
$title"
    fi

    lines=$(wc -l < "$f" | tr -d ' ')
    if [ "$lines" -gt "$MAX_LINES" ]; then
      warn "${f#$ROOT/}：$lines 行，超過 $MAX_LINES —— 確認是結論不是流水帳"
    fi
  done <<EOF
$(note_files)
EOF

  [ "$rc" -eq 0 ] && printf '檢查通過：%s 篇筆記符合 %s\n' "$count" "$NOTE_SPEC"
  return "$rc"
}

cmd_index() {
  cmd_check
  local tmp cat prev_cat f
  tmp="$(mktemp)"

  {
    printf '# 操作筆記索引\n\n'
    printf '<!-- 規格：%s -->\n\n' "$INDEX_SPEC"
    printf '| 規格 | %s |\n|------|------|\n| 筆記規格 | `%s` |\n| 索引規格 | `%s` |\n\n' \
      "值" "$NOTE_SPEC" "$INDEX_SPEC"
    printf '> 本檔由 `scripts/note.sh index` 自動重建，**請勿手動編輯**。\n'
    printf '> 動手前先查：`scripts/note.sh find <關鍵字>`。\n\n'
  } > "$tmp"

  prev_cat=""
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    cat="$(field "$f" 分類)"
    if [ "$cat" != "$prev_cat" ]; then
      [ -n "$prev_cat" ] && printf '\n' >> "$tmp"
      printf '## %s\n\n| 筆記 | 觸發時機 | 摘要 | 狀態 |\n|------|----------|------|------|\n' "$cat" >> "$tmp"
      prev_cat="$cat"
    fi
    printf '| [%s](%s) | %s | %s | %s |\n' \
      "$(esc "$(field "$f" 標題)")" \
      "$(basename "$f")" \
      "$(esc "$(field "$f" 觸發時機)")" \
      "$(esc "$(field "$f" 摘要)")" \
      "$(esc "$(field "$f" 狀態)")" >> "$tmp"
  done <<EOF
$(note_files | while IFS= read -r f; do [ -n "$f" ] && printf '%s\t%s\n' "$(field "$f" 分類)" "$f"; done | sort | cut -f2-)
EOF

  mv "$tmp" "$INDEX"
  chmod 644 "$INDEX"   # mktemp 產生 600，會與其他筆記權限不一致
  printf '已重建索引：%s\n' "${INDEX#$ROOT/}"
}

cmd_new() {
  local slug="${1:-}"; shift || true
  [ -n "$slug" ] || die "缺 slug，例：flash-size-mismatch"

  local category="" title="" when="" summary="" opt
  while [ $# -gt 0 ]; do
    opt="$1"
    case "$opt" in
      -c) category="${2:-}"; shift 2 ;;
      -t) title="${2:-}"; shift 2 ;;
      -w) when="${2:-}"; shift 2 ;;
      -s) summary="${2:-}"; shift 2 ;;
      *) die "未知參數：$opt" ;;
    esac
  done

  [ -n "$category" ] || die "缺 -c 分類"
  [ -n "$title" ]    || die "缺 -t 標題"
  [ -n "$when" ]     || die "缺 -w 觸發時機"
  [ -n "$summary" ]  || die "缺 -s 摘要"

  mkdir -p "$NOTES_DIR"
  local f="$NOTES_DIR/$slug.md"
  [ -e "$f" ] && die "已存在：${f#$ROOT/}（要更新請直接編輯該檔，再跑 sync）"

  cat > "$f" <<EOF
---
規格: $NOTE_SPEC
標題: $title
分類: $category
觸發時機: $when
摘要: $summary
狀態: 生效
建立日期: $(date +%F)
---

## 結論
待填：直接寫「下次遇到這情境該怎麼做」，一段話，不寫過程。

## 為什麼
待填：根因，一到三點。

## 驗證
待填：可複製的指令或可觀察的值。
EOF

  printf '已建立：%s\n填完正文後跑：scripts/note.sh sync\n' "${f#$ROOT/}"
}

cmd_find() {
  local kw="${1:-}"
  [ -n "$kw" ] || die "缺關鍵字"
  [ -f "$INDEX" ] || die "索引不存在，先跑 scripts/note.sh index"
  local hits
  hits="$(grep -i -- "$kw" "$INDEX" | grep '^|' | grep -v '^|---' || true)"
  if [ -z "$hits" ]; then
    printf '索引中查無「%s」，可直接動手；完成後若有可重用結論再補筆記。\n' "$kw"
  else
    printf '索引命中（%s）：\n%s\n' "$kw" "$hits"
  fi
}

case "${1:-}" in
  new)   shift; cmd_new "$@" ;;
  check) cmd_check ;;
  index) cmd_index ;;
  sync)  cmd_index ;;
  find)  shift; cmd_find "$@" ;;
  ""|-h|--help|help) usage ;;
  *) usage; exit 1 ;;
esac
