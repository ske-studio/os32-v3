# タイルマップエンジン設計仕様

## 1. 概要

SFC（スーパーファミコン）風の4枚BGプレーンによるタイルマップ合成エンジン。
OS32の既存GFXインフラ上に構築し、Back-to-Front / Front-to-Back の2方式をユーザー選択可能とする。

### スペック

| 項目 | 値 |
|------|-----|
| タイルサイズ | 16×16 ピクセル |
| グリッド | 24×24 タイル |
| 論理解像度 | 384×384 ピクセル |
| BGプレーン数 | 4 (BG0=最背面、BG3=最前面) |
| 最大タイル定義数 | 1024 |
| 色数 | 16色 (PC-98パレット共有) |
| 透明色 | パレット0 |

---

## 2. 既存GFXインフラの活用

### 利用するAPI/構造体

| コンポーネント | 用途 |
|---------------|------|
| `gfx_get_framebuffer()` | BBの4プレーンポインタ取得 (KAPI v28) |
| `GFX_Framebuffer.planes[4]` | BB直接書き込み |
| `gfx_blit()` | Surface→BB バイト境界高速転送 (`asm_copy_plane_rect`) |
| `gfx_blit_colorkey()` | Surface→BB 透過色指定転送 (※要高速化、別途 02参照) |
| `gfx_add_dirty_rect()` | 変更タイル領域の登録 (32pxアライメント自動) |
| `gfx_present_dirty()` | dirty rect限定VRAM転送 + ページフリッピング |
| `gfx_hardware_scroll()` | 全プレーン同方向のHWスクロール |

### 32pxアライメントとの整合

384 = 32 × 12。タイル幅16px = 2バイト = 16bit。
`gfx_add_dirty_rect` は内部で32px境界にアライメントするため (x & ~31)、
隣接2タイル(32px)が1つのdirty rectにまとまる。無駄な転送は発生しない。

画面配置は32px単位で自由に設定可能:

| 配置 | オフセットX | 余白 |
|------|-----------|------|
| 左寄せ | 0 | 右256px |
| センタリング | 128 | 左右各128px |
| 右寄せ+左UI | 256 | 左256px |

---

## 3. データ構造

### 3-1. タイル定義

PC-98のプレーン構造に直接対応した形式で保持する。

```c
#define TILE_W          16
#define TILE_H          16
#define TILE_PITCH      2       /* 16px / 8 = 2バイト/行/プレーン */
#define TILE_PLANE_SZ   32      /* 2 * 16 = 32バイト/プレーン */
#define TILE_TOTAL_SZ   128     /* 32 * 4 = 128バイト/タイル */
#define MAX_TILES       1024

/* タイル不透明度 (事前計算) */
enum tile_opacity {
    TILE_TRANSPARENT = 0,   /* 全ピクセルが色0 → 完全スキップ */
    TILE_PARTIAL     = 1,   /* 一部が色0 → colorkey付きblit */
    TILE_OPAQUE      = 2    /* 色0なし → 無条件blit */
};

typedef struct {
    u8 planes[4][TILE_PLANE_SZ];  /* プレーン別 (gfx_blit互換) */
    u8 opacity;                   /* 事前計算済み不透明度 */
} TileDef;
```

`TileDef` を `GFX_Surface` のように見せるため、blit呼び出し時にスタック上に
一時 `GFX_Surface` を構築する（プール枯渇を回避するハイブリッド方式）:

```c
static void tile_to_surface(GFX_Surface *s, const TileDef *t)
{
    s->w = TILE_W;
    s->h = TILE_H;
    s->pitch = TILE_PITCH;
    s->planes[0] = (u8 *)t->planes[0];
    s->planes[1] = (u8 *)t->planes[1];
    s->planes[2] = (u8 *)t->planes[2];
    s->planes[3] = (u8 *)t->planes[3];
    s->_pool_idx = -1;  /* プール外 */
}
```

### 3-2. メモリ見積もり

