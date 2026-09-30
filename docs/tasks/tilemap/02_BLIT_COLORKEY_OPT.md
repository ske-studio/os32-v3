# gfx_blit_colorkey 高速化リファクタリング

## 1. 現状の問題

[gfx_surface.c L151-183](../../../userland/lib/gfx/draw/gfx_surface.c) の
`gfx_blit_colorkey()` はピクセル単位のネストループで実装されている:

```c
for (iy = 0; iy < sh; iy++) {
    for (ix = 0; ix < sw; ix++) {
        /* 4プレーンから1ピクセルの色を再構築 */
        /* colorkey と比較 → 不一致なら gfx_pixel() で描画 */
    }
}
```

### ボトルネック分析

1ピクセルあたりの処理:
- 4プレーンからビット読み取り: 4回のメモリ読み + ビットシフト
- 色の再構築: 4回のOR演算
- colorkey比較: 1回
- `gfx_pixel()` 呼び出し: 関数コール + 再びビット操作で書き込み

16×16タイル1枚 = 256ピクセル × 上記処理 → **非常に遅い**。

対する `gfx_blit()` はバイト境界時に `asm_copy_plane_rect` (rep movsw) で
プレーン単位の矩形ブロック転送を行うため、桁違いに高速。

---

## 2. 高速化方針

### 2-1. バイト単位のマスク生成方式

色0(透明) = 全プレーンのビットが0。
つまり `planes[0] | planes[1] | planes[2] | planes[3]` のビットが0の位置が透明ピクセル。

```c
/* 1バイト内の不透明マスク生成 */
u8 opaque_mask = src->planes[0][off]
               | src->planes[1][off]
               | src->planes[2][off]
               | src->planes[3][off];

/* opaque_mask のビットが 1 のピクセルだけ BB に書き込む */
```

これにより:
- colorkey=0 に限定すれば、ピクセル単位のループが不要
- バイト単位 (8ピクセル同時) で透明/不透明判定
- `opaque_mask == 0xFF` なら全ピクセル不透明 → 無条件バイトコピー
- `opaque_mask == 0x00` なら全ピクセル透明 → 完全スキップ
- 中間値のみビット演算で合成

### 2-2. 合成ロジック

```c
/* バイト単位の透過合成 (colorkey=0専用) */
static void blit_row_masked(u8 *dst_planes[4],
                            const u8 *src_planes[4],
                            int dst_off, int src_off, int bytes)
{
    int i, p;
    for (i = 0; i < bytes; i++) {
        u8 mask = src_planes[0][src_off + i]
                | src_planes[1][src_off + i]
                | src_planes[2][src_off + i]
                | src_planes[3][src_off + i];

        if (mask == 0x00) continue;  /* 全透明 → スキップ */

        if (mask == 0xFF) {
            /* 全不透明 → 無条件コピー */
            for (p = 0; p < 4; p++) {
                dst_planes[p][dst_off + i] = src_planes[p][src_off + i];
            }
        } else {
            /* 部分透明 → ビット合成 */
            u8 keep = ~mask;
            for (p = 0; p < 4; p++) {
                dst_planes[p][dst_off + i] =
                    (dst_planes[p][dst_off + i] & keep)
                    | src_planes[p][src_off + i];
            }
        }
    }
}
```

### 2-3. 32bit化

メインメモリ上のBBでは32bit操作が効率的。
2バイト(16px)ずつではなく、4バイト(32px)ずつ処理する:

```c
/* 32bit幅の透過合成 (4バイト = 32ピクセル同時) */
u32 *sp0 = (u32 *)&src->planes[0][src_off];
u32 *sp1 = (u32 *)&src->planes[1][src_off];
u32 *sp2 = (u32 *)&src->planes[2][src_off];
u32 *sp3 = (u32 *)&src->planes[3][src_off];
u32 mask32 = *sp0 | *sp1 | *sp2 | *sp3;

if (mask32 == 0) continue;           /* 32ピクセル全透明 */
if (mask32 == 0xFFFFFFFF) {          /* 32ピクセル全不透明 */
    *(u32 *)&dst[0][off] = *sp0;
    *(u32 *)&dst[1][off] = *sp1;
    *(u32 *)&dst[2][off] = *sp2;
    *(u32 *)&dst[3][off] = *sp3;
} else {
    u32 keep = ~mask32;
    *(u32 *)&dst[0][off] = (*(u32 *)&dst[0][off] & keep) | *sp0;
    *(u32 *)&dst[1][off] = (*(u32 *)&dst[1][off] & keep) | *sp1;
    *(u32 *)&dst[2][off] = (*(u32 *)&dst[2][off] & keep) | *sp2;
    *(u32 *)&dst[3][off] = (*(u32 *)&dst[3][off] & keep) | *sp3;
}
```

16×16タイルの場合、1行 = 2バイト → 32bit化には隣接タイルとの結合が必要。
これはタイルマップエンジン側で2タイル同時処理として対応する
(01_TILEMAP_DESIGN.md §4-3 参照)。

単独タイルの `gfx_blit_colorkey` としては、16bit (WORD) 単位での最適化が現実的:

```c
u16 mask16 = *(u16 *)&src->planes[0][off]
           | *(u16 *)&src->planes[1][off]
           | *(u16 *)&src->planes[2][off]
           | *(u16 *)&src->planes[3][off];

/* 16ピクセル全透明/全不透明の高速パス */
```

---

## 3. 性能見積もり

### 現行 (ピクセル単位)

16×16タイル:
- 256ピクセル × (4読み取り + 条件分岐 + gfx_pixel呼び出し) ≈ **5000命令**

### 最適化後 (バイト単位)

16×16タイル:
- 16行 × 2バイト × (4読み取り + OR + 条件分岐 + 4書き込み) ≈ **500命令**

→ **約10倍の高速化**。

### NASM版 (将来)

`asm_sprite.asm` の `asm_gfx_draw_sprite_core` と同様のマスク付きバイト合成を
NASM で実装すれば、さらに2-3倍の高速化が見込める。

---

## 4. 実装計画

### Step 1: `gfx_blit_colorkey_fast` の追加

既存の `gfx_blit_colorkey` はそのまま残し（汎用）、
colorkey=0 専用の高速版を新規追加:

```c
/* colorkey=0 専用の高速透過blit */
void gfx_blit_transparent(int dx, int dy,
                           const GFX_Surface *src,
                           const GFX_Rect *src_rect);
```

#### 対象ファイル

- `programs/libos32gfx/draw/gfx_surface.c` — 関数追加
- `programs/libos32gfx/libos32gfx.h` — プロトタイプ追加

### Step 2: バイト境界高速パス

`dx` と `src->w` がバイト境界 (8の倍数) の場合、
バイト単位のマスク合成ループを使用。
タイル幅16px → 常にバイト境界のため、タイルマップ用途では常に高速パス。

### Step 3: NASM化 (将来)

`asm/asm_blit.asm` として独立したNASMファイルに実装。
`asm_gfx_draw_sprite_core` のマスク合成ロジックを流用可能。

---

## 5. 既存コードへの影響

| ファイル | 変更 |
|---------|------|
| `libos32gfx/draw/gfx_surface.c` | `gfx_blit_transparent()` 追加 |
| `libos32gfx/libos32gfx.h` | プロトタイプ追加 |
| `libos32gfx/asm/asm_blit.asm` | (Step 3) NASM版追加 |
| 既存の `gfx_blit_colorkey` | **変更なし** (汎用版として維持) |

---

*gfx_blit_colorkey Optimization — 2026-04-23*
