/* ======================================================================== */
/*  BENCH_SCALE2X — libpyxel Phase 0: スケーリング性能ベンチマーク          */
/*                                                                          */
/*  03_SCALING.md §6 で定義された全ベンチマーク項目を計測する。              */
/*  結果はテキストVRAM + ログファイル (0:/scale2x.log) に出力。              */
/* ======================================================================== */

#include <stdio.h>
#include <string.h>
#include "os32api.h"
#include "libos32gfx.h"

static KernelAPI *kapi = NULL;
static int log_fd = -1;

/* ======================================================================== */
/*  Scale2x LUT — 1バイト(8px) を 2バイト(16px) に2倍展開するテーブル        */
/*  03_SCALING.md §4-1 の設計に準拠                                         */
/* ======================================================================== */
static u16 scale2x_lut[256];

static void init_scale2x_lut(void)
{
    int i, bit;
    for (i = 0; i < 256; i++) {
        u16 val = 0;
        for (bit = 0; bit < 8; bit++) {
            if (i & (0x80 >> bit)) {
                val |= (0xC000 >> (bit * 2));
            }
        }
        scale2x_lut[i] = val;
    }
}

/* ======================================================================== */
/*  ユーティリティ                                                          */
/* ======================================================================== */

/* tick (100Hz) をミリ秒に変換 */
#define TICKS_TO_MS(t) ((t) * 10)

static void log_result(const char *name, u32 ticks, const char *desc)
{
    char buf[160];
    int len;
    u32 ms = TICKS_TO_MS(ticks);

    kapi->kprintf(ATTR_GREEN, "  %-24s: %3lu ticks (%4lu ms) %s\r\n",
                  name, (unsigned long)ticks, (unsigned long)ms, desc);

    if (log_fd >= 0) {
        len = sprintf(buf, "%-24s: %3lu ticks (%4lu ms) %s\r\n",
                      name, (unsigned long)ticks, (unsigned long)ms, desc);
        kapi->sys_write(log_fd, buf, len);
    }
}

static void log_header(const char *title)
{
    char buf[80];
    int len;

    kapi->kprintf(ATTR_CYAN, "\r\n=== %s ===\r\n", title);

    if (log_fd >= 0) {
        len = sprintf(buf, "\r\n=== %s ===\r\n", title);
        kapi->sys_write(log_fd, buf, len);
    }
}

static void wait_ticks(u32 n)
{
    u32 start = kapi->get_tick();
    while (kapi->get_tick() - start < n) {
        /* busy wait */
    }
}

/* ======================================================================== */
/*  テスト1: gfx_present_rect 転送速度比較                                  */
/*  3解像度で present_rect の所要時間を比較する                              */
/* ======================================================================== */
static void test_present_rect(void)
{
    u32 start, end;
    int iter;

    log_header("Test 1: gfx_present_rect Transfer Speed");

    /* 描画領域をパターンで埋める */
    gfx_clear(0);
    {
        int y;
        for (y = 0; y < 400; y += 2) {
            gfx_hline(0, y, 640, (u8)((y / 2) % 16));
        }
    }

    /* 320x200 (既存ゲームデモ相当: 32KB) */
    start = kapi->get_tick();
    for (iter = 0; iter < 100; iter++) {
        kapi->gfx_present_rect(0, 0, 320, 200);
    }
    end = kapi->get_tick();
    log_result("320x200 x100", end - start, "~32KB/frame");

    /* 480x320 (候補B/C: 75KB) */
    start = kapi->get_tick();
    for (iter = 0; iter < 100; iter++) {
        kapi->gfx_present_rect(0, 0, 480, 320);
    }
    end = kapi->get_tick();
    log_result("480x320 x100", end - start, "~75KB/frame");

    /* 512x384 (候補A Pyxel標準: 96KB) */
    start = kapi->get_tick();
    for (iter = 0; iter < 100; iter++) {
        kapi->gfx_present_rect(0, 0, 512, 384);
    }
    end = kapi->get_tick();
    log_result("512x384 x100", end - start, "~96KB/frame");

    /* 640x400 (全画面: 125KB) */
    start = kapi->get_tick();
    for (iter = 0; iter < 100; iter++) {
        gfx_present();
    }
    end = kapi->get_tick();
    log_result("640x400 x100", end - start, "~125KB/frame (full)");
}

