/*
 * tile_bench.c — タイルマップエンジン パフォーマンスベンチマーク
 *
 * テスト項目:
 *   1. 初期全面描画 (576タイル): btf / btf_fast / ftb の3方式を比較
 *   2. 差分更新 (1タイル移動): 同上3方式
 *   3. 全面更新 (全タイルdirty): 同上3方式
 *   4. H/Vフリップ付き描画: btf のフリップあり/なし比較
 *   5. スクロール (全タイルdirty化): btf でのスクロール更新コスト
 *
 * 結果は stdout (printf) に表形式で出力する。
 * get_tick() の 1 tick = 10ms。
 */

#include "os32api.h"
#include "libos32tilemap.h"
#include <string.h>
#include <stdio.h>

static KernelAPI *kapi;

/* ====================================================================== */
/*  タイルデータ (4bpp packed, 128 bytes/tile)                             */
/* ====================================================================== */

/* ID 1: 不透明タイル (草) — 色2 ベースに色10 アクセント */
static const u8 tile_opaque[128] = {
    0x22, 0x22, 0xA2, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x2A, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x2A, 0x22, 0x22, 0x22,
    0x2A, 0x22, 0x22, 0x22, 0x22, 0x22, 0x2A, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x2A, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x2A, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x2A, 0x22, 0x22, 0x22, 0x2A, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x2A, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x2A, 0x22, 0x22,
    0x22, 0x2A, 0x22, 0x22, 0x22, 0x22, 0x22, 0x2A,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x22, 0x2A, 0x22, 0x22, 0x22,
};

