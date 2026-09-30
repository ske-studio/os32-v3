/*
 * blit_test.c — gfx_blit_transparent テストプログラム
 *
 * テスト内容:
 *   1. 正確性検証: gfx_blit_colorkey(旧)と gfx_blit_transparent(新)の
 *      描画結果をピクセル単位で比較し、全ピクセル一致を確認する
 *   2. 性能比較: 同じ描画を反復実行し、tick数を比較する
 *
 * 対象サイズ: 16x16, 32x32 (バイト境界条件を網羅)
 * パターン: 全透明 / 全不透明 / 市松模様(部分透明)
 */

#include "os32api.h"
#include "libos32gfx.h"
#include <string.h>

extern int sprintf(char *str, const char *format, ...);

static KernelAPI *kapi;

/* ---- サーフェス用バッファ (グローバルでスタックオーバーフロー回避) ---- */
static u8 buf_16[16 * 2 * 4];   /* 16x16: pitch=2, 4planes */
static u8 buf_32[32 * 4 * 4];   /* 32x32: pitch=4, 4planes */

/* BB全体のスナップショット用 (80 * 400 * 4planes = 128000 bytes) */
static u8 snap_a[4][80 * 400];
static u8 snap_b[4][80 * 400];

/* ---- サーフェス構築 ---- */
static void setup_surface(GFX_Surface *surf, u8 *buf, int size, int pattern)
{
    int p, i;
    int pitch = size / 8;
    int total = pitch * size;

    surf->w = size;
    surf->h = size;
    surf->pitch = pitch;
    surf->_pool_idx = -1;

    for (p = 0; p < 4; p++) {
        surf->planes[p] = buf + (p * total);
    }

    /* パターン別のデータ書き込み */
    for (i = 0; i < total; i++) {
        int y = i / pitch;
        u8 val;

        switch (pattern) {
        case 0: /* 全透明 (color=0) */
            val = 0x00;
            break;
        case 1: /* 全不透明 */
            val = 0xFF;
            break;
        case 2: /* 市松模様 (交互に透明/不透明) */
            val = (y & 1) ? 0xAA : 0x55;
            break;
        default:
            val = 0x00;
            break;
        }

        /* 色7 (白: plane0,1,2 ON) で描画 */
        surf->planes[0][i] = val;
        surf->planes[1][i] = val;
        surf->planes[2][i] = val;
        surf->planes[3][i] = 0;  /* plane3 = 0 */
    }
}

/* ---- BB スナップショット取得 ---- */
static void snapshot_bb(u8 dst[4][80 * 400])
{
    int p;
    for (p = 0; p < 4; p++) {
        memcpy(dst[p], gfx_fb.planes[p], 80 * 400);
    }
}

/* ---- BB スナップショット比較 ---- */
static int compare_snapshots(int dx, int dy, int w, int h)
{
    int p, y, x;
    int pitch = 80;
    int mismatch = 0;

    for (p = 0; p < 4; p++) {
        for (y = dy; y < dy + h && y < 400; y++) {
            for (x = dx; x < dx + w && x < 640; x++) {
                int off = y * pitch + (x >> 3);
                u8 bit = 0x80 >> (x & 7);
                u8 a = snap_a[p][off] & bit;
                u8 b = snap_b[p][off] & bit;
                if (a != b) {
                    if (mismatch < 5) {
                        kapi->kprintf(ATTR_RED,
                            "  MISMATCH: plane%d x=%d y=%d old=0x%02X new=0x%02X\r\n",
                            p, x, y, snap_a[p][off], snap_b[p][off]);
                    }
                    mismatch++;
                }
            }
        }
    }
    return mismatch;
}

/* ---- 1テストケース実行 ---- */
static int run_test(const char *label, int size, int pattern, int dx, int dy)
{
    GFX_Surface surf;
    u8 *buf;
    int mismatch;

    buf = (size == 16) ? buf_16 : buf_32;
    setup_surface(&surf, buf, size, pattern);

    kapi->kprintf(ATTR_WHITE, "  %s: size=%dx%d pat=%d pos=(%d,%d) ... ",
                  label, size, size, pattern, dx, dy);

    /* (A) gfx_blit_colorkey で描画 → スナップショット */
    gfx_clear(1);  /* 背景を青(1)で塗る */
    gfx_blit_colorkey(dx, dy, &surf, NULL, 0);
    gfx_present();
    snapshot_bb(snap_a);

    /* (B) gfx_blit_transparent で描画 → スナップショット */
    gfx_clear(1);
    gfx_blit_transparent(dx, dy, &surf, NULL);
    gfx_present();
    snapshot_bb(snap_b);

    /* 比較 */
    mismatch = compare_snapshots(dx, dy, size, size);

    if (mismatch == 0) {
        kapi->kprintf(ATTR_GREEN, "OK\r\n");
    } else {
        kapi->kprintf(ATTR_RED, "FAIL (%d mismatches)\r\n", mismatch);
    }

    return mismatch;
}

