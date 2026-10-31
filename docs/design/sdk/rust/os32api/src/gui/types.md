# sdk/rust/os32api/src/gui/types.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `sdk/rust/os32api/src/gui/types.rs`

## G. 描画 (GDI 相当) — HAL のプリミティブと同じ粒度

### G1. 基本型

| 型 | 定義 | 備考 |
|---|---|---|
| `Rect` | `{x: i16, y: i16, w: i16, h: i16}` | 空は `w<=0 \|\| h<=0`。座標は描画先サーフェスのローカル |
| `Color` | `u8` | 0〜15 はシステム色 (G6)。16〜255 は 256 色バックエンドでのみ有効 |
| `Style` | `{fg: Color, bg: Color, flags: u8}` | flags は G6 で確定: `TRANSPARENT_BG` / `XOR` / `DOTTED` / `DITHER50` |
| `SurfaceId` | `u32` (index:16 \| generation:16、0 = 無効) | ウィンドウのクライアント面、またはオフスクリーン |
| `BitmapId` | 同上 | 16 色版必須、256 色版任意 (DESIGN §2 色方針) |
| `FontId` | 同上 | v1 は KCG 固定 2 種: `FONT_ANK` 8x16 / `FONT_KANJI` 16x16 (自動選択) |

> [要確認] KCG 固定2種という限定は、現行 userland/rust/libos32gui/src/draw.rs のフォント選択と要照合。

`Style.flags` (u8) — 確定: `TRANSPARENT_BG` (背景を塗らず文字だけ) / `XOR` (枠ドラッグ) /
`DOTTED` (フォーカス矩形の点線) / `DITHER50` (50% 市松塗り。無効面や 16 色での中間調)。
