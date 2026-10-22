# [P1] 候補操作の拡充

> 索引: [`00_INDEX.md`](00_INDEX.md) ／ 前提: [`00_INDEX.md` §1 描画抽象化](00_INDEX.md#1-全体アーキテクチャ方針-共通設計基盤)
> 優先度: **高**

[`FEP_FUTURE.md`](FEP_FUTURE.md)「候補操作の拡充」3 項目 (↑↓リスト送り / 数字ダイレクト選択 / ページング) の実装済み設計。
共通の描画抽象と状態機械も本書にまとめる。コードスケッチは設計の説明であり、現行実装は参照先のソースを読む。

---

## 共通設計の前提

| 前提 | 内容 |
|------|------|
| シングルタスク | FEP ON 時はアプリのメインループが `ime_getkey()` 内でブロックする。割り込み駆動の非同期描画は不要 |
| メモリ制約 | カーネルヒープ + MEMSYS5。現行の MEMSYS5 容量は `lib/sqlite3/os32_sqlite_vfs.c:34`、v3 の計画予算は [MEMMAP_V3 §4-6](../../decisions/MEMMAP_V3.md#4-6-sqlite-の予算と-fep-の取り分-d27ユーザー決定-2026-09-30) を参照。`IME_State` は `static` グローバル 1 個 (BSS) |
| 描画経路 | [CONSTRAINTS.md HW1](../../CONSTRAINTS.md#hw1-egc--grcg--gdc-描画コマンドを使わない) に従う。GFX 計画の実装時も HAL の描画経路に合わせる |
| 既存 API 互換維持 | `ime_getchar()` / `ime_getkey()` のシグネチャと戻り値仕様は変更しない。KAPI バイナリ互換を壊さない |
| UTF-8 内部表現 | かなバッファ・確定バッファは全て UTF-8。文字境界処理は `utf8_*` を使用 |

### データ構造

`kernel/ime.h:65` の `IME_State` が状態の単一実体。`state/page/per_page/render` は実装済み。

## 描画バックエンドと状態機械

2026-05-30 の共通設計を、実装済み部分と将来計画に分けて記す。
TVRAM 抽象と候補 UI は実装済み。P5 の GVRAM 直接描画案は未着手である。

### 1.1 描画バックエンドの抽象化 (P1 / P5 の土台)

`preedit_draw()` は実装済みの `IME_Render` 関数表を通して描画する。
P5 (GFX モード) では描画先の差し替えを計画している。
P1 の候補リストウィンドウもこの抽象を通すことで、テキスト/グラフィック両モードで再利用できる。

```c
/* ime_render.h — 実装済みの描画バックエンド抽象 */
typedef struct {
    /* 1 セル ANK 文字。x,y はセル座標 (TVRAM 互換 80x25 グリッド) */
    void (*putc)(int x, int y, char ank, u8 color);
    /* 全角 1 文字 (Unicode コードポイント)。戻り値=消費セル幅(1 or 2) */
    int  (*putw)(int x, int y, u32 codepoint, u8 color);
    /* y 行を空白で消去 */
    void (*clear_row)(int y, u8 color);
    /* バックエンド固有: 描画開始/終了 (GFX 退避・復元フック) */
    void (*begin)(void);
    void (*end)(void);
} IME_Render;
```

- テキストモード実装 (`ime_render_tvram.c`): 既存の `tvram_putchar_at` / `tvram_putkanji_at` / `unicode_to_jis` をラップ。
- 将来のグラフィックモード実装 (`ime_render_gfx.c`): GVRAM プレーンへの直接ビットマップ転送 ([`05_GFX_MODE.md`](05_GFX_MODE.md))。

`kernel/ime.c:592` で TVRAM 実装を選択する。描画表の定義は `kernel/ime_render.h:10`。

### 1.2 状態機械の明確化

候補リスト UI の導入時に、以下の状態定数を追加した (`kernel/ime.h`)。

```c
#define IME_ST_INPUT      0   /* かな入力中 (未変換) */
#define IME_ST_CONVERT    1   /* 変換候補選択中 (インライン ▼ 表示) */
#define IME_ST_CANDLIST   2   /* 候補リストウィンドウ展開中 (P1 で追加) */
```

`converting` は `state != IME_ST_INPUT` と等価。現行実装は `g_ime.converting` と `g_ime.state` を併用し、変換開始・終了時に同期する。
将来の連文節変換 ([`07_BUNSETSU.md`](07_BUNSETSU.md)) では `IME_ST_BUNSETSU` を追加できる構造にしておく。

---

## 1. UI 仕様

2 段階 UI とする。

| 状態 | 表示 | 遷移契機 |
|------|------|---------|
| `IME_ST_CONVERT` | 25 行目にインライン `▼候補(01/12)` (現状維持) | Space で変換開始 |
| `IME_ST_CANDLIST` | 画面下部に候補リストウィンドウ (番号付き 1 ページ 9 件) | 候補選択中に ↑ / ↓ / XFER 押下 |

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
| `KEY_UP` (0x3A)    | リスト展開 → 前候補 (idx--, 循環) | 前候補 |
| `KEY_XFER` (0x35)  | リスト展開 → CANDLIST へ | 次候補 |
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

TVRAM ではリスト展開前に `candlist_save()` が候補領域の文字と属性を退避し、
畳む / 確定する際に `candlist_clear()` → `candlist_restore()` で復元する
(`kernel/ime.c:76`、`kernel/ime.c:98`、`kernel/ime.c:120`)。退避済みフラグで二重退避を防ぐ。
退避バッファが無い場合は保存しないため、その場合の復元は保証されない。
スクロールの予約行制御は [P2 §4](02_SCROLL_GUARD.md#4-fep-からの制御)、
未実装の GVRAM 退避・復元案は [P5 §3](05_GFX_MODE.md#3-gfx-vram-退避復元) を参照。

---

## 7. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `ime.h` | `IME_ST_*` 定義、`IME_Render` 前方宣言、`state/page/per_page` フィールド追加 |
| `ime.c` | `ime_process_key` の候補ブロック拡張、`candlist_draw/clear`、`draw_utf8` 抽出、`ime_init` で `per_page=9` |
| `ime_render_tvram.c` (新規) | 描画バックエンド TVRAM 実装 ([`00_INDEX.md` §1.1](00_INDEX.md#11-描画バックエンドの抽象化-p1--p5-の土台)) |
| `ime_render.h` (新規) | `IME_Render` 定義 |
| `build/kernel.mk` | `C_KERNEL` に `ime_render_tvram.c` 追加 |

**テスト手順** (NP21/W リモート):
1. `ime` ON → "かんじ" 入力 → Space で `IME_ST_CONVERT` 確認。
2. Space 連打で循環、`(NN/MM)` カウンタ増加を確認。
3. ↓ でリスト展開、数字 1〜9 で該当候補が即確定することを確認。
4. XFER で次候補、ROLLDOWN でページ送り、端数ページ (4 ページ目) の件数が正しいこと。
5. ESC でリストを畳み、CONVERT に戻ること。`man` 等のアプリ画面が破壊されないこと。
6. [ビルド・検証手順](../../08_build.md#ビルドターゲット) (§8-4) に従って確認する。

---

*前: (なし) ／ 次: [`02_SCROLL_GUARD.md`](02_SCROLL_GUARD.md) ／ 索引: [`00_INDEX.md`](00_INDEX.md)*
