/*
 * blit_test2.c — gfx_blit_transparent 映像テスト
 *
 * カラフルな背景の上で、透明色付きタイルが複数バウンスする。
 * 透過合成が正しく動作していれば、タイルの透明部分から
 * 背景のストライプが見えるはず。
 *
 * FPSカウンタ付き。任意キーで終了。
 */

#include "os32api.h"
#include "libos32gfx.h"
#include <string.h>

extern int sprintf(char *str, const char *format, ...);

static KernelAPI *kapi;

/* ---- ビューポート定義 ---- */
#define VP_X  80
#define VP_Y  24
#define VP_W  480
#define VP_H  336

/* ---- タイル数 ---- */
#define NUM_TILES 6

/* ---- タイルオブジェクト ---- */
typedef struct {
    GFX_Surface surf;
    int x, y;
    int vx, vy;
    int old_x, old_y;
    u8 *bg_buf;
} TileObj;

static TileObj tiles[NUM_TILES];

/* ---- タイルプレーンデータ (グローバル) ---- */
/* 16x16: pitch=2, total=2*16=32 bytes/plane, *4=128 */
static u8 tdata_16a[16 * 2 * 4];
static u8 tdata_16b[16 * 2 * 4];
static u8 tdata_16c[16 * 2 * 4];

/* 32x32: pitch=4, total=4*32=128 bytes/plane, *4=512 */
static u8 tdata_32a[32 * 4 * 4];
static u8 tdata_32b[32 * 4 * 4];
static u8 tdata_32c[32 * 4 * 4];

/* ---- 背景退避バッファ ---- */
/* save_rect: wb = ((x+w+7)/8) - (x/8), バイトアライン済なので pitch * h * 4 */
static u8 bgbuf_16a[2 * 16 * 4];
static u8 bgbuf_16b[2 * 16 * 4];
static u8 bgbuf_16c[2 * 16 * 4];
static u8 bgbuf_32a[4 * 32 * 4];
static u8 bgbuf_32b[4 * 32 * 4];
static u8 bgbuf_32c[4 * 32 * 4];

/* ---- サーフェス初期化 (全透明にクリア) ---- */
static void init_surface(GFX_Surface *s, u8 *buf, int size)
{
    int p;
    int pitch = size / 8;
    int total = pitch * size;

    s->w = size;
    s->h = size;
    s->pitch = pitch;
    s->_pool_idx = -1;
    for (p = 0; p < 4; p++) {
        s->planes[p] = buf + p * total;
        memset(s->planes[p], 0, total);
    }
}

/* ---- サーフェスにピクセルを描画 ---- */
static void surf_pixel(GFX_Surface *s, int x, int y, u8 color)
{
    int off, p;
    u8 bit;

    if (x < 0 || x >= s->w || y < 0 || y >= s->h) return;
    off = y * s->pitch + (x >> 3);
    bit = 0x80 >> (x & 7);
    for (p = 0; p < 4; p++) {
        if (color & (1 << p))
            s->planes[p][off] |= bit;
        else
            s->planes[p][off] &= ~bit;
    }
}

/* ==== タイルパターン生成 ==== */

/* 枠線のみ (内部は透明) */
static void pattern_frame(GFX_Surface *s, u8 color)
{
    int x, y;
    for (x = 0; x < s->w; x++) {
        surf_pixel(s, x, 0, color);
        surf_pixel(s, x, 1, color);
        surf_pixel(s, x, s->h - 1, color);
        surf_pixel(s, x, s->h - 2, color);
    }
    for (y = 2; y < s->h - 2; y++) {
        surf_pixel(s, 0, y, color);
        surf_pixel(s, 1, y, color);
        surf_pixel(s, s->w - 1, y, color);
        surf_pixel(s, s->w - 2, y, color);
    }
}

/* 十字 (中央に太い十字、他は透明) */
static void pattern_cross(GFX_Surface *s, u8 color)
{
    int i, cx, cy;
    cx = s->w / 2;
    cy = s->h / 2;
    for (i = 0; i < s->w; i++) {
        surf_pixel(s, i, cy - 1, color);
        surf_pixel(s, i, cy, color);
        surf_pixel(s, i, cy + 1, color);
    }
    for (i = 0; i < s->h; i++) {
        surf_pixel(s, cx - 1, i, color);
        surf_pixel(s, cx, i, color);
        surf_pixel(s, cx + 1, i, color);
    }
}

/* 市松模様 (4x4ブロック) */
static void pattern_checker(GFX_Surface *s, u8 color, int block)
{
    int x, y;
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            if (((x / block) + (y / block)) & 1)
                surf_pixel(s, x, y, color);
        }
    }
}

/* 斜めストライプ */
static void pattern_diagonal(GFX_Surface *s, u8 color)
{
    int x, y;
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            if ((x + y) % 6 < 3)
                surf_pixel(s, x, y, color);
        }
    }
}

/* 塗りつぶし丸 (近似) */
static void pattern_circle(GFX_Surface *s, u8 color)
{
    int x, y, cx, cy, r, dx, dy;
    cx = s->w / 2;
    cy = s->h / 2;
    r = (s->w < s->h ? s->w : s->h) / 2 - 1;
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            dx = x - cx;
            dy = y - cy;
            if (dx * dx + dy * dy <= r * r)
                surf_pixel(s, x, y, color);
        }
    }
}