| 項目 | 計算 | サイズ |
|------|------|--------|
| タイル定義 × 1024 | (128+1) × 1024 | **約129KB** |
| BGタイルマップ × 4 | (24×24×2 + 24×24 + 4) × 4 | **約8KB** |
| カバレッジマスク (FtB用) | 384×384 / 8 | **18KB** |
| タイルカバレッジ (FtB用) | 24×24 | **576B** |
| **合計** | | **約156KB** |

> ※ Makefile の `--heap` は 256KB 以上を推奨。

### 3-3. BGプレーン

```c
#define TILEMAP_COLS  24
#define TILEMAP_ROWS  24
#define BG_COUNT      4

typedef struct {
    u16 map[TILEMAP_ROWS][TILEMAP_COLS]; /* タイルID (0-1023) */
    u8  dirty[TILEMAP_ROWS][TILEMAP_COLS]; /* ダーティフラグ */
    i16 scroll_x;     /* ピクセル単位スクロール */
    i16 scroll_y;
    u8  visible;      /* 表示ON/OFF */
} BGPlane;
```

### 3-4. タイルマップ属性ビット (将来拡張)

```c
/* map[r][c] のビットレイアウト:
 *   bit 15   : 水平反転
 *   bit 14   : 垂直反転
 *   bit 13-10: 予約
 *   bit 9-0  : タイルID (0-1023)
 */
#define TILEMAP_ID(v)     ((v) & 0x3FF)
#define TILEMAP_HFLIP(v)  ((v) & 0x8000)
#define TILEMAP_VFLIP(v)  ((v) & 0x4000)
```

---

## 4. 合成方式

ユーザーが `tilemap_compose()` 呼び出し時に方式を選択できるAPIを提供する。

### 4-1. Back-to-Front (方式B)

```c
void tilemap_compose_btf(void);  /* BG0 → BG1 → BG2 → BG3 の順で上書き */
```

**アルゴリズム:**

```
FOR bg = 0 to 3:
    IF NOT bg[n].visible THEN CONTINUE
    FOR each tile (row, col):
        tile_id = TILEMAP_ID(bg[n].map[row][col])
        tile = &tiles[tile_id]

        IF tile->opacity == TILE_TRANSPARENT → SKIP

        tile_to_surface(&tmp_surf, tile)

        IF bg == 0 OR tile->opacity == TILE_OPAQUE:
            gfx_blit(x, y, &tmp_surf, NULL)           /* 高速パス */
        ELSE:
            gfx_blit_colorkey(x, y, &tmp_surf, NULL, 0) /* 透過合成 */

        gfx_add_dirty_rect(x, y, 16, 16)
```

**特徴:**
- 既存APIのみで完結
- 実装がシンプル
- 下位レイヤーの不可視ピクセルも描画するため、重なりが多いと無駄

### 4-2. Front-to-Back (方式C)

```c
void tilemap_compose_ftb(void);  /* BG3 → BG2 → BG1 → BG0 の順、カバレッジマスク付き */
```

**アルゴリズム:**

```
clear coverage_mask      /* ビットマップ: 384×384 / 8 = 18KB */
clear tile_coverage      /* バイト配列: 24×24 */

FOR bg = 3 downto 0:
    IF NOT bg[n].visible THEN CONTINUE
    FOR each tile (row, col):
        IF tile_coverage[row][col] == FULL → SKIP (L1高速パス)

        tile_id = TILEMAP_ID(bg[n].map[row][col])
        tile = &tiles[tile_id]

        IF tile->opacity == TILE_TRANSPARENT → SKIP (L0高速パス)

        IF tile->opacity == TILE_OPAQUE AND tile_coverage[row][col] == 0:
            draw_tile_direct(x, y, tile)   /* BB直接書き込み、マスク不要 */
            tile_coverage[row][col] = FULL
        ELSE:
            draw_tile_masked(x, y, tile, coverage_mask)
            update_coverage(x, y, tile, coverage_mask)
            /* 全ビットが立ったか確認 */
            IF coverage_full(row, col) THEN tile_coverage[row][col] = FULL

        gfx_add_dirty_rect(x, y, 16, 16)
```

**特徴:**
- 重なりが多いほど高速（既描画ピクセルをスキップ）
- カバレッジマスク管理のオーバーヘッドあり
- BBへの直接書き込み（`gfx_get_framebuffer`経由）が必要

