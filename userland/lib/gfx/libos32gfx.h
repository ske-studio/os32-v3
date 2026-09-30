#ifndef __LIBOS32GFX_H
#define __LIBOS32GFX_H

#include "os32api.h"

extern KernelAPI *gfx_api;
extern GFX_Framebuffer gfx_fb;
extern int gfx_dirty_suppress;  /* 非0のとき gfx_blit 等の dirty rect 登録を抑制 */

/* 画素形式 (票 H2b)。0 = 4 プレーン 16 色 (PC-9801 標準)、
 * 非0 = パックド 8bpp (PEGC 256 色: planes[0] + y*pitch + x に 1 バイト/画素)。
 * libos32gfx_attach が gfx_get_framebuffer / gfx_screen_info を見て決める。
 * 色は 0〜15 をそのまま使う (システム色に同じ番号が割ってある)。 */
extern int gfx_packed;

void libos32gfx_init(KernelAPI *api);    /* gfx_init + attach (単独アプリ) */
void libos32gfx_attach(KernelAPI *api);  /* init 抜き: gshell 配下のアプリ / shlib */
void libos32gfx_shutdown(void);
void gfx_present(void);

/* Primitives */
void gfx_clear(u8 color);
void gfx_pixel(int x, int y, u8 color);
u8   gfx_get_pixel(int x, int y);
void gfx_hline(int x, int y, int w, u8 color);
void gfx_vline(int x, int y, int h, u8 color);
void gfx_line(int x0, int y0, int x1, int y1, u8 color);
void gfx_rect(int x, int y, int w, int h, u8 color);
void gfx_fill_rect(int x, int y, int w, int h, u8 color);
void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, u8 color);

/* Blitting & Cache */
void __cdecl gfx_save_rect(int x, int y, int w, int h, void *buf);
void __cdecl gfx_restore_rect(int x, int y, int w, int h, const void *buf);

/* Surface */
void gfx_surface_init(void);
GFX_Surface *gfx_create_surface(int w, int h);
void gfx_free_surface(GFX_Surface *surf);
void gfx_surface_clear(GFX_Surface *surf, u8 color);
void gfx_surface_pixel(GFX_Surface *surf, int x, int y, u8 color);
void gfx_surface_fill_rect(GFX_Surface *surf, int x, int y, int w, int h, u8 color);
void gfx_blit(int dx, int dy, const GFX_Surface *src, const GFX_Rect *src_rect);
void gfx_blit_colorkey(int dx, int dy, const GFX_Surface *src, const GFX_Rect *src_rect, u8 colorkey);
void gfx_blit_transparent(int dx, int dy, const GFX_Surface *src, const GFX_Rect *src_rect);

/* Rotation */
void gfx_blit_rotated(int dx, int dy,
                       const GFX_Surface *src,
                       int angle);
void gfx_blit_affine(int dx, int dy,
                      const GFX_Surface *src,
                      int angle,
                      int scale,
                      u8 colorkey);

/* Sprite */
void gfx_sprite_init(void);
GFX_Sprite *gfx_create_sprite(const GFX_Surface *src, u8 trans_color);
void gfx_free_sprite(GFX_Sprite *spr);
void gfx_draw_sprite(int x, int y, const GFX_Sprite *spr);
void gfx_sprite_save_bg(int x, int y, GFX_Sprite *spr);
void gfx_sprite_restore_bg(int x, int y, GFX_Sprite *spr);


/* Font & KCG */
void gfx_draw_font(int x, int y, const u8 *pat, int w_bytes, int h_lines, u8 fg);

void kcg_set_scale(int scale);
void kcg_draw_ank(int x, int y, u8 ch, u8 fg, u8 bg);
void kcg_draw_kanji(int x, int y, u16 jis_code, u8 fg, u8 bg);
int kcg_draw_utf8(int x, int y, const char *utf8_str, u8 fg, u8 bg);

/* Circle / Ellipse / Arc */
void gfx_circle(int cx, int cy, int r, u8 color);
/* dirty rect 登録なしのピクセル描画 (バッチ描画用内部関数) */
void gfx_pixel_nodirty(int x, int y, u8 color);
void gfx_fill_circle(int cx, int cy, int r, u8 color);
void gfx_ellipse(int cx, int cy, int rx, int ry, u8 color);
void gfx_fill_ellipse(int cx, int cy, int rx, int ry, u8 color);
void gfx_arc(int cx, int cy, int r, int start_deg, int end_deg, u8 color);
void gfx_circle_thick(int cx, int cy, int r, int thickness, u8 color);

/* 整数sin/cos LUT (512分割, 15bit固定小数点) */
i32 gfx_isin(int angle);          /* angle: 0〜511, 戻り値: -32767〜+32767 */
i32 gfx_icos(int angle);
int gfx_deg_to_idx(int deg);      /* 度数(0〜359) → LUTインデックス(0〜511) */
#define GFX_SIN_SCALE  32767      /* sin/cosの1.0に相当する値 */

/* ベジェ曲線 (de Casteljau, 整数演算のみ) */
void gfx_bezier2(int x0, int y0, int x1, int y1,
                 int x2, int y2, u8 color);
void gfx_bezier3(int x0, int y0, int x1, int y1,
                 int x2, int y2, int x3, int y3, u8 color);
void gfx_bezier3_thick(int x0, int y0, int x1, int y1,
                       int x2, int y2, int x3, int y3,
                       int thickness, u8 color);

/* ======================================================================== */
/*  スキャンラインフィル (even-odd rule)                                    */
/* ======================================================================== */

/* エッジ (直線セグメント, y0 <= y1 に正規化) */
typedef struct {
    int x0, y0, x1, y1;
} GFX_Edge;

/* エッジテーブル */
typedef struct {
    GFX_Edge *edges;
    int num_edges;
    int max_edges;
    int *intersect_buf;
    int max_intersect;
} GFX_EdgeTable;

/* エッジテーブル操作 */
GFX_EdgeTable *gfx_edge_table_create(int max_edges, int max_intersect);
void gfx_edge_table_free(GFX_EdgeTable *et);
void gfx_edges_clear(GFX_EdgeTable *et);
void gfx_edge_add(GFX_EdgeTable *et, int x0, int y0, int x1, int y1);

/* ベジェ曲線 → エッジテーブルへの平坦化 */
void gfx_bezier3_to_edges(GFX_EdgeTable *et,
                           int x0, int y0, int x1, int y1,
                           int x2, int y2, int x3, int y3, int depth);

/* スキャンラインフィル実行 */
void gfx_scanline_fill(GFX_EdgeTable *et, u8 color, int min_y, int max_y);

/* Raster Palette */
void gfx_raster_clear(GFX_RasterPalTable *table);
int  gfx_raster_add(GFX_RasterPalTable *table,
                     int line, int pal_idx, u8 r, u8 g, u8 b);
void gfx_present_raster_only(GFX_RasterPalTable *table);
void gfx_present_with_raster(GFX_RasterPalTable *table);

#endif