/* ベタ塗りダイヤ */
static void pattern_diamond(GFX_Surface *s, u8 color)
{
    int x, y, cx, cy, r;
    cx = s->w / 2;
    cy = s->h / 2;
    r = (s->w < s->h ? s->w : s->h) / 2 - 1;
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy <= r)
                surf_pixel(s, x, y, color);
        }
    }
}

/* ---- 背景描画 (カラフルな横ストライプ + 矩形) ---- */
static void draw_background(void)
{
    int y;
    static const u8 stripe_colors[] = {1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14};
    int num_colors = sizeof(stripe_colors) / sizeof(stripe_colors[0]);

    /* ビューポート内をストライプで塗る */
    for (y = VP_Y; y < VP_Y + VP_H; y++) {
        int ci = ((y - VP_Y) / 8) % num_colors;
        gfx_hline(VP_X, y, VP_W, stripe_colors[ci]);
    }

    /* 中央に大きな白矩形 (透過がよく見える目印) */
    gfx_fill_rect(VP_X + VP_W / 2 - 40, VP_Y + VP_H / 2 - 40, 80, 80, 15);

    /* 角に小さな色付き矩形 */
    gfx_fill_rect(VP_X + 8, VP_Y + 8, 32, 32, 7);
    gfx_fill_rect(VP_X + VP_W - 40, VP_Y + 8, 32, 32, 10);
    gfx_fill_rect(VP_X + 8, VP_Y + VP_H - 40, 32, 32, 12);
    gfx_fill_rect(VP_X + VP_W - 40, VP_Y + VP_H - 40, 32, 32, 14);
}

/* ---- FPSバー描画 ---- */
static void draw_fps(int fps)
{
    int bw;
    u8 c;
    char buf[16];

    gfx_fill_rect(VP_X, VP_Y + VP_H + 4, VP_W, 12, 0);
    bw = fps * 5;
    if (bw > VP_W) bw = VP_W;
    if (bw > 0) {
        c = (fps >= 55) ? 10 : (fps >= 30) ? 14 : 12;
        gfx_fill_rect(VP_X, VP_Y + VP_H + 4, bw, 8, c);
    }

    /* 目盛り */
    gfx_vline(VP_X + 150, VP_Y + VP_H + 4, 8, 8);
    gfx_vline(VP_X + 300, VP_Y + VP_H + 4, 8, 7);

    /* FPS数値 */
    sprintf(buf, "%d fps", fps);
    kcg_draw_utf8(VP_X + VP_W + 4, VP_Y + VP_H + 4, buf, 15, 0);
    kapi->gfx_add_dirty_rect(VP_X, VP_Y + VP_H + 3, VP_W + 60, 14);
}