/* ======================================================================== */
/*  テスト2: Scale2x LUT展開ベンチマーク                                    */
/*  256バイト → 512バイト のLUT展開をメモリ→メモリで計測                    */
/* ======================================================================== */
static u8 lut_src[32 * 192];      /* 256px/8 * 192行 = 6144 bytes */
static u8 lut_dst[64 * 384];      /* 512px/8 * 384行 = 24576 bytes */

static void test_lut_expand(void)
{
    u32 start, end;
    int iter, src_y, x_byte;

    log_header("Test 2: Scale2x LUT Expand (Mem->Mem)");

    /* ソースデータを適当なパターンで埋める */
    {
        int i;
        for (i = 0; i < (int)sizeof(lut_src); i++) {
            lut_src[i] = (u8)(i * 37 + 73);
        }
    }

    /* LUT展開: 256x192 → 512x384 (メモリ内のみ) */
    start = kapi->get_tick();
    for (iter = 0; iter < 100; iter++) {
        for (src_y = 0; src_y < 192; src_y++) {
            int dst_y = src_y * 2;
            u8 *src_row = lut_src + src_y * 32;
            u8 *dst_row0 = lut_dst + dst_y * 64;
            u8 *dst_row1 = dst_row0 + 64;

            for (x_byte = 0; x_byte < 32; x_byte++) {
                u16 expanded = scale2x_lut[src_row[x_byte]];
                dst_row0[x_byte * 2]     = (u8)(expanded >> 8);
                dst_row0[x_byte * 2 + 1] = (u8)(expanded & 0xFF);
                dst_row1[x_byte * 2]     = (u8)(expanded >> 8);
                dst_row1[x_byte * 2 + 1] = (u8)(expanded & 0xFF);
            }
        }
    }
    end = kapi->get_tick();
    log_result("LUT 256x192->512x384 x100", end - start, "mem-to-mem");
}

/* ======================================================================== */
/*  テスト3: Scale2x全体 (LUT展開 + BB書き込み)                             */
/*  BB内の256x192領域をLUTで512x384に展開してBBに書き戻す                    */
/* ======================================================================== */
static void test_scale2x_to_bb(void)
{
    u32 start, end;
    int iter, plane, src_y, x_byte;
    GFX_Framebuffer fb;

    log_header("Test 3: Scale2x Full (LUT + BB write)");

    kapi->gfx_get_framebuffer(&fb);

    /* ソース領域 (BB内 0,0 から 256x192) にパターンを描画 */
    {
        int y;
        for (y = 0; y < 192; y++) {
            gfx_hline(0, y, 256, (u8)(y % 16));
        }
    }

    /* BB内で LUT展開: 各プレーンの 256x192 → 512x384 へスケーリング */
    /* 注: 実際のlibpyxelでは描画領域とスケーリング先は分離する必要がある */
    /*      ここでは性能計測のため、同一BB内の別領域に展開する             */
    start = kapi->get_tick();
    for (iter = 0; iter < 10; iter++) {
        for (plane = 0; plane < 4; plane++) {
            u8 *src_plane = fb.planes[plane];

            for (src_y = 0; src_y < 192; src_y++) {
                int dst_y = src_y * 2;
                u8 *src_row = src_plane + src_y * 80;
                u8 *dst_row0 = lut_dst + dst_y * 64;
                u8 *dst_row1 = dst_row0 + 64;

                for (x_byte = 0; x_byte < 32; x_byte++) {
                    u16 expanded = scale2x_lut[src_row[x_byte]];
                    dst_row0[x_byte * 2]     = (u8)(expanded >> 8);
                    dst_row0[x_byte * 2 + 1] = (u8)(expanded & 0xFF);
                    dst_row1[x_byte * 2]     = (u8)(expanded >> 8);
                    dst_row1[x_byte * 2 + 1] = (u8)(expanded & 0xFF);
                }
            }
        }
    }
    end = kapi->get_tick();
    log_result("Scale2x BB x10", end - start, "4planes * 192lines");
}

