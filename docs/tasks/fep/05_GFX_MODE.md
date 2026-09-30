# [P5] GFX モード対応

> 索引: [`00_INDEX.md`](00_INDEX.md) ／ 前提: [`00_INDEX.md` §1 描画抽象化](00_INDEX.md#11-描画バックエンドの抽象化-p1--p5-の土台)
> 優先度: **高(大規模)** ／ 他フェーズと切り離して最後に実施

[`FEP_FUTURE.md`](FEP_FUTURE.md)「GFX モード対応」。最大規模の変更。グラフィックモードアプリ
(`gfx_demo` 等) で FEP プリエディットを表示する。

---

## 1. 課題

- プリエディット UI は TVRAM 25 行目前提。GFX モード中はテキスト VRAM が
  非表示 (or グラフィックと排他) のため見えない。
- GFX 画面下部にオーバーレイ描画すると、アプリのフレームバッファ内容を上書きする
  → 退避・復元が必要。

---

## 2. 全体アーキテクチャ

[`00_INDEX.md` §1.1](00_INDEX.md#11-描画バックエンドの抽象化-p1--p5-の土台) の描画バックエンド抽象 `IME_Render` を GFX 実装で差し替える。
表示領域は**画面最下部の 16 ライン (Y=384〜399)**、フォントは漢字 ROM 16x16。

```
┌──────────────────────────────┐
│   アプリのグラフィック描画 (Y=0..383)     │
├──────────────────────────────┤ ← Y=384
│  FEP オーバーレイ (16 ライン)  [あ]▼漢字  │ ← begin() で退避、end() で復元
└──────────────────────────────┘ ← Y=399
```

---

## 3. GFX VRAM 退避・復元

FEP 表示開始時にオーバーレイ領域 (Y=384〜399, 16 ライン × 4 プレーン) を退避し、
終了時に書き戻す。

```c
/* ime_render_gfx.c */
#define OVL_Y0      384
#define OVL_LINES   16
#define OVL_BPL     80                       /* bytes/line/plane (GFX_BPL) */
#define OVL_PLANE   (OVL_BPL * OVL_LINES)    /* 1280 bytes/plane */
#define OVL_SAVE_SZ (OVL_PLANE * 4)          /* 5120 bytes (FEP_FUTURE 記載値と一致) */

static u8 *g_ovl_save = 0;   /* mem_alloc(5120) */

static const u32 plane_base[4] = {
    VRAM_PLANE_B, VRAM_PLANE_R, VRAM_PLANE_G, VRAM_PLANE_I
};

static void gfx_ovl_save(void)
{
    int pl, ln;
    if (!g_ovl_save) g_ovl_save = (u8 *)mem_alloc(OVL_SAVE_SZ);
    for (pl = 0; pl < 4; pl++) {
        volatile u8 *src = (volatile u8 *)(plane_base[pl] + (u32)OVL_Y0 * OVL_BPL);
        u8 *dst = g_ovl_save + pl * OVL_PLANE;
        for (ln = 0; ln < OVL_PLANE; ln++) dst[ln] = src[ln];
    }
}

static void gfx_ovl_restore(void)
{
    int pl, ln;
    if (!g_ovl_save) return;
    for (pl = 0; pl < 4; pl++) {
        u8 *src = g_ovl_save + pl * OVL_PLANE;
        volatile u8 *dst = (volatile u8 *)(plane_base[pl] + (u32)OVL_Y0 * OVL_BPL);
        for (ln = 0; ln < OVL_PLANE; ln++) dst[ln] = src[ln];
    }
}
```

`IME_Render.begin` = `gfx_ovl_save` + 黒塗り、`IME_Render.end` = `gfx_ovl_restore`。

> **重要:** OS32 の GFX は通常バックバッファ→`gfx_present()` 転送方式 (gfx.h)。
> しかし FEP オーバーレイは**表示中の VRAM へ直接描く**必要がある (アプリは present で
> オーバーレイ領域も上書きしうる)。アプリが Y=384〜399 を present する瞬間に
> オーバーレイが消えるため、`gfx_present_rect` で**オーバーレイ行を除外**できるよう
> アプリ側に協調を求めるか、FEP ON 中は論理画面を 384 ラインに縮める運用とする。
> → 現実解: **FEP ON 中はアプリのメインループが停止する** ([`00_INDEX.md` §0](00_INDEX.md#0-設計の前提条件) シングルタスク前提) ため、
> present は走らない。オーバーレイ直描き + 終了時復元で十分。アプリ復帰時に
> アプリが自前で再 present すれば残像も消える。

---

## 4. 漢字 ROM ビットマップ直接描画

GFX モードでは TVRAM の漢字表示機構が使えないため、漢字 ROM から 16x16 ビットマップを
読み出し、各プレーンへ転送する。既存の `gfx_draw_font()` / `kcg_read_kanji()` を活用。

```c
/* Unicode → JIS → 16x16 bitmap → GVRAM */
static int gfx_putw(int cx, int cy, u32 cp, u8 color)
{
    u16 jis;
    u8  pat[32];                 /* 16x16 = 2 bytes * 16 lines */
    int px = cx * 8;             /* セル→ピクセル (ANK 8px 基準) */
    int py = OVL_Y0;             /* オーバーレイ行固定 */
    jis = unicode_to_jis(cp);
    if (!jis) return 1;
    kcg_read_kanji(jis, pat);    /* 漢字 ROM から 32 バイト取得 */
    /* 直接 VRAM へ: gfx_draw_font はバックバッファ前提のため VRAM 版を用意 */
    ovl_draw_glyph(px, py, pat, 2 /*w_bytes*/, 16 /*h*/, color);
    return 2;                    /* 全角 = 2 セル */
}
```

ANK (半角) は `kcg_read_ank()` で 8x16、1 セル。
`ovl_draw_glyph()` は VRAM 直書きラスタライザ (アプリの `gfx_draw_font` の VRAM 版):

```c
static void ovl_draw_glyph(int px, int py, const u8 *pat, int wb, int h, u8 color)
{
    int pl, ln, b;
    int byte_x = px >> 3;
    /* color の各ビットをプレーンごとに展開 */
    for (pl = 0; pl < 4; pl++) {
        int on = (color >> pl) & 1;
        if (!on) continue;       /* そのプレーンは触らない (前景のみ) */
        for (ln = 0; ln < h; ln++) {
            volatile u8 *row = (volatile u8 *)
                (plane_base[pl] + (u32)(py + ln) * OVL_BPL + byte_x);
            for (b = 0; b < wb; b++) row[b] |= pat[ln * wb + b];
        }
    }
}
```

> 背景 (黒) は `begin()` の黒塗りで処理。前景色のビットだけ OR するため
> アンチエイリアスなしの単色描画。座標は 8px 境界 (byte 境界) に揃え、
> シフト描画は不要 (セルグリッドが 8px 単位のため)。
> UTF-8 デコードループ自体は [`01_UI_CANDIDATE.md` §5](01_UI_CANDIDATE.md) の `draw_utf8` を共用し、
> 末端の `putc`/`putw` だけが本実装に差し替わる。

---

## 5. テキスト/グラフィック混在制御 (任意・将来)

[`FEP_FUTURE.md`](FEP_FUTURE.md) はテキスト VRAM とグラフィック VRAM の同時表示
(GDC 画面モード `0x68` 系) も挙げている。これにより GFX 画面の上に
**テキスト VRAM のプリエディット行をそのまま重ねる**選択肢がある。

| 方式 | 長所 | 短所 |
|------|------|------|
| A: GVRAM 直描き (§3-4) | 確実・GDC 設定に非依存 | 退避/復元コスト、漢字 ROM 描画実装 |
| B: テキスト/グラフィック混在表示 | 既存 TVRAM 描画を流用できる | GDC モード制御が機種/エミュ依存、ハードウェアアクセラレータ禁止規定との整合確認要 |

→ **採用は方式 A**。方式 B は GDC のテキスト表示 ON 制御が
「ハードウェアアクセラレータ使用禁止」(CLAUDE.md) の精神に照らしてリスクがあり、
NP21/W 互換も未検証のため将来検討に留める。`tvram_set_visible()` の新設は方式 B 採用時のみ。

---

## 6. バックエンド選択ロジック

FEP 表示時にどちらの `IME_Render` を使うか判定する。
GFX モードかどうかは `gfx` サブシステムの状態 (例: `gfx_is_active()` を新設) で判断:

```c
void ime_select_render(void)
{
    if (gfx_is_active())  g_ime.render = &ime_render_gfx;
    else                  g_ime.render = &ime_render_tvram;
}
```

`ime_toggle()` の ON 時、および `ime_set_mode()` で呼ぶ。
`gfx_is_active()` は `gfx_core.c` に `g_gfx_initialized` フラグを公開する形で追加。

---

## 7. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `ime_render.h` | `IME_Render` 定義 ([`00_INDEX.md` §1.1](00_INDEX.md#11-描画バックエンドの抽象化-p1--p5-の土台)) |
| `ime_render_tvram.c` (新規) | テキスト実装 |
| `ime_render_gfx.c` (新規) | GFX 実装 (退避/復元/グリフ描画) |
| `ime.c` | `render` ポインタ経由描画化、`ime_select_render` |
| `gfx_core.c` / `gfx.h` | `int gfx_is_active(void);` 公開 |
| `build.sh` | 新規 .c を `C_KERNEL` に追加 |

**テスト:** `gfx_demo` 起動中に Shift+Space で FEP ON → 画面最下部 16 ラインに
`[あ]` と入力かな・変換候補が表示され、FEP OFF で元のグラフィックが復元されること。
`build.sh` エラー 0。

---

*前: [`04_DICT_QUALITY.md`](04_DICT_QUALITY.md) ／ 次: [`06_MISC.md`](06_MISC.md) ／ 索引: [`00_INDEX.md`](00_INDEX.md)*