/* ---- ベンチマーク ---- */
static void run_benchmark(int size, int pattern)
{
    GFX_Surface surf;
    u8 *buf;
    u32 start, end;
    int i;
    int iterations;
    u32 t_old, t_new;
    int ratio_x10;

    /* 旧関数はピクセル単位なので大タイルは反復回数を減らす */
    iterations = (size <= 16) ? 1000 : 200;

    buf = (size == 16) ? buf_16 : buf_32;
    setup_surface(&surf, buf, size, pattern);

    kapi->kprintf(ATTR_WHITE, "  Bench %dx%d pat=%d x%d: ", size, size, pattern, iterations);

    /* 旧: gfx_blit_colorkey */
    gfx_clear(0);
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        gfx_blit_colorkey(100, 100, &surf, NULL, 0);
    }
    end = kapi->get_tick();
    t_old = end - start;

    /* 新: gfx_blit_transparent */
    gfx_clear(0);
    start = kapi->get_tick();
    for (i = 0; i < iterations; i++) {
        gfx_blit_transparent(100, 100, &surf, NULL);
    }
    end = kapi->get_tick();
    t_new = end - start;

    kapi->kprintf(ATTR_CYAN, "old=%lu new=%lu", t_old, t_new);

    if (t_new > 0 && t_new < t_old) {
        ratio_x10 = (int)((t_old * 10) / t_new);
        kapi->kprintf(ATTR_GREEN, " (%d.%dx faster)\r\n",
                      ratio_x10 / 10, ratio_x10 % 10);
    } else {
        kapi->kprintf(ATTR_YELLOW, " (no speedup)\r\n");
    }
}

/* ---- メイン ---- */
void main(int argc, char **argv, KernelAPI *api)
{
    int total_fail = 0;

    kapi = api;
    api->kprintf(ATTR_WHITE, "=== gfx_blit_transparent Test ===\r\n\r\n");

    libos32gfx_init(api);

    /* ---- 正確性テスト ---- */
    api->kprintf(ATTR_CYAN, "[Correctness Tests]\r\n");

    /* 16x16 テスト */
    total_fail += run_test("16x16 transparent",  16, 0, 100, 100);
    total_fail += run_test("16x16 opaque",       16, 1, 100, 100);
    total_fail += run_test("16x16 checker",      16, 2, 100, 100);

    /* 32x32 テスト (32bit パスを通る) */
    total_fail += run_test("32x32 transparent",  32, 0, 100, 100);
    total_fail += run_test("32x32 opaque",       32, 1, 100, 100);
    total_fail += run_test("32x32 checker",      32, 2, 100, 100);

    /* 境界テスト: バイト境界位置 */
    total_fail += run_test("16x16 at (0,0)",     16, 2,   0,   0);
    total_fail += run_test("16x16 at (624,384)", 16, 2, 624, 384);
    total_fail += run_test("32x32 at (0,0)",     32, 2,   0,   0);
    total_fail += run_test("32x32 at (608,368)", 32, 2, 608, 368);

    /* src_rect テスト (部分矩形) */
    {
        GFX_Surface surf;
        GFX_Rect rect;
        int mismatch;

        setup_surface(&surf, buf_32, 32, 2);
        rect.x = 0; rect.y = 0; rect.w = 16; rect.h = 16;

        api->kprintf(ATTR_WHITE, "  src_rect 16x16 from 32x32 ... ");

        gfx_clear(1);
        gfx_blit_colorkey(200, 200, &surf, &rect, 0);
        gfx_present();
        snapshot_bb(snap_a);

        gfx_clear(1);
        gfx_blit_transparent(200, 200, &surf, &rect);
        gfx_present();
        snapshot_bb(snap_b);

        mismatch = compare_snapshots(200, 200, 16, 16);
        if (mismatch == 0) {
            api->kprintf(ATTR_GREEN, "OK\r\n");
        } else {
            api->kprintf(ATTR_RED, "FAIL (%d)\r\n", mismatch);
            total_fail += mismatch;
        }
    }

    /* ---- 結果サマリー ---- */
    api->kprintf(ATTR_WHITE, "\r\n");
    if (total_fail == 0) {
        api->kprintf(ATTR_GREEN, "All correctness tests PASSED.\r\n\r\n");
    } else {
        api->kprintf(ATTR_RED, "FAILED: %d mismatches total.\r\n\r\n", total_fail);
    }

    /* ---- ベンチマーク ---- */
    api->kprintf(ATTR_CYAN, "[Benchmark (ticks, lower=better)]\r\n");
    run_benchmark(16, 1);  /* 全不透明 */
    run_benchmark(16, 2);  /* 市松模様 */
    run_benchmark(32, 1);
    run_benchmark(32, 2);

    /* クリーンアップ */
    gfx_clear(0);
    gfx_present();
    libos32gfx_shutdown();

    api->kprintf(ATTR_WHITE, "\r\nDone. Press any key.\r\n");
    api->kbd_getchar();
}
