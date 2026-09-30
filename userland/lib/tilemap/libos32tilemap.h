#ifndef __LIBTILEMAP_H
#define __LIBTILEMAP_H

#include "os32api.h"
#include "libos32gfx.h"
#include "libos32asset.h"

/* 定数 */
#define TILE_W          16
#define TILE_H          16
#define TILE_PITCH      2       /* 16px / 8 = 2バイト/行/プレーン */
#define TILE_PLANE_SZ   32      /* 2 * 16 = 32バイト/プレーン */
#define TILE_TOTAL_SZ   128     /* 32 * 4 = 128バイト/タイル */
#define MAX_TILES       1024

/* BG プレーンの大きさ (タイル数)。
   既定は 26x20 = 416x320px。表示領域 400x304px (25x19 タイル) に
   スクロールの糊しろを 1 タイルずつ足したもの。

   ビルド時に -DTILEMAP_COLS=... で上書きできる。プレーンは BSS の静的
   配列なので、大きくするとメモリ使用量が (COLS*ROWS) に比例して増える。
   TILEMAP_COLS は偶数であること — 合成器がカバレッジマスクを 2 タイル
   ペア単位で持つため (tilemap_compose.c の COV_PAIRS)。 */
#ifndef TILEMAP_COLS
#define TILEMAP_COLS    26
#endif
#ifndef TILEMAP_ROWS
#define TILEMAP_ROWS    20
#endif
#define BG_COUNT        4

/* タイル不透明度 (事前計算) */
#define TILE_TRANSPARENT 0   /* 全ピクセルが色0 → 完全スキップ */
#define TILE_PARTIAL     1   /* 一部が色0 → colorkey付きblit */
#define TILE_OPAQUE      2   /* 色0なし → 無条件blit */

/* タイル属性ビット (16bit)
 * bit 15   : 水平反転
 * bit 14   : 垂直反転
 * bit 13-10: 予約
 * bit 9-0  : タイルID (0-1023)
 */
#define TILEMAP_ID(v)     ((v) & 0x3FF)
#define TILEMAP_HFLIP(v)  ((v) & 0x8000)
#define TILEMAP_VFLIP(v)  ((v) & 0x4000)

/* 反転フラグ付きタイル属性を構築するヘルパー */
#define TILEMAP_ATTR(id, hflip, vflip) \
    (((id) & 0x3FF) | ((hflip) ? 0x8000 : 0) | ((vflip) ? 0x4000 : 0))

/* タイル定義 */
typedef struct {
    u8 planes[4][TILE_PLANE_SZ];  /* プレーン別 (gfx_blit互換) */
    u8 opacity;                   /* 事前計算済み不透明度 */
} TileDef;

/* BGプレーン */
typedef struct {
    u16 map[TILEMAP_ROWS][TILEMAP_COLS]; /* タイル属性 (0-1023) */
    u8  dirty[TILEMAP_ROWS][TILEMAP_COLS]; /* ダーティフラグ */
    i16 scroll_x;     /* ピクセル単位スクロール */
    i16 scroll_y;     /* ピクセル単位スクロール */
    u8  visible;      /* 表示ON/OFF */
} BGPlane;

/* ===== 初期化・終了 ===== */
void tilemap_init(KernelAPI *api);
void tilemap_shutdown(void);

/* ===== タイル定義 ===== */
/* 4bppパックドデータからタイルを登録 (不透明度も自動計算) */
void tilemap_define(int id, const u8 *data_4bpp);
/* ファイルからタイルセット一括ロード (4bpp packed, 128bytes/tile) */
int  tilemap_load(const char *path, int start_id);
/* アセットハンドル経由でタイルセットロード (キャッシュ対応)
 * h: asset_load() で取得したハンドル (ASSET_STATE_READY であること)
 * start_id: 開始タイルID
 * 戻り値: ロードしたタイル数、失敗時 -1
 */
int  tilemap_load_asset(asset_handle_t h, int start_id);

/* ===== BGプレーン操作 ===== */
void tilemap_set(int bg, int col, int row, u16 tile_attr);
u16  tilemap_get(int bg, int col, int row);
void tilemap_fill(int bg, u16 tile_attr);
void tilemap_set_visible(int bg, int visible);
void tilemap_scroll(int bg, int sx, int sy);

/* ===== 合成 ===== */
void tilemap_compose_btf(void);       /* Back-to-Front (汎用) */
void tilemap_compose_btf_fast(void);  /* Back-to-Front (2タイルDWORD転送) */
void tilemap_compose_ftb(void);       /* Front-to-Back (カバレッジマスク) */
void tilemap_compose_scroll(void);    /* 差分スクロール (BBシフト+端列描画) */

/* ===== 表示 ===== */
void tilemap_force_redraw(void);  /* 全タイルを強制ダーティ化 */
/* dirty rectのみVRAM転送 (gfx_present_dirty呼び出し) */
void tilemap_present(void);

/* ===== 画面配置 ===== */
/* 描画オフセット (8の倍数に揃えられる、デフォルト0,0) */
void tilemap_set_origin(int ox, int oy);
/* 表示クリップ矩形 (画面座標)。w<=0 で無効化 (デフォルト = 全域)。
   スクロール中の糊しろタイルがビューの外へはみ出すのを防ぐ。
   compose_btf / compose_ftb に効く (compose_btf_fast は非対応)。 */
void tilemap_set_clip(int x, int y, int w, int h);

/* ===== HWスクロール ===== */
/* 全プレーン同方向スクロール (GDC SCROLLコマンド経由) */
void tilemap_scroll_hw(int lines);
void tilemap_scroll_sync(void);

/* ===== パレット ===== */
void tilemap_set_palette(const u8 pal[16][3]);

#endif /* __LIBTILEMAP_H */