/* ======================================================================== */
/*  テスト4: gfx_fill_rect 2x2 連続描画                                    */
/*  Pyxelの pyxel_pset() 相当: 256x192回の 2x2ブロック描画                  */
/* ======================================================================== */
static void test_fill_rect_2x2(void)
{
    u32 start, end;
    int x, y, iter;

    log_header("Test 4: gfx_fill_rect(2x2) 256x192 pixels");

    start = kapi->get_tick();
    for (iter = 0; iter < 5; iter++) {
        for (y = 0; y < 192; y++) {
            for (x = 0; x < 256; x++) {
                gfx_fill_rect(x * 2, y * 2, 2, 2, (u8)((x + y) % 16));
            }
        }
    }
    end = kapi->get_tick();
    log_result("fill_rect 2x2 x5", end - start, "49152 rects/iter");
}

/* ======================================================================== */
/*  テスト5: ダーティ矩形転送ベンチマーク                                    */
/*  小/中/大/全画面の4パターンでdirty rect転送速度を比較                     */
/* ======================================================================== */
static void test_dirty_rect(void)
{
    u32 start, end;
    int iter;

    log_header("Test 5: Dirty Rect Transfer");

    /* まずBBに描画内容を用意 */
    gfx_clear(0);
    {
        int i;
        for (i = 0; i < 50; i++) {
            gfx_fill_rect(i * 12, i * 7, 40, 30, (u8)(i % 15 + 1));
        }
    }

    /* (a) 小ダーティ: 32x32 x1 */
    start = kapi->get_tick();
    for (iter = 0; iter < 1000; iter++) {
        kapi->gfx_add_dirty_rect(100, 100, 32, 32);
        kapi->gfx_present_dirty();
    }
    end = kapi->get_tick();
    log_result("Small 32x32 x1000", end - start, "sprite 1");

    /* (b) 中ダーティ: 64x64 x4 */
    start = kapi->get_tick();
    for (iter = 0; iter < 500; iter++) {
        kapi->gfx_add_dirty_rect(50, 50, 64, 64);
        kapi->gfx_add_dirty_rect(200, 50, 64, 64);
        kapi->gfx_add_dirty_rect(50, 200, 64, 64);
        kapi->gfx_add_dirty_rect(200, 200, 64, 64);
        kapi->gfx_present_dirty();
    }
    end = kapi->get_tick();
    log_result("Medium 64x64x4 x500", end - start, "multi sprites");

    /* (c) 大ダーティ: 160x128 x1 */
    start = kapi->get_tick();
    for (iter = 0; iter < 500; iter++) {
        kapi->gfx_add_dirty_rect(100, 100, 160, 128);
        kapi->gfx_present_dirty();
    }
    end = kapi->get_tick();
    log_result("Large 160x128 x500", end - start, "1/3 screen");

    /* (d) 全画面ダーティ: 512x384 */
    start = kapi->get_tick();
    for (iter = 0; iter < 100; iter++) {
        kapi->gfx_add_dirty_rect(0, 0, 512, 384);
        kapi->gfx_present_dirty();
    }
    end = kapi->get_tick();
    log_result("Full 512x384 x100", end - start, "full game area");
}

/* ======================================================================== */
/*  テスト6: 総合模擬 — ゲームループ風のFPS計測                             */
/*  背景クリア + 矩形8枚描画 + dirty rect転送 で実効FPSを計測               */
/* ======================================================================== */
static void test_game_loop_sim(void)
{
    u32 start_tick, cur_tick;
    int frames;
    int fps;
    int i, iter;
    char buf[80];
    int len;

    log_header("Test 6: Simulated Game Loop (3sec)");

    gfx_clear(0);
    gfx_present();

    /* 3秒間ゲームループを模擬実行してフレーム数を計測 */
    frames = 0;
    start_tick = kapi->get_tick();

    for (iter = 0; iter < 10000; iter++) {
        cur_tick = kapi->get_tick();
        if (cur_tick - start_tick >= 300) break; /* 3秒 = 300 ticks */

        /* 背景クリア (ゲーム領域のみ) */
        gfx_fill_rect(0, 0, 512, 384, 1);

        /* 矩形8枚描画 (スプライト相当) */
        for (i = 0; i < 8; i++) {
            int sx = (frames * (i + 1) * 3) % 480;
            int sy = (frames * (i + 1) * 2) % 352;
            gfx_fill_rect(sx, sy, 32, 32, (u8)(i + 2));
        }

        /* dirty rect転送 */
        kapi->gfx_add_dirty_rect(0, 0, 512, 384);
        kapi->gfx_present_dirty();

        frames++;
    }

    cur_tick = kapi->get_tick();
    fps = 0;
    if (cur_tick > start_tick) {
        fps = (frames * 100) / (int)(cur_tick - start_tick);
    }

    kapi->kprintf(ATTR_WHITE, "  Frames: %d in %lu ticks => %d fps\r\n",
                  frames, (unsigned long)(cur_tick - start_tick), fps);

    if (log_fd >= 0) {
        len = sprintf(buf, "  Frames: %d => %d fps\r\n", frames, fps);
        kapi->sys_write(log_fd, buf, len);
    }

    /* 判定 */
    if (fps >= 30) {
        kapi->kprintf(ATTR_GREEN, "  PASS: 30fps以上達成\r\n");
    } else if (fps >= 20) {
        kapi->kprintf(ATTR_YELLOW, "  OK: 20fps以上 (最適化で改善可能)\r\n");
    } else {
        kapi->kprintf(ATTR_RED, "  WARN: 20fps未満 — ダーティ方式必須\r\n");
    }
}