/* ID 2: 部分透明タイル (木) — 色0=透明 */
static const u8 tile_partial[128] = {
    0x00, 0x00, 0x04, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x44, 0x44, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x44, 0x44, 0x44, 0x44, 0x00, 0x00, 0x00,
    0x04, 0x44, 0x24, 0x44, 0x44, 0x40, 0x00, 0x00,
    0x44, 0x42, 0x22, 0x44, 0x44, 0x44, 0x00, 0x00,
    0x44, 0x22, 0x22, 0x24, 0x44, 0x44, 0x00, 0x00,
    0x04, 0x42, 0x22, 0x24, 0x44, 0x40, 0x00, 0x00,
    0x00, 0x44, 0x44, 0x44, 0x44, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x44, 0x44, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ID 3: 全透明タイル */
static const u8 tile_transparent[128] = {0};

/* ====================================================================== */
/*  計測ヘルパー                                                          */
/* ====================================================================== */



/* 計測結果1行を表示 */
static void print_result(const char *label, u32 ticks)
{
    u32 ms = ticks * 10;
    printf("  %-28s %4lu ticks (%lu ms)\n", label, ticks, ms);
}

/* ====================================================================== */
/*  シナリオ構築                                                          */
/* ====================================================================== */

/* シナリオ A: BG0のみ不透明 (最軽量) */
static void setup_scenario_a(void)
{
    tilemap_fill(0, 1);         /* 全面 草 */
    tilemap_set_visible(0, 1);
    tilemap_set_visible(1, 0);
    tilemap_set_visible(2, 0);
    tilemap_set_visible(3, 0);
}

/* シナリオ B: BG0不透明 + BG1に透過タイル散布 (2層) */
static void setup_scenario_b(void)
{
    int row, col;
    tilemap_fill(0, 1);         /* BG0: 草 */
    tilemap_fill(1, 0);         /* BG1: 透明ベース */
    for (row = 1; row < TILEMAP_ROWS; row += 3) {
        for (col = 1; col < TILEMAP_COLS; col += 3) {
            tilemap_set(1, col, row, 2); /* 木を散布 */
        }
    }
    tilemap_set_visible(0, 1);
    tilemap_set_visible(1, 1);
    tilemap_set_visible(2, 0);
    tilemap_set_visible(3, 0);
}

/* シナリオ C: 4層全部使用 (最重量) */
static void setup_scenario_c(void)
{
    int row, col;
    tilemap_fill(0, 1);         /* BG0: 草 */
    tilemap_fill(1, 0);
    tilemap_fill(2, 0);
    tilemap_fill(3, 0);
    for (row = 0; row < TILEMAP_ROWS; row += 2) {
        for (col = 0; col < TILEMAP_COLS; col += 2) {
            tilemap_set(1, col, row, 2);
        }
    }
    for (row = 1; row < TILEMAP_ROWS; row += 3) {
        for (col = 0; col < TILEMAP_COLS; col += 4) {
            tilemap_set(2, col, row, 2);
        }
    }
    for (row = 0; row < TILEMAP_ROWS; row += 5) {
        for (col = 2; col < TILEMAP_COLS; col += 5) {
            tilemap_set(3, col, row, 2);
        }
    }
    tilemap_set_visible(0, 1);
    tilemap_set_visible(1, 1);
    tilemap_set_visible(2, 1);
    tilemap_set_visible(3, 1);
}

/* ====================================================================== */
/*  ベンチマーク本体                                                      */
/* ====================================================================== */

/* 全面描画ベンチマーク (compose + present) */
static void bench_full_draw(const char *scenario_name,
                            void (*setup_fn)(void),
                            int iterations)
{
    u32 t_btf, t_fast, t_ftb;
    u32 start, end;
    int i;

    printf("\n[%s] Full Draw x%d\n", scenario_name, iterations);

    /* --- btf --- */
    setup_fn();
    tilemap_force_redraw();
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        if (i > 0) tilemap_force_redraw();
        tilemap_compose_btf();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_btf = end - start;
    print_result("compose_btf", t_btf);

    /* --- btf_fast --- */
    setup_fn();
    tilemap_force_redraw();
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        if (i > 0) tilemap_force_redraw();
        tilemap_compose_btf_fast();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_fast = end - start;
    print_result("compose_btf_fast", t_fast);

    /* --- ftb --- */
    setup_fn();
    tilemap_force_redraw();
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        if (i > 0) tilemap_force_redraw();
        tilemap_compose_ftb();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_ftb = end - start;
    print_result("compose_ftb", t_ftb);
}

/* 差分更新ベンチマーク (1タイル移動) */
static void bench_delta_update(const char *scenario_name,
                               void (*setup_fn)(void),
                               int iterations)
{
    u32 t_btf, t_fast, t_ftb;
    u32 start, end;
    int i, x;

    printf("\n[%s] Delta (1-tile move) x%d\n", scenario_name, iterations);

    /* --- btf --- */
    setup_fn();
    tilemap_set_visible(2, 1);
    tilemap_fill(2, 0);
    tilemap_force_redraw();
    tilemap_compose_btf();
    tilemap_present();

    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        x = i % (TILEMAP_COLS - 1);
        tilemap_set(2, x, 10, 0);
        tilemap_set(2, x + 1, 10, 2);
        tilemap_compose_btf();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_btf = end - start;
    print_result("compose_btf", t_btf);

    /* --- btf_fast --- */
    setup_fn();
    tilemap_set_visible(2, 1);
    tilemap_fill(2, 0);
    tilemap_force_redraw();
    tilemap_compose_btf_fast();
    tilemap_present();

    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        x = i % (TILEMAP_COLS - 1);
        tilemap_set(2, x, 10, 0);
        tilemap_set(2, x + 1, 10, 2);
        tilemap_compose_btf_fast();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_fast = end - start;
    print_result("compose_btf_fast", t_fast);

    /* --- ftb --- */
    setup_fn();
    tilemap_set_visible(2, 1);
    tilemap_fill(2, 0);
    tilemap_force_redraw();
    tilemap_compose_ftb();
    tilemap_present();

    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        x = i % (TILEMAP_COLS - 1);
        tilemap_set(2, x, 10, 0);
        tilemap_set(2, x + 1, 10, 2);
        tilemap_compose_ftb();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_ftb = end - start;
    print_result("compose_ftb", t_ftb);
}

/* H/Vフリップ性能ベンチマーク */
static void bench_flip(int iterations)
{
    u32 t_noflip, t_hflip, t_vflip, t_hvflip;
    u32 start, end;
    int i;

    printf("\n[Flip] Full Draw x%d\n", iterations);

    /* フリップなし */
    tilemap_fill(0, 1);
    tilemap_set_visible(0, 1);
    tilemap_set_visible(1, 0);
    tilemap_set_visible(2, 0);
    tilemap_set_visible(3, 0);
    tilemap_force_redraw();
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        if (i > 0) tilemap_force_redraw();
        tilemap_compose_btf();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_noflip = end - start;
    print_result("No flip", t_noflip);

    /* 全タイル H-flip */
    tilemap_fill(0, TILEMAP_ATTR(1, 1, 0));
    tilemap_force_redraw();
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        if (i > 0) tilemap_force_redraw();
        tilemap_compose_btf();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_hflip = end - start;
    print_result("H-flip (all tiles)", t_hflip);

    /* 全タイル V-flip */
    tilemap_fill(0, TILEMAP_ATTR(1, 0, 1));
    tilemap_force_redraw();
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        if (i > 0) tilemap_force_redraw();
        tilemap_compose_btf();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_vflip = end - start;
    print_result("V-flip (all tiles)", t_vflip);

    /* 全タイル HV-flip */
    tilemap_fill(0, TILEMAP_ATTR(1, 1, 1));
    tilemap_force_redraw();
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        if (i > 0) tilemap_force_redraw();
        tilemap_compose_btf();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_hvflip = end - start;
    print_result("HV-flip (all tiles)", t_hvflip);
}

/* スクロール性能ベンチマーク */
static void bench_scroll(int iterations)
{
    u32 start, end, t_btf, t_scroll;
    int i;

    printf("\n[Scroll] x%d\n", iterations);

    /* --- 従来 btf (全面再描画) --- */
    setup_scenario_b();
    tilemap_force_redraw();
    tilemap_compose_btf();
    tilemap_present();

    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        /* 16px単位 (1タイル幅) でスクロール — 8px境界保証 */
        tilemap_scroll(0, (i * 16) % (TILEMAP_COLS * TILE_W), 0);
        /* scroll() がもう全dirty化しないので手動で全dirty化 */
        tilemap_force_redraw();
        tilemap_compose_btf();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_btf = end - start;
    print_result("btf (full redraw)", t_btf);
    if (iterations > 0) {
        printf("  avg: %lu ms/frame\n",
                      (t_btf * 10) / (u32)iterations);
    }

    /* --- 差分スクロール --- */
    setup_scenario_b();
    tilemap_force_redraw();
    tilemap_compose_btf();
    tilemap_present();

    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        /* 16px単位 (1タイル幅) でスクロール — 8px境界保証 */
        tilemap_scroll(0, (i * 16) % (TILEMAP_COLS * TILE_W), 0);
        tilemap_compose_scroll();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_scroll = end - start;
    print_result("compose_scroll H-diff", t_scroll);
    if (iterations > 0) {
        printf("  avg: %lu ms/frame\n",
                      (t_scroll * 10) / (u32)iterations);
    }

    /* --- 垂直差分スクロール --- */
    setup_scenario_b();
    tilemap_scroll(0, 0, 0);
    tilemap_scroll_sync();
    tilemap_force_redraw();
    tilemap_compose_btf();
    tilemap_present();

    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        tilemap_scroll(0, 0, (i * 16) % (TILEMAP_ROWS * TILE_H));
        tilemap_compose_scroll();
        tilemap_present();
    }
    end = kapi->get_tick();
    t_scroll = end - start;
    print_result("compose_scroll V-diff", t_scroll);
    if (iterations > 0) {
        printf("  avg: %lu ms/frame\n",
                      (t_scroll * 10) / (u32)iterations);
    }
}