/* ---- メイン ---- */
void main(int argc, char **argv, KernelAPI *api)
{
    u32 last_tick, fps_tick;
    int fps, fps_frames;
    int i;

    kapi = api;
    api->kprintf(ATTR_WHITE, "blit_test2: visual transparency test (any key to exit)\r\n");

    libos32gfx_init(api);
    api->kcg_init();
    kcg_set_scale(1);

    /* ---- タイル生成 ---- */
    /* 0: 16x16 枠線 (白) */
    init_surface(&tiles[0].surf, tdata_16a, 16);
    pattern_frame(&tiles[0].surf, 15);
    tiles[0].bg_buf = bgbuf_16a;

    /* 1: 16x16 十字 (赤) */
    init_surface(&tiles[1].surf, tdata_16b, 16);
    pattern_cross(&tiles[1].surf, 4);
    tiles[1].bg_buf = bgbuf_16b;

    /* 2: 16x16 丸 (シアン) */
    init_surface(&tiles[2].surf, tdata_16c, 16);
    pattern_circle(&tiles[2].surf, 3);
    tiles[2].bg_buf = bgbuf_16c;

    /* 3: 32x32 市松模様 (緑) */
    init_surface(&tiles[3].surf, tdata_32a, 32);
    pattern_checker(&tiles[3].surf, 2, 4);
    tiles[3].bg_buf = bgbuf_32a;

    /* 4: 32x32 斜めストライプ (マゼンタ) */
    init_surface(&tiles[4].surf, tdata_32b, 32);
    pattern_diagonal(&tiles[4].surf, 5);
    tiles[4].bg_buf = bgbuf_32b;

    /* 5: 32x32 ダイヤモンド (黄) */
    init_surface(&tiles[5].surf, tdata_32c, 32);
    pattern_diamond(&tiles[5].surf, 14);
    tiles[5].bg_buf = bgbuf_32c;

    /* ---- 初期位置・速度 ---- */
    tiles[0].x = VP_X + 16;  tiles[0].y = VP_Y + 16;
    tiles[0].vx = 2;  tiles[0].vy = 1;

    tiles[1].x = VP_X + 80;  tiles[1].y = VP_Y + 80;
    tiles[1].vx = -1; tiles[1].vy = 2;

    tiles[2].x = VP_X + 200; tiles[2].y = VP_Y + 40;
    tiles[2].vx = 2;  tiles[2].vy = -2;

    tiles[3].x = VP_X + 120; tiles[3].y = VP_Y + 200;
    tiles[3].vx = -2; tiles[3].vy = 1;

    tiles[4].x = VP_X + 300; tiles[4].y = VP_Y + 100;
    tiles[4].vx = 1;  tiles[4].vy = -1;

    tiles[5].x = VP_X + 400; tiles[5].y = VP_Y + 250;
    tiles[5].vx = -1; tiles[5].vy = -2;

    for (i = 0; i < NUM_TILES; i++) {
        /* 8ピクセル境界にアライン (バイト境界高速パスに入るため) */
        tiles[i].x &= ~7;
        tiles[i].old_x = tiles[i].x;
        tiles[i].old_y = tiles[i].y;
    }

    /* ---- 初期描画 ---- */
    gfx_clear(0);

    /* ビューポート枠線 */
    gfx_rect(VP_X - 1, VP_Y - 1, VP_W + 2, VP_H + 2, 7);

    /* 背景 */
    draw_background();

    /* タイトル */
    kcg_draw_utf8(VP_X, VP_Y - 16, "gfx_blit_transparent Visual Test", 15, 0);

    /* 初回の背景退避 + 描画 */
    for (i = 0; i < NUM_TILES; i++) {
        gfx_save_rect(tiles[i].x, tiles[i].y,
                       tiles[i].surf.w, tiles[i].surf.h, tiles[i].bg_buf);
    }
    for (i = 0; i < NUM_TILES; i++) {
        gfx_blit_transparent(tiles[i].x, tiles[i].y, &tiles[i].surf, NULL);
    }

    gfx_present();

    last_tick = api->get_tick();
    fps_tick = last_tick;
    fps = 0;
    fps_frames = 0;

    /* ---- メインループ ---- */
    while (1) {
        if (api->kbd_trygetchar() != -1) break;

        /* Phase 1: 旧位置の背景を書き戻す (逆順) */
        for (i = NUM_TILES - 1; i >= 0; i--) {
            gfx_restore_rect(tiles[i].old_x, tiles[i].old_y,
                              tiles[i].surf.w, tiles[i].surf.h, tiles[i].bg_buf);
        }

        /* Phase 2: 座標更新 + バウンス */
        for (i = 0; i < NUM_TILES; i++) {
            int w = tiles[i].surf.w;
            int h = tiles[i].surf.h;

            tiles[i].x += tiles[i].vx * 8;  /* 8px単位で移動(バイト境界) */
            tiles[i].y += tiles[i].vy;

            if (tiles[i].x <= VP_X) {
                tiles[i].x = VP_X;
                tiles[i].vx = -tiles[i].vx;
            } else if (tiles[i].x + w >= VP_X + VP_W) {
                tiles[i].x = ((VP_X + VP_W - w) & ~7);
                tiles[i].vx = -tiles[i].vx;
            }

            if (tiles[i].y <= VP_Y) {
                tiles[i].y = VP_Y;
                tiles[i].vy = -tiles[i].vy;
            } else if (tiles[i].y + h >= VP_Y + VP_H) {
                tiles[i].y = VP_Y + VP_H - h;
                tiles[i].vy = -tiles[i].vy;
            }

            /* バイト境界にアライン */
            tiles[i].x &= ~7;
        }

        /* Phase 3: 新位置の背景を退避 */
        for (i = 0; i < NUM_TILES; i++) {
            gfx_save_rect(tiles[i].x, tiles[i].y,
                           tiles[i].surf.w, tiles[i].surf.h, tiles[i].bg_buf);
        }

        /* Phase 4: 描画 (gfx_blit_transparent) */
        for (i = 0; i < NUM_TILES; i++) {
            gfx_blit_transparent(tiles[i].x, tiles[i].y, &tiles[i].surf, NULL);
        }

        /* Phase 5: ダーティレクト登録 + VRAM転送 */
        for (i = 0; i < NUM_TILES; i++) {
            int w = tiles[i].surf.w;
            int h = tiles[i].surf.h;
            kapi->gfx_add_dirty_rect(tiles[i].old_x, tiles[i].old_y, w, h);
            kapi->gfx_add_dirty_rect(tiles[i].x, tiles[i].y, w, h);
            tiles[i].old_x = tiles[i].x;
            tiles[i].old_y = tiles[i].y;
        }

        fps_frames++;

        /* FPS計測 (1秒ごと) */
        if (api->get_tick() - fps_tick >= 100) {
            fps = fps_frames;
            fps_frames = 0;
            fps_tick = api->get_tick();
            draw_fps(fps);
        }

        api->gfx_present_dirty();

        /* 100Hz同期 */
        while (api->get_tick() == last_tick) {
            api->sys_halt();
        }
        last_tick = api->get_tick();
    }

    gfx_clear(0);
    gfx_present();
    libos32gfx_shutdown();
    api->kprintf(ATTR_WHITE, "blit_test2 exited.\r\n");
}