### 4-3. 32bit操作の活用

> BBはメインメモリ上にあるため、32bit (DWORD) 操作が使える。

タイル幅16px = 2バイト/プレーン/行 → **1タイル行は16bit**。
単独タイルでは DWORD 操作に適さないが、以下の方法で32bit化できる:

**案1: 2タイル同時処理**
隣接2タイル (32px = 4バイト/プレーン/行) をまとめて `rep movsd` で転送。

```c
/* 隣接2タイルの同時転送 (4プレーン × 1 DWORD/行 × 16行) */
static void draw_tile_pair(int px, int py,
                           const TileDef *left, const TileDef *right)
{
    int row, p;
    for (p = 0; p < 4; p++) {
        u8 *dst = fb.planes[p] + py * fb.pitch + (px >> 3);
        for (row = 0; row < TILE_H; row++) {
            u32 val = (u32)left->planes[p][row * 2] << 24
                    | (u32)left->planes[p][row * 2 + 1] << 16
                    | (u32)right->planes[p][row * 2] << 8
                    | (u32)right->planes[p][row * 2 + 1];
            *(u32 *)(dst + row * fb.pitch) = val;
        }
    }
}
```

**案2: タイルデータ自体を32bit幅に再構成**
タイル定義を2バイト→4バイト/行にパディングし、上位16bitをゼロ埋め。
メモリは増えるが、`movsd` 1命令で1プレーン1行を転送可能。

→ **推奨: 案1 (2タイル同時処理)**。タイルデータサイズを増やさず、
24列 = 12ペアで処理可能。端数なし。

---

## 5. API設計

```c
/* ===== 初期化・終了 ===== */
void tilemap_init(KernelAPI *api);
void tilemap_shutdown(void);

/* ===== タイル定義 ===== */
/* 4bppパックドデータからタイルを登録 (不透明度も自動計算) */
void tilemap_define(int id, const u8 *data_4bpp);
/* ファイルからタイルセット一括ロード */
int  tilemap_load(const char *path, int start_id);

/* ===== BGプレーン操作 ===== */
void tilemap_set(int bg, int col, int row, u16 tile_attr);
u16  tilemap_get(int bg, int col, int row);
void tilemap_fill(int bg, u16 tile_attr);
void tilemap_set_visible(int bg, int visible);
void tilemap_scroll(int bg, int sx, int sy);

/* ===== 合成 (ユーザー選択) ===== */
void tilemap_compose_btf(void);  /* Back-to-Front */
void tilemap_compose_ftb(void);  /* Front-to-Back */

/* ===== 表示 ===== */
/* dirty rectのみVRAM転送 (gfx_present_dirty呼び出し) */
void tilemap_present(void);

/* ===== 画面配置 ===== */
/* 描画オフセット (32px単位、デフォルト0,0) */
void tilemap_set_origin(int ox, int oy);

/* ===== パレット ===== */
void tilemap_set_palette(const u8 pal[16][3]);
```

---

## 6. スクロール

### タイル単位スクロール (コスト0)

`tilemap_scroll(bg, sx, sy)` で `sx`, `sy` をタイルサイズ(16)の倍数にすれば、
タイルマップの参照開始位置がシフトするだけ。描画タイル自体は変わらないため追加コスト0。

### ピクセル単位スムーズスクロール

端タイルのクリッピングが必要。`gfx_blit` の `src_rect` パラメータで
タイルの一部分だけを転送する方式で対応可能（既存API）。

### HWスクロール (全プレーン同方向)

全BGプレーンが同一方向・同一速度でスクロールする場合（RPGフィールド等）、
`gfx_hardware_scroll()` を併用してCPU負荷をゼロにできる。
GDC SCROLLコマンドでVRAMオフセットを変更するだけ。

---

## 7. ハイブリッドタイルデータ管理

`GFX_Surface` の静的プール (`SURF_POOL_MAX=16`) はタイル1024枚には不足。
`GFX_Sprite` プールも同様。

**ハイブリッド方式:**