/* ======================================================================== */
/*  テスト7: スプライトオーバーレイ方式ゲームループ模擬                      */
/*  gfx_save_rect/gfx_restore_rect で背景を保存/復元する方式でのFPS計測      */
/*  07_BENCH_RESULTS.md §3 推奨方式の検証                                   */
/* ======================================================================== */

/* 背景退避バッファ: 32x32 × 4プレーン = 4 * (32/8) * 32 = 512 bytes/枚 */
#define SPR_W 32
#define SPR_H 32
#define SPR_BG_SIZE (SPR_W / 8 * SPR_H * 4)
static u8 bg_buf[8][SPR_BG_SIZE];

static void test_dirty_game_loop(void)
{
    u32 start_tick, cur_tick;
    int frames;
    int fps;
    int i, iter;
    char buf[80];
    int len;
    int sx[8], sy[8];
    int prev_sx[8], prev_sy[8];

    log_header("Test 7: Sprite Overlay Game Loop (3sec)");

    /* 背景を描画 (市松模様) — 初回のみ */
    gfx_clear(1);
    {
        int bx, by;
        for (by = 0; by < 384; by += 16) {
            for (bx = 0; bx < 512; bx += 16) {
                if (((bx / 16) + (by / 16)) % 2 == 0) {
                    gfx_fill_rect(bx, by, 16, 16, 5);
                }
            }
        }
    }
    gfx_present();

    /* 初期位置設定 */
    for (i = 0; i < 8; i++) {
        sx[i] = (i * 55) % 480;
        sy[i] = (i * 40) % 352;
        prev_sx[i] = sx[i];
        prev_sy[i] = sy[i];
    }

    /* フェーズ0: 初回の背景退避 + スプライト描画 */
    for (i = 0; i < 8; i++) {
        gfx_save_rect(sx[i], sy[i], SPR_W, SPR_H, bg_buf[i]);
    }
    for (i = 0; i < 8; i++) {
        gfx_fill_rect(sx[i], sy[i], SPR_W, SPR_H, (u8)(i + 2));
        kapi->gfx_add_dirty_rect(sx[i], sy[i], SPR_W, SPR_H);
    }
    kapi->gfx_present_dirty();

    frames = 0;
    start_tick = kapi->get_tick();

    for (iter = 0; iter < 10000; iter++) {
        cur_tick = kapi->get_tick();
        if (cur_tick - start_tick >= 300) break; /* 3秒 */

        /* フェーズ1: 全スプライトの背景を復元 (消去) */
        for (i = 0; i < 8; i++) {
            gfx_restore_rect(prev_sx[i], prev_sy[i],
                             SPR_W, SPR_H, bg_buf[i]);
        }

        /* フェーズ2: 座標更新 */
        for (i = 0; i < 8; i++) {
            prev_sx[i] = sx[i];
            prev_sy[i] = sy[i];
            sx[i] = (frames * (i + 1) * 3) % 480;
            sy[i] = (frames * (i + 1) * 2) % 352;
        }

        /* フェーズ3: 新しい位置の背景を退避 */
        for (i = 0; i < 8; i++) {
            gfx_save_rect(sx[i], sy[i], SPR_W, SPR_H, bg_buf[i]);
        }

        /* フェーズ4: スプライト描画 */
        for (i = 0; i < 8; i++) {
            gfx_fill_rect(sx[i], sy[i], SPR_W, SPR_H, (u8)(i + 2));
        }

        /* フェーズ5: dirty rect登録 + 転送 */
        for (i = 0; i < 8; i++) {
            kapi->gfx_add_dirty_rect(prev_sx[i], prev_sy[i],
                                     SPR_W, SPR_H);
            kapi->gfx_add_dirty_rect(sx[i], sy[i], SPR_W, SPR_H);
        }
        kapi->gfx_present_dirty();

        frames++;
    }

    cur_tick = kapi->get_tick();
    fps = 0;
    if (cur_tick > start_tick) {
        fps = (frames * 100) / (int)(cur_tick - start_tick);
    }

    kapi->kprintf(ATTR_WHITE, "  Frames: %d in %lu ticks => %d fps\r\n",
                  frames, (unsigned long)(cur_tick - start_tick), fps);

    if (log_fd >= 0) {
        len = sprintf(buf, "  Frames: %d => %d fps (sprite overlay)\r\n",
                      frames, fps);
        kapi->sys_write(log_fd, buf, len);
    }

    if (fps >= 30) {
        kapi->kprintf(ATTR_GREEN, "  PASS: 30fps以上達成 (sprite overlay)\r\n");
    } else if (fps >= 20) {
        kapi->kprintf(ATTR_YELLOW, "  OK: 20fps以上 (sprite overlay)\r\n");
    } else {
        kapi->kprintf(ATTR_RED, "  WARN: 20fps未満 — 更なる最適化が必要\r\n");
    }
}

