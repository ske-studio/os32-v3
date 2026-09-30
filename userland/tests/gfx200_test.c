/* ======================================================================== */
/*  GFX200_TEST.C — 640x200 ラインモード動作検証テストプログラム             */
/*                                                                          */
/*  GFX 200ラインモード (gfx_init_200) の基本動作を検証する。               */
/*  矩形描画・ライン描画・テキスト描画を行い、VRAM転送をテストする。         */
/* ======================================================================== */

#include "os32api.h"
#include "libos32gfx.h"

static KernelAPI *api;

/* Pyxel 16色パレット */
static const u8 pal16[16][3] = {
    { 0,  0,  0}, { 2,  3,  3}, { 7,  2,  3}, { 0,  7,  3},
    { 5,  4,  2}, { 5,  5,  2}, {12, 12,  9}, {15, 15, 15},
    {15,  0,  0}, {15,  7,  0}, {15, 15,  0}, { 0, 15,  0},
    { 0, 15, 15}, { 5,  5, 15}, {15,  0, 15}, {15,  8,  8}
};

/* ======================================================================== */
/*  カラーバーテスト: 16色のカラーバーを描画                                 */
/* ======================================================================== */
static void test_color_bars(void)
{
    int i;
    int bar_w = 640 / 16;  /* 各バー 40px */

    for (i = 0; i < 16; i++) {
        gfx_fill_rect(i * bar_w, 0, bar_w, 50, (u8)i);
    }
}

/* ======================================================================== */
/*  矩形・ライン描画テスト                                                  */
/* ======================================================================== */
static void test_primitives(void)
{
    int i;

    /* 外枠 */
    gfx_rect(0, 0, 640, 200, 7);

    /* 矩形 */
    gfx_fill_rect(10, 60, 100, 60, 8);   /* 赤 */
    gfx_fill_rect(120, 60, 100, 60, 11);  /* 緑 */
    gfx_fill_rect(230, 60, 100, 60, 12);  /* 水色 */
    gfx_fill_rect(340, 60, 100, 60, 10);  /* 黄 */
    gfx_fill_rect(450, 60, 100, 60, 13);  /* 青 */
    gfx_fill_rect(560, 60, 70, 60, 14);   /* 紫 */

    /* 斜めライン */
    for (i = 0; i < 10; i++) {
        gfx_line(0, 130 + i * 7, 639, 130 + i * 3, (u8)(1 + (i % 15)));
    }
}

/* ======================================================================== */
/*  テキスト描画テスト                                                      */
/* ======================================================================== */
static void test_text(void)
{
    kcg_set_scale(1);
    kcg_draw_utf8(10, 180, "640x200 LINE MODE TEST", 7, 0);
    kcg_draw_utf8(400, 180, "Press any key...", 6, 0);
}

/* ======================================================================== */
/*  FPSベンチマーク: 全画面dirty rect転送を繰り返す                         */
/* ======================================================================== */
static void test_fps_benchmark(void)
{
    u32 start_tick, elapsed;
    int frames, fps;
    int x, dx;
    int running;

    start_tick = api->get_tick();
    frames = 0;
    fps = 0;
    x = 0;
    dx = 4;
    running = 1;

    /* 背景をクリア */
    gfx_clear(0);

    while (running) {
        int key;

        /* 背景 */
        gfx_fill_rect(0, 0, 640, 200, 1);

        /* 移動するバー */
        gfx_fill_rect(x, 30, 80, 140, 10);

        x += dx;
        if (x + 80 >= 640 || x <= 0) dx = -dx;

        /* FPS表示 */
        {
            char buf[32];


            buf[0] = 'F'; buf[1] = 'P'; buf[2] = 'S';
            buf[3] = ':'; buf[4] = ' ';
            /* 簡易数値変換 */
            if (fps >= 100) {
                buf[5] = '0' + (fps / 100);
                buf[6] = '0' + ((fps / 10) % 10);
                buf[7] = '0' + (fps % 10);
                buf[8] = '\0';
            } else if (fps >= 10) {
                buf[5] = '0' + (fps / 10);
                buf[6] = '0' + (fps % 10);
                buf[7] = '\0';
            } else {
                buf[5] = '0' + fps;
                buf[6] = '\0';
            }
            kcg_set_scale(2);
            kcg_draw_utf8(10, 5, buf, 7, 1);
        }

        /* VRAM転送 (dirty rect全体) */
        gfx_api->gfx_add_dirty_rect(0, 0, 640, 200);
        gfx_api->gfx_present_nosync();

        frames++;

        /* FPS計測 (1秒 = 100 ticks) */
        elapsed = api->get_tick() - start_tick;
        if (elapsed >= 100) {
            fps = frames;
            frames = 0;
            start_tick = api->get_tick();
        }

        /* キー入力チェック */
        key = api->kbd_trygetchar();
        if (key > 0) running = 0;
    }
}

/* ======================================================================== */
/*  メイン                                                                  */
/* ======================================================================== */
void __cdecl main(int argc, char **argv, KernelAPI *kapi)
{
    int i;

    api = kapi;
    (void)argc; (void)argv;

    /* === フェーズ1: 200ラインモード基本表示テスト === */
    api->kprintf(ATTR_WHITE, "=== GFX 200-line mode test ===\r\n");
    api->kprintf(ATTR_WHITE, "Initializing 640x200 mode...\r\n");

    /* 200ラインモードで初期化 */
    libos32gfx_init(api);  /* まず通常で初期化 (libos32gfx内部を準備) */

    /* 200ラインモードに切り替え */
    api->gfx_init_200();
    api->gfx_get_framebuffer(&gfx_fb);  /* FB更新 */

    api->kprintf(ATTR_GREEN,
        "Mode: %dx%d, pitch=%d\r\n",
        gfx_fb.width, gfx_fb.height, gfx_fb.pitch);

    /* パレット設定 */
    for (i = 0; i < 16; i++) {
        api->gfx_set_palette(i, pal16[i][0], pal16[i][1], pal16[i][2]);
    }

    /* テスト描画 */
    gfx_clear(0);
    test_color_bars();
    test_primitives();
    test_text();

    /* VRAM転送 */
    api->gfx_add_dirty_rect(0, 0, 640, 200);
    api->gfx_present_dirty();

    /* キー待ち */
    api->kbd_getchar();

    /* === フェーズ2: FPSベンチマーク === */
    test_fps_benchmark();

    /* 終了: 400ラインモードに復帰 */
    api->gfx_shutdown();
    api->gfx_init();
    api->gfx_shutdown();
    api->tvram_clear();

    api->kprintf(ATTR_GREEN, "GFX 200-line mode test completed.\r\n");
}