/* ====================================================================== */
/*  メイン                                                                */
/* ====================================================================== */

void main(int argc, char **argv, KernelAPI *api)
{
    int run_full = 1;
    int run_flip = 0;
    int run_scroll = 0;

    kapi = api;

    /* 引数解析: "s" → フリップ+スクロール, "v" → スクロールのみ */
    if (argc >= 2) {
        if (argv[1][0] == 'v') {
            run_full = 0;
            run_flip = 0;
            run_scroll = 1;
        } else if (argv[1][0] == 's') {
            run_full = 0;
            run_flip = 1;
            run_scroll = 1;
        } else if (argv[1][0] == 'f') {
            run_full = 0;
            run_flip = 1;
        }
    } else {
        run_flip = 1;
        run_scroll = 1;
    }

    printf("=== Tilemap Benchmark ===\n");

    tilemap_init(api);
    gfx_clear(0);
    gfx_present();
    tilemap_set_origin(128, 8);

    /* タイル定義 */
    tilemap_define(1, tile_opaque);
    tilemap_define(2, tile_partial);
    tilemap_define(3, tile_transparent);

    if (run_full) {
        /* --- 全面描画ベンチマーク --- */
        bench_full_draw("1-Layer Opaque", setup_scenario_a, 5);
        bench_full_draw("2-Layer Mixed",  setup_scenario_b, 5);
        bench_full_draw("4-Layer Heavy",  setup_scenario_c, 3);

        /* --- 差分更新ベンチマーク --- */
        bench_delta_update("2-Layer Mixed", setup_scenario_b, 30);
    }

    if (run_flip) {
        /* --- フリップベンチマーク --- */
        bench_flip(3);
    }

    if (run_scroll) {
        /* --- スクロールベンチマーク --- */
        bench_scroll(10);
    }

    /* クリーンアップ */
    gfx_clear(0);
    gfx_present();
    tilemap_shutdown();

    printf("\nDone. Press any key.\n");
    api->kbd_getchar();
}