1. タイルデータは `mem_alloc` で確保した生配列 (`TileDef tiles[MAX_TILES]`) に格納
2. 描画時にスタック上に一時 `GFX_Surface` を構築し `gfx_blit` / `gfx_blit_colorkey` に渡す
3. Front-to-Back 方式では `gfx_get_framebuffer` で取得したBBポインタに直接書き込み
4. いずれの方式でも `gfx_add_dirty_rect` でdirty登録は共通

```c
/* 描画呼び出し例 */
GFX_Surface tmp;
tile_to_surface(&tmp, &tiles[tile_id]);
gfx_blit(dx, dy, &tmp, NULL);
```

これにより:
- プール枯渇なし（プールを一切消費しない）
- `GFX_Surface` の互換レイアウトを維持（既存blit関数がそのまま使える）
- スタック使用量は `sizeof(GFX_Surface)` = 28バイト程度で無視可能

---

## 8. ダーティタイル追跡

毎フレーム全タイルを再描画する必要はない。

```c
/* タイルマップ変更時にダーティフラグをセット */
void tilemap_set(int bg, int col, int row, u16 tile_attr)
{
    if (bg_planes[bg].map[row][col] != tile_attr) {
        bg_planes[bg].map[row][col] = tile_attr;
        bg_planes[bg].dirty[row][col] = 1;
    }
}
```

`tilemap_compose_*()` ではダーティフラグが立っているタイル位置のみ、
全BGプレーンの該当位置を再合成する。静的な場面では描画コストがほぼゼロ。

スクロール時は移動方向の端1列/1行だけをダーティにする差分更新方式も検討。

---

## 9. 16色パレット共有

PC-98は16色パレットが画面全体で1つのみ。
BGプレーンごとに異なるパレットバンクを持つSFCとは異なり、ハードウェア的な制約。

**デザイン上の対応:**
- 16色を用途別に配分（例: 色0=透明、色1-4=BG0用、色5-8=BG1用、…）
- またはBGプレーン間で共通パレットを使用し、タイルデザインで差別化

ラスタパレット (`gfx_present_raster`) で走査線単位にパレットを変えることは可能だが、
BGプレーン単位の切替はできないため、本エンジンでは対象外とする。

---

## 10. 実装フェーズ

### Phase 1: 基本合成 ✅ 完了

- [x] `libtilemap` ディレクトリ作成、ヘッダ・ソースの雛形
- [x] `TileDef` タイル定義、不透明度自動計算
- [x] `BGPlane` タイルマップ管理、ダーティフラグ
- [x] `tilemap_compose_btf()` Back-to-Front合成 (gfx_blit + gfx_blit_transparent)
- [x] `tilemap_present()` dirty rect VRAM転送
- [x] Makefile統合 (heap 512KB)
- [x] `demo_tile.c` 基本デモ (4プレーン表示 + WASDキャラクター移動)

### Phase 2: Front-to-Back + 最適化 ✅ 完了

- [x] `tilemap_compose_ftb()` Front-to-Back合成 (カバレッジマスク)
- [x] BB直接書き込み (gfx_get_framebuffer経由、FtB内で使用)
- [x] 2タイル同時32bit転送 (`tilemap_compose_btf_fast()`)
- [x] ダーティタイル追跡による差分合成 (tilemap_compose_btf内で実装済み)

### Phase 3: スクロール + 拡張 ✅ 完了

- [x] ピクセル単位スムーズスクロール (`tilemap_scroll()` + `calc_tile_draw()`)
- [x] HWスクロール連携 (`tilemap_scroll_hw()` → `gfx_hardware_scroll`)
- [x] 水平/垂直反転属性 (`TILEMAP_ATTR()` マクロ + `tile_to_surface_flipped()`)
- [x] タイルセットファイルフォーマットとローダー (`tilemap_load()` — 4bpp packed 128bytes/tile)

---

## 11. 前提条件

`gfx_blit_transparent()` (NASM最適化済み `asm_blit_transparent_core`) により
透過合成の高速化は完了済み。詳細は [02_BLIT_COLORKEY_OPT.md](02_BLIT_COLORKEY_OPT.md) を参照。

---

*Tilemap Engine Design — 2026-04-23*
