/* ======================================================================== */
/*  GFX.H — SDL風グラフィックAPI (640x400x16, CPU直接描画)                 */
/*                                                                          */
/*  GRCG/EGC不使用。全描画はシステムRAMバックバッファ上で行い、              */
/*  gfx_present() でVRAMに一括転送する方式。                                */
/* ======================================================================== */

#ifndef __GFX_H
#define __GFX_H

#include "types.h"  /* u8, u16, u32 */

/* ======== 画面定数 ======== */
#define GFX_WIDTH    640
#define GFX_HEIGHT   400
#define GFX_BPL      80     /* bytes per line (640/8) */
#define GFX_WPL      40     /* words (16bit) per line */
#define GFX_PLANE_SZ 32000  /* 80 * 400 = 32000 bytes/plane */

/* 200ラインモード用定数 */
#define GFX_HEIGHT_200   200
#define GFX_PLANE_SZ_200 16000  /* 80 * 200 = 16000 bytes/plane */

/* ======== VRAMプレーンアドレス (PC9800Bible §2-7) ======== */
#define VRAM_PLANE_B  0xA8000UL   /* 青 / Plane 0 */
#define VRAM_PLANE_R  0xB0000UL   /* 赤 / Plane 1 */
#define VRAM_PLANE_G  0xB8000UL   /* 緑 / Plane 2 */
#define VRAM_PLANE_I  0xE0000UL   /* 輝度 / Plane 3 */

/* ======== パレットI/O (16色モード) ======== */
#define PAL_IDX_PORT  0xA8
#define PAL_G_PORT    0xAA
#define PAL_R_PORT    0xAC
#define PAL_B_PORT    0xAE

/* ======== データ構造 ======== */

#include "os32_kapi_shared.h"

/* 上記のヘッダで GFX_Rect, GFX_Color, GFX_Surface, GFX_Sprite は定義される */  

/* ======== 初期化・終了 ======== */
void gfx_init(void);       /* 640x400x16初期化 + バックバッファ確保 */
void gfx_init_200(void);   /* 640x200x16初期化 (縦はHWが2倍表示) */
/* KAPI スロット gfx_init / gfx_init_200 の実体 (票 T8 D1 / D1a)。
 * 画面の所有者を確かめてから本体を呼ぶ「門」で、sdk/kapi.json の
 * "target" がここを指す。GUI 中に宣言 (OS32X_FLAG_GFX) の無い CPL=3 が
 * 呼んだときは **何もしない** (戻り型は void のまま。拒否の数は
 * カーネルシンボル gfx_init_reject_count)。カーネル内部から画面を
 * 立て直すときは従来どおり gfx_init() を直接呼ぶ。 */
void gfx_kapi_init(void);
void gfx_kapi_init_200(void);
/* 画面の所有者 (KAPI v48)。1 = シェル帯 (WM) / 2〜5 = アプリ。誰でも呼べる。 */
i32  gfx_screen_owner(void);
void gfx_shutdown(void);   /* テキスト復帰 */
int  gfx_get_height(void); /* 現在のGFX高さ (200 or 400) を返す */

/* ======== ハードウェアスクロール ======== */
void gfx_hardware_scroll(int lines);

/* ======== スクリーンショット ======== */
int gfx_screenshot(const char *path);
int gfx_load_vdp(const char *path);

/* ======== 画面更新 (SDL_RenderPresent相当) ======== */
/* バックバッファ → VRAM 一括転送 (rep movsw) */
void gfx_present(void);
/* ビューポート矩形転送 (X範囲も限定してVRAM転送量を削減) */
void gfx_present_rect(int rx, int ry, int rw, int rh);
/* ラスタパレット付きVRAM転送 (HBLANK同期でパレット書き換え) */
void gfx_present_raster(GFX_RasterPalTable *table);
/* VSYNC待ちなしVRAM転送 (自前フレームレート制御用) */
void gfx_present_nosync(void);
/* GUI HAL 枠 (KAPI v40): 能力の問い合わせと、アクセラレータ向けのハードウェア塗り/転送。
 * CPU バックエンド (9801 プレーン / PEGC) では hw_* は OS32_ERR_NOSYS を返し、
 * 共有ライブラリが CPU 実装へフォールバックする。 */
void __cdecl gfx_screen_info(void *out);
int  __cdecl gfx_hw_fill_rect(int x, int y, int w, int h, u8 color);
int  __cdecl gfx_hw_blit(int dx, int dy, int sx, int sy, int w, int h);
/* GUI HAL カウンタとパレットリース (KAPI v41、契約 G7 / G8)。
 * gfx_stats は GFX_Stats を埋め、gfx_lease_palette は lease_mask の範囲内だけ
 * パレットを差し替える。K1 が kapi.json に載せる弱既定を強シンボルで上書きする。 */
int  __cdecl gfx_stats(void *out);
int  __cdecl gfx_lease_palette(int first, int count, const u8 *rgb);
void __cdecl gfx_set_palette_hal(int idx, u8 r, u8 g, u8 b);  /* KAPI gfx_set_palette の実体 (HAL 経由, レビュー ④) */

/* ======== 描画プリミティブ (バックバッファ対象) ======== */
void gfx_clear(u8 color);
/* 矩形クリア (ビューポート限定で高速) */
void gfx_clear_rect(int rx, int ry, int rw, int rh, u8 color);
void gfx_pixel(int x, int y, u8 color);
u8   gfx_get_pixel(int x, int y);
void gfx_hline(int x, int y, int w, u8 color);
void gfx_vline(int x, int y, int h, u8 color);
void gfx_line(int x0, int y0, int x1, int y1, u8 color);
void gfx_rect(int x, int y, int w, int h, u8 color);
void gfx_fill_rect(int x, int y, int w, int h, u8 color);

/* ======== サーフェス管理 (静的プール) ======== */
GFX_Surface *gfx_create_surface(int w, int h);
void         gfx_free_surface(GFX_Surface *surf);
void         gfx_surface_clear(GFX_Surface *surf, u8 color);
void         gfx_surface_pixel(GFX_Surface *surf, int x, int y, u8 color);

/* ======== ブリット ======== */
/* サーフェス → バックバッファに転送 (不透過) */
void gfx_blit(int dx, int dy,
              const GFX_Surface *src, const GFX_Rect *src_rect);

/* サーフェス → バックバッファに転送 (透過色指定) */
void gfx_blit_colorkey(int dx, int dy,
                       const GFX_Surface *src, const GFX_Rect *src_rect,
                       u8 colorkey);

/* ======== スプライト ======== */
GFX_Sprite *gfx_create_sprite(const GFX_Surface *src, u8 trans_color);
void        gfx_free_sprite(GFX_Sprite *spr);
void        gfx_draw_sprite(int x, int y, const GFX_Sprite *spr);
/* KCG用フォント描画ラスタライザ (アセンブラ最適化版) */
void        gfx_draw_font(int x, int y, const u8 *pat, int w_bytes, int h_lines, u8 fg);

/* ======== パレット ======== */
void gfx_set_palette(int idx, u8 r, u8 g, u8 b);
void gfx_set_palette_all(const GFX_Color *pal);
void gfx_set_default_palette(void);

/* ======== テストパターン ======== */
/* gfx_test_pattern: 外部プログラム化のため削除 */

#endif /* __GFX_H */