/* ======================================================================== */
/*  メインエントリ                                                          */
/* ======================================================================== */

int main(int argc, char *argv[], KernelAPI *api)
{
    if (!api) return -1;
    kapi = api;

    kapi->kprintf(ATTR_WHITE,
        "========================================\r\n"
        " libpyxel Phase 0: Scale2x Benchmark\r\n"
        " 03_SCALING.md Sec.6 Verification\r\n"
        "========================================\r\n");

    /* LUT初期化 */
    init_scale2x_lut();

    /* ログファイルオープン */
    log_fd = kapi->sys_open("/tmp/scale2x.log",
                            KAPI_O_CREAT | KAPI_O_WRONLY | KAPI_O_TRUNC);

    /* GFX初期化 */
    libos32gfx_init(kapi);

    /* テスト1: gfx_present_rect 転送速度 */
    test_present_rect();

    /* テスト2: LUT展開 (メモリ→メモリ) */
    test_lut_expand();

    /* テスト3: Scale2x 全体 (LUT + BB書き込み) */
    test_scale2x_to_bb();

    /* テスト4: gfx_fill_rect 2x2 連続描画 */
    test_fill_rect_2x2();

    /* テスト5: ダーティ矩形転送 */
    test_dirty_rect();

    /* GFXシャットダウン → テスト結果をテキストで見せるため */
    gfx_clear(0);
    gfx_present();
    libos32gfx_shutdown();
    kapi->tvram_clear();

    kapi->kprintf(ATTR_WHITE,
        "\r\n--- GFX tests done. Starting game loop sims... ---\r\n");
    wait_ticks(200); /* 2秒待ち */

    /* GFX再初期化 (ゲームループテスト用) */
    libos32gfx_init(kapi);

    /* テスト6: 総合模擬 (全画面更新) */
    test_game_loop_sim();

    /* テスト7: ダーティ方式ゲームループ */
    test_dirty_game_loop();

    /* クリーンアップ */
    gfx_clear(0);
    gfx_present();
    libos32gfx_shutdown();
    kapi->tvram_clear();

    if (log_fd >= 0) {
        kapi->sys_close(log_fd);
    }

    kapi->kprintf(ATTR_CYAN,
        "\r\n========================================\r\n"
        " All benchmarks completed.\r\n"
        " Results saved to /tmp/scale2x.log\r\n"
        "========================================\r\n");

    return 0;
}
