# [P2] TVRAM スクロール保護

> 索引: [`00_INDEX.md`](00_INDEX.md)
> 優先度: **高**

[`FEP_FUTURE.md`](FEP_FUTURE.md)「TVRAM スクロール保護」。FEP 表示行 (25 行目 = 行 24) を
アプリ出力のスクロールが巻き込む問題を解消する。本フェーズは他フェーズに依存しない。

---

## 1. 課題

現在の `kernel/console.c::tvram_scroll()` は `TVRAM_ROWS`(25) 行全体を 1 行上にシフトする。
FEP ON 時、25 行目のプリエディット表示がスクロールで吸い上げられ、表示が乱れる。

```c
/* 現状: 0..23 行を引き上げ、24 行目を空白化 → 24 行目(FEP)も巻き込む */
void tvram_scroll(void)
{
    for (i = 0; i < TVRAM_COLS * (TVRAM_ROWS - 1); i++) {
        text[i] = text[i + TVRAM_COLS];   /* 行 24 の内容が行 23 に来る */
        ...
    }
}
```

---

## 2. 設計: スクロール行数の動的制限

`tvram_scroll()` に「保護行数」を導入する。FEP ON 時は最終行を保護し、
スクロール対象を 0〜23 行に限定する。

```c
/* console.c: 保護行数 (下から数えた固定行)。0=従来通り全行 */
static int g_scroll_reserve = 0;

void tvram_set_scroll_reserve(int rows)   /* 公開 API (tvram.h) */
{
    if (rows < 0) rows = 0;
    if (rows >= TVRAM_ROWS) rows = TVRAM_ROWS - 1;
    g_scroll_reserve = rows;
}

void tvram_scroll(void)
{
    volatile u16 *text = (volatile u16 *)TVRAM_TEXT;
    volatile u16 *attr = (volatile u16 *)TVRAM_ATTR;
    int rows = TVRAM_ROWS - g_scroll_reserve;   /* 実スクロール行数 */
    int i;

    /* 0 .. rows-1 を 1 行引き上げ */
    for (i = 0; i < TVRAM_COLS * (rows - 1); i++) {
        text[i] = text[i + TVRAM_COLS];
        attr[i] = attr[i + TVRAM_COLS];
    }
    /* 新規行 (rows-1 行目) を空白化 */
    for (i = TVRAM_COLS * (rows - 1); i < TVRAM_COLS * rows; i++) {
        text[i] = 0x0020;
        attr[i] = ATTR_WHITE;
    }
}
```

---

## 3. console カーソル境界の整合

`console.c` 内の各所にある折返し処理:

```c
if (cursor_y >= TVRAM_ROWS) { tvram_scroll(); cursor_y = TVRAM_ROWS - 1; }
```

これを保護行を考慮した境界に変更する。新マクロ `CONSOLE_LAST_ROW` を導入:

```c
#define CONSOLE_LAST_ROW  (TVRAM_ROWS - 1 - g_scroll_reserve)
...
if (cursor_y > CONSOLE_LAST_ROW) {
    tvram_scroll();
    cursor_y = CONSOLE_LAST_ROW;
}
```

`g_scroll_reserve == 0` のとき従来と完全に同一挙動 (`CONSOLE_LAST_ROW == 24`)。
`console.c` には同形の折返しが複数箇所 (putchar / puts / kprintf 系) にあるため、
全箇所をこの境界式に統一する。

---

## 4. FEP からの制御

`ime_toggle()` / `ime_set_mode()` で予約行を切り替える:

```c
void ime_toggle(void) {
    if (g_ime.mode == IME_MODE_OFF) {
        ... /* 辞書ロード */
        tvram_set_scroll_reserve(1);   /* 25 行目を保護 */
        ...
    } else {
        ...
        tvram_set_scroll_reserve(0);   /* 保護解除 */
        preedit_clear();
    }
}
```

> 候補リストウィンドウ ([`01_UI_CANDIDATE.md` §5](01_UI_CANDIDATE.md)) を TVRAM で多行表示する間は `reserve = rows+1` に
> 一時拡張し、畳んだら 1 に戻す、という拡張も可能。ただしアプリ表示領域が削れるため、
> リスト展開中のみ動的に増やす運用とする。

---

## 5. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `include/tvram.h` | `void tvram_set_scroll_reserve(int rows);` 宣言追加 |
| `kernel/console.c` | `g_scroll_reserve`、`tvram_scroll` 改修、カーソル境界 `CONSOLE_LAST_ROW` 化 |
| `kernel/ime.c` | `ime_toggle`/`ime_set_mode` で予約行設定 |

**テスト:** FEP ON のまま `ls` 等で画面を最下行までスクロールさせ、25 行目の `[あ]`
インジケータが保持され続けること。FEP OFF 後は 25 行目までスクロールが及ぶ (従来挙動) こと。
`build.sh` エラー 0。

---

*前: [`01_UI_CANDIDATE.md`](01_UI_CANDIDATE.md) ／ 次: [`03_DICT_COMMAND.md`](03_DICT_COMMAND.md) ／ 索引: [`00_INDEX.md`](00_INDEX.md)*
