# drivers/video/pc98/backend_pc98.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DESIGN (D1 で移した)
ソース: `drivers/video/pc98/backend_pc98.c`

- OS32 は主記憶のバックバッファに描いて `gfx_present_rect` で転送する方式なので、
  GRCG/EGC は転送量を減らせない (多色データはプレーンごとに別)。[HW1] の
  「CPU 直書き」はこの構造では唯一の選択肢であり、失っているのは VRAM 内
  スクロールと VRAM 直接の単色塗りだけ (どちらもバックバッファ側で済む)。

## 3. 対象ハードウェア

| バックエンド | 画素形式 | VRAM | NP21/W で検証 |
|---|---|---|---|
| 9801 標準 (16 色, 640x400) | 4 プレーン | 0xA8000〜 (現行) | 可 (現行) |

| 観点 | 9801 プレーン |
|---|---|
| present 1 ピクセルあたりの VRAM 書き込み | 0.5 バイト |

```c
/* 各 backend_*.c が所有。PC98_WIDTH 等は既存定数の例示。 */
static const GfxMode pc98_modes[] = {
    {GFX_WIDTH, GFX_HEIGHT,     GFX_FMT_PLANAR4, 4, 0, 0, GFX_BPL},
    {GFX_WIDTH, GFX_HEIGHT_200, GFX_FMT_PLANAR4, 4, 1, 0, GFX_BPL}
};
```

- 宣言以外の clip・転送 pitch・clear 範囲と query の幅は gfx_backend_mode() から得る。

- 98 の 16 色のバックエンドは init の関数を持たない (gfx_core.c が init=NULL を見て GDC を初期化する)。
