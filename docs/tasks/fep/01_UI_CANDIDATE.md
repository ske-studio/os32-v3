# [P1] 候補操作の拡充

> 索引: [`00_INDEX.md`](00_INDEX.md) ／ 前提: [`00_INDEX.md` §1 描画抽象化](00_INDEX.md#1-全体アーキテクチャ方針-共通設計基盤)
> 優先度: **高**

[`FEP_FUTURE.md`](FEP_FUTURE.md)「候補操作の拡充」3 項目 (↑↓リスト送り / 数字ダイレクト選択 / ページング) を実装する。
本フェーズは [`00_INDEX.md` §1.1](00_INDEX.md#11-描画バックエンドの抽象化-p1--p5-の土台) の描画抽象化リファクタリングを起点とする。

---

## 1. UI 仕様

2 段階 UI とする。

| 状態 | 表示 | 遷移契機 |
|------|------|---------|
| `IME_ST_CONVERT` | 25 行目にインライン `▼候補(01/12)` (現状維持) | Space で変換開始 |
| `IME_ST_CANDLIST` | 画面下部に候補リストウィンドウ (番号付き 1 ページ 9 件) | 候補選択中にさらに ↓ または XFER 押下 |

`IME_ST_CONVERT` で Space 連打すると従来通りインラインで次候補に送る。
**↓ を押した時点でリストウィンドウを展開** し、`IME_ST_CANDLIST` へ遷移する。
これにより「軽い変換は Space だけ」「迷ったらリスト表示」の二段構えになる。

状態列挙 `IME_ST_*` の定義は [`00_INDEX.md` §1.2](00_INDEX.md#12-状態機械の明確化) を参照。

---

## 2. データ構造変更 (`ime.h`)

`IME_State` に以下を追記する (構造体末尾、BSS なので初期化は `ime_init` の `kmemset` で 0)。

```c
    int  state;          /* IME_ST_xxx */
    int  page;           /* 候補リストの現在ページ (0 始まり) */
    int  per_page;       /* 1 ページの候補数 (既定 9) */
```

`IME_MAX_RESULTS` は 32 のまま。1 ページ 9 件なので最大 4 ページ (32/9=3.55→4)。

---

## 3. キーバインド設計

`ime_process_key()` の「変換候補表示中の操作」ブロックを拡張する。

| キー (scancode) | `IME_ST_CONVERT` | `IME_ST_CANDLIST` |
|-----------------|------------------|--------------------|
| `KEY_SPACE` (0x34) | 次候補 (idx++, 循環) | 次候補 (ページ跨ぎで page 自動更新) |
| `KEY_DOWN` (0x3D)  | **リスト展開** → CANDLIST へ | 次候補 |
| `KEY_UP` (0x3A)    | 前候補 (idx--, 循環) | 前候補 |
| `KEY_XFER` (0x35)  | リスト展開 → CANDLIST へ | 次ページ |
| `'1'..'9'` (ascii) | (透過: 未変換時の通常入力) | **ページ内 n 番目を即確定** |
| `KEY_ROLLDOWN`(0x37) / `KEY_RIGHT`(0x3C) | — | 次ページ |
| `KEY_ROLLUP`(0x36) / `KEY_LEFT`(0x3B)    | — | 前ページ |
| `KEY_RETURN` (0x1C) | 候補確定 | 候補確定 |
| `KEY_BS` / `ESC`    | 変換キャンセル | CONVERT に戻す (リストを畳む) |
| その他英字           | 候補確定後フォールスルー | 候補確定後フォールスルー |

> **数字キーの扱いの注意:** `IME_ST_CONVERT` (インライン) では数字を「通常入力」に通す。
> 候補リスト展開中 (`IME_ST_CANDLIST`) のみ数字を選択キーとして奪う。
> これにより「ばん1」のような数字混じり入力を壊さない。

`KEY_XFER` (変換) / `KEY_NFER` (無変換) の割り当ては [`06_MISC.md` §1](06_MISC.md) と整合させる。

---

## 4. 候補インデックス計算

ページとインデックスの関係:

```c
/* 現在ページの先頭候補グローバル index */
#define IME_PAGE_BASE(s)  ((s)->page * (s)->per_page)
/* 現在ページの候補数 (末尾ページは端数) */
static int page_count(const IME_State *s)
{
    int base = s->page * s->per_page;
    int rem  = s->result_count - base;
    return (rem > s->per_page) ? s->per_page : rem;
}
```

数字キー `n` (1..9) 押下時の確定対象:

```c
/* n は 1 始まり */
int target = s->page * s->per_page + (n - 1);
if (target < s->result_count) {
    s->candidate_idx = target;
    commit_candidate();   /* 既存の確定処理を再利用 */
}
```

Space による次候補送りでページを自動追従させる:

```c
s->candidate_idx++;
if (s->candidate_idx >= s->result_count) s->candidate_idx = 0;
s->page = s->candidate_idx / s->per_page;   /* 表示ページを同期 */
```

---

## 5. 候補リストウィンドウ描画 (`preedit_draw` 拡張)

`IME_ST_CANDLIST` のとき、画面下部に番号付きリストを描く。
テキストモードでは 25 行目 (インジケータ) の **上**、行 `24-1-rows` 〜 `23` を使う。

```
   行19: ┌─候補─────────────┐     ← 枠 (任意)
   行20: │1 漢字  2 感じ  3 幹事│
   行21: │4 監事  5 ...        │
   ...
   行24: [あ]▼漢字(03/12)        ← 既存インライン行 (維持)
```

実装方針:
- 1 行に 3 件ずつ並べる横展開 (9 件 → 3 行)。番号はシアン、候補本体は白、選択中はハイライト (反転 `tvram_reverse_cell` か別色)。
- 描画は [`00_INDEX.md` §1.1](00_INDEX.md#11-描画バックエンドの抽象化-p1--p5-の土台) の `IME_Render` 経由 (`g_ime.render->...`)。
- 描画関数を新設し `preedit_draw()` から分岐:

```c
static void candlist_draw(void)
{
    int rows = (g_ime.per_page + 2) / 3;     /* 3 件/行 */
    int top  = IME_PREEDIT_ROW - rows;        /* インライン行の上 */
    int base = g_ime.page * g_ime.per_page;
    int n    = page_count(&g_ime);
    int i;
    int y, x;

    for (i = 0; i < n; i++) {
        int gi = base + i;                    /* グローバル候補 index */
        u8  col = (gi == g_ime.candidate_idx) ? ATTR_YELLOW : ATTR_WHITE;
        y = top + (i / 3);
        x = (i % 3) * 26;                     /* 3 列 × 26 桁 */
        /* 番号 "1 " */
        g_ime.render->putc(x, y, '1' + i, ATTR_CYAN);
        g_ime.render->putc(x + 1, y, ' ', col);
        /* 候補文字列 (utf8 → putw) */
        draw_utf8(x + 2, y, g_ime.results[gi].kanji, col);
    }
    /* ページ位置 "1/4" を末尾に */
    ...
}
```

`draw_utf8(x, y, str, col)` は既存 `preedit_draw` 内のデコードループ
(`utf8_decode` → `unicode_to_ank` / `unicode_to_jis`) を関数化して共用する。
この `draw_utf8` は GFX モード ([`05_GFX_MODE.md`](05_GFX_MODE.md)) でもそのまま再利用される。

---

## 6. 状態遷移後のクリア

リストを畳む / 確定する際は、リスト領域 (複数行) を `clear_row` で消去する必要がある。
`preedit_clear()` を拡張し「直前にリスト展開していたら全行クリア」する:

```c
static void candlist_clear(void)
{
    int rows = (g_ime.per_page + 2) / 3;
    int y;
    for (y = IME_PREEDIT_ROW - rows; y < IME_PREEDIT_ROW; y++) {
        g_ime.render->clear_row(y, ATTR_WHITE);
    }
}
```

> **TVRAM スクロールとの干渉:** リスト領域はアプリの出力行 (0〜23) と重なる。
> リストを畳んだ瞬間にアプリ画面が欠ける問題があるため、本来は退避が必要。
> 当面はテキストモードでは「リスト表示中はアプリ出力が一時的に隠れる」許容仕様とし、
> 畳んだ後にアプリ側が再描画する想定 (シェルはプロンプト再描画で復帰)。
> 完全な退避・復元は [`02_SCROLL_GUARD.md`](02_SCROLL_GUARD.md) / [`05_GFX_MODE.md`](05_GFX_MODE.md) の枠組みを流用して将来対応。

---

## 7. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `ime.h` | `IME_ST_*` 定義、`IME_Render` 前方宣言、`state/page/per_page` フィールド追加 |
| `ime.c` | `ime_process_key` の候補ブロック拡張、`candlist_draw/clear`、`draw_utf8` 抽出、`ime_init` で `per_page=9` |
| `ime_render_tvram.c` (新規) | 描画バックエンド TVRAM 実装 ([`00_INDEX.md` §1.1](00_INDEX.md#11-描画バックエンドの抽象化-p1--p5-の土台)) |
| `ime_render.h` (新規) | `IME_Render` 定義 |
| `build.sh` | `C_KERNEL` に `ime_render_tvram.c` 追加 |

**テスト手順** (NP21/W リモート):
1. `ime` ON → "かんじ" 入力 → Space で `IME_ST_CONVERT` 確認。
2. Space 連打で循環、`(NN/MM)` カウンタ増加を確認。
3. ↓ でリスト展開、数字 1〜9 で該当候補が即確定することを確認。
4. XFER / ROLLDOWN でページ送り、端数ページ (4 ページ目) の件数が正しいこと。
5. ESC でリストを畳み、CONVERT に戻ること。`man` 等のアプリ画面が破壊されないこと (許容範囲内)。
6. `build.sh` エラー 0。

---

*前: (なし) ／ 次: [`02_SCROLL_GUARD.md`](02_SCROLL_GUARD.md) ／ 索引: [`00_INDEX.md`](00_INDEX.md)*
