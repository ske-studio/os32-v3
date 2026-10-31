# include/gfx_format.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: GFX_MODES_DESIGN (D1 で移した)
ソース: `include/gfx_format.h`

## 2. 形式の層

| 形式 | bpp | 色の意味 | パレット項目数 | 論理 1 行 / 面数 |
|---|---:|---|---:|---|
| GFX_FMT_PLANAR4 (=0) | G0 の 4 | indexed | G0 の 16 | (width+7)/8 bytes **各プレーン**、4 面 |
| GFX_FMT_PACKED8 (=1) | G0 の 8 | indexed | G0 の 256 | width bytes、1 面 |

- bpp/8 の一律計算は禁止。PLANAR4 の 640 幅は 80 bytes×4、320 bytes の単一面ではない。
テーブル/関数は static inline でビルド登録を増やさない。

```c
typedef struct GfxFormatDesc {
    u8 bpp, indexed, planes, bits_per_plane;
    u16 palette_count;
} GfxFormatDesc;
static inline int gfx_format_desc(u8 fmt, GfxFormatDesc *out);
static inline int gfx_format_row(u8 fmt, u32 width, u32 *row_bytes);
/* 既知形式を switch で解く。未知形式・0幅・計算超過は 0、成功は 1。 */
```

indexed の意味と収納方式を分け、palette_count=1<<bpp を汎用規則にしない。
