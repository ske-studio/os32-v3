/* ======================================================================== */
/*  ROTATE_TEST.C — gfx_blit_rotated 視覚テスト                             */
/*                                                                          */
/*  32x32 の矢印スプライトを画面中央で回転させ、                            */
/*  8.8固定小数点精度の回転品質・クロッピング・FPS を検証する。              */
/*  任意キーで終了。                                                        */
/* ======================================================================== */

#include "os32api.h"
#include "libos32gfx.h"
#include <string.h>

extern int sprintf(char *str, const char *format, ...);

static KernelAPI *kapi;

/* ビューポート定義 */
#define VP_X  80
#define VP_Y  24
#define VP_W  480
#define VP_H  336

/* スプライトサイズ */
#define SPR_SZ  32
#define SPR_PITCH  (SPR_SZ / 8)   /* 4 bytes */
#define SPR_PLANE  (SPR_PITCH * SPR_SZ)  /* 128 bytes */

/* サーフェスデータ (32x32: pitch=4, total=128 bytes/plane, *4=512) */
static GFX_Surface surf;
static u8 tdata[SPR_PLANE * 4];

/*
 * 背景退避バッファ
 * gfx_save_rect のバッファサイズ = orig_wb * h * 4
 * cx - 16 = 304 → orig_wb = ((304+32+7)/8) - (304/8) = 42-38 = 4
 * 4 * 32 * 4 = 512 バイト
 */
static u8 bgbuf[SPR_PITCH * SPR_SZ * 4];

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

static void pattern_arrow(GFX_Surface *s)
{
    int x, y, w;
    int mid = s->w / 2;
    int half = s->h / 2;

    /* 軸部分 (白=15): 中央に5px幅の縦棒 */
    for (y = half - 2; y < s->h - 2; y++) {
        for (x = mid - 2; x <= mid + 2; x++) {
            surf_pixel(s, x, y, 15);
        }
    }

    /* 三角形の先端 (赤=2): 上にいくほど狭くなる */
    for (y = 2; y <= half; y++) {
        w = y;
        for (x = mid - w; x <= mid + w; x++) {
            if (x >= 0 && x < s->w) {
                surf_pixel(s, x, y, 2);
            }
        }
    }
}

/* カラフルなストライプ背景 */
static void draw_background(void)
{
    int y, ci;
    static const u8 stripe_colors[] = {
        1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14
    };
    int num_colors = sizeof(stripe_colors) / sizeof(stripe_colors[0]);

    for (y = VP_Y; y < VP_Y + VP_H; y++) {
        ci = ((y - VP_Y) / 8) % num_colors;
        gfx_hline(VP_X, y, VP_W, stripe_colors[ci]);
    }

    /* 中央に大きなグレー矩形 (透過がよく見える目印) */
    gfx_fill_rect(VP_X + VP_W / 2 - 40, VP_Y + VP_H / 2 - 40, 80, 80, 8);
}

/* FPSバー表示 */
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

    sprintf(buf, "%d fps", fps);
    kcg_draw_utf8(VP_X + VP_W + 4, VP_Y + VP_H + 4, buf, 15, 0);
    kapi->gfx_add_dirty_rect(VP_X, VP_Y + VP_H + 3, VP_W + 60, 14);
}

/* ======================================================================== */
/*  メイン                                                                  */
/* ======================================================================== */

void main(int argc, char **argv, KernelAPI *api)
{
    u32 last_tick, fps_tick;
    int fps, fps_frames;
    int angle;
    int spr_x, spr_y;

    kapi = api;
    api->kprintf(ATTR_WHITE,
        "rotate_test: 8.8fp rotation test (any key to exit)\r\n");

    libos32gfx_init(api);
    api->kcg_init();
    kcg_set_scale(1);

    /* スプライト生成 */
    init_surface(&surf, tdata, SPR_SZ);
    pattern_arrow(&surf);

    /* スプライト描画中心 (ビューポート中央) */
    spr_x = VP_X + VP_W / 2;
    spr_y = VP_Y + VP_H / 2;

    /* 画面初期化 */
    gfx_clear(0);
    gfx_rect(VP_X - 1, VP_Y - 1, VP_W + 2, VP_H + 2, 7);
    draw_background();
    kcg_draw_utf8(VP_X, VP_Y - 16,
                  "gfx_blit_rotated 8.8fp Test", 15, 0);

    /* 初回描画 */
    angle = 0;
    gfx_save_rect(spr_x - SPR_SZ / 2, spr_y - SPR_SZ / 2,
                   SPR_SZ, SPR_SZ, bgbuf);
    gfx_blit_rotated(spr_x, spr_y, &surf, angle);
    gfx_present();

    last_tick = api->get_tick();
    fps_tick = last_tick;
    fps = 0;
    fps_frames = 0;

    /* リモート実行時の残留キー入力(改行など)をクリア */
    while (api->kbd_trygetchar() != -1) { /* flush */ }

    /* メインループ */
    while (1) {
        if (api->kbd_trygetchar() != -1) break;

        /* 背景復元 */
        gfx_restore_rect(spr_x - SPR_SZ / 2, spr_y - SPR_SZ / 2,
                          SPR_SZ, SPR_SZ, bgbuf);

        /* 角度更新 */
        angle = (angle + 1) & 511;

        /* 背景退避 (座標固定なので毎フレーム同じだが、パターン統一のため) */
        gfx_save_rect(spr_x - SPR_SZ / 2, spr_y - SPR_SZ / 2,
                       SPR_SZ, SPR_SZ, bgbuf);

        /* 回転描画 */
        gfx_blit_rotated(spr_x, spr_y, &surf, angle);

        /* dirty rect (gfx_blit_rotated 内部でも登録されるが明示的に) */
        kapi->gfx_add_dirty_rect(spr_x - SPR_SZ / 2, spr_y - SPR_SZ / 2,
                                  SPR_SZ, SPR_SZ);

        fps_frames++;

        /* FPS 計測 (1秒ごと) */
        if (api->get_tick() - fps_tick >= 100) {
            fps = fps_frames;
            fps_frames = 0;
            fps_tick = api->get_tick();
            draw_fps(fps);
        }

        api->gfx_present_dirty();

        /* 100Hz 同期 */
        while (api->get_tick() == last_tick) {
            api->sys_halt();
        }
        last_tick = api->get_tick();
    }

    gfx_clear(0);
    gfx_present();
    libos32gfx_shutdown();
    api->kprintf(ATTR_WHITE, "rotate_test exited.\r\n");
}
