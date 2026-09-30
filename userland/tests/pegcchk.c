/* ======================================================================== */
/*  PEGCCHK.C — PEGC 640x480 の出入りを画面を見ずに確かめる (実機・NP21/W)   */
/*                                                                          */
/*  Usage: pegcchk [秒数]                                                    */
/*    秒数  480 ラインに留まる時間 (既定 3、1〜30)                           */
/*                                                                          */
/*  票 docs/tasks/realhw/TASK_PEGC480_REALHW.md §5 (C)。シリアル (rshell) から */
/*  走らせ、結果を**テキストで**出す:                                         */
/*    1. バックエンドが PEGC か (gfx_screen_info: 8bpp パックドで HW 描画なし) */
/*    2. gfx_init で 640x480 へ (カーネルの pegc_init — 実機 ROM と同じ列)、   */
/*       テスト画 (枠・縦線 x=0/1/320/638/639・16 ラインごとの横線・対角線)を */
/*       描いて present、指定秒数待つ                                         */
/*    3. gfx_shutdown で CUI へ。カーネルが 2 行出す:                          */
/*         [pegc] enter 09a8=.. msk= dsp= lcd= clk= ext= 800l= fifo_to= vs_to= */
/*         [pegc] exit  09a8=.. ...                                          */
/*       (480 ラインへ入った直後と戻った後の 09A8h・09A0h の読み、GDC の FIFO */
/*        と VSYNC の待ちの打ち切り数。backend_pegc.c pegc_diag_take)         */
/*    4. 戻った後の INT 18h AH=31h は続けて `v86 -g` を走らせて R 31h の行で */
/*       見る (v86_* を呼ぶと app.conf の宣言が `cui` になり、gfx_init を呼ぶ  */
/*       このプログラムの `gfx` と両立しない — tools/check_manifests.py)。     */
/*    5. 最後に "pegcchk done" — ここまで出れば応答は続いている。             */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>

#define PEGCCHK_W       640
#define PEGCCHK_H       480
#define PEGCCHK_GRID    16      /* 横線の間隔 (テキスト 1 行 = 16 ラスタ) */
#define TICKS_PER_SEC   100

static void draw_pattern(u8 *fb, int pitch);

/* main() はファイルの最初の関数 (sdk/crt/crt0.asm の入口、CLAUDE.md)。 */
int main(int argc, char **argv, KernelAPI *api)
{
    GFX_ScreenInfo si;
    GFX_Framebuffer fb;
    int secs = 3, i, has_hw, drawn = 0;
    int in_w, in_h;
    u32 t0;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            const char *p = argv[i];
            secs = 0;
            while (*p >= '0' && *p <= '9') secs = secs * 10 + (*p++ - '0');
        } else {
            printf("Usage: pegcchk [seconds]\n");
            return 1;
        }
    }
    if (secs < 1) secs = 1;
    if (secs > 30) secs = 30;

    if (api->version < 40) {
        printf("pegcchk: KAPI v%d < 40 (gfx_screen_info)\n", api->version);
        return 1;
    }
    api->gfx_screen_info(&si);
    has_hw = (si.flags & (GFX_CAP_HW_FILL | GFX_CAP_HW_BLT)) ? 1 : 0;
    if (si.format != GFX_FMT_PACKED8 || has_hw) {
        printf("pegcchk: SKIP backend is %s, not pegc (gfxmode pegc, then reset)\n",
               si.format != GFX_FMT_PACKED8 ? "pc98" : "cirrus");
        return 2;
    }

    printf("pegcchk: entering 640x480 for %d s\n", secs);
    api->gfx_init();
    api->gfx_screen_info(&si);
    in_w = si.width;
    in_h = si.height;
    api->gfx_get_framebuffer(&fb);
    if (fb.planes[0] && fb.pitch >= PEGCCHK_W && in_h >= PEGCCHK_H) {
        draw_pattern(fb.planes[0], fb.pitch);
        api->gfx_present();
        drawn = 1;
    }
    t0 = api->get_tick();
    while (api->get_tick() - t0 < (u32)secs * TICKS_PER_SEC) api->sys_halt();
    api->gfx_shutdown();

    printf("R mode     : %dx%d while in (expect 640x480), pattern %s\n",
           in_w, in_h, drawn ? "drawn" : "NOT drawn");
    printf("R back     : CUI after %lu ticks\n",
           (unsigned long)(api->get_tick() - t0));

    printf("pegcchk done\n");
    return 0;
}

static void hline(u8 *fb, int pitch, int y, u8 c)
{
    int x;
    for (x = 0; x < PEGCCHK_W; x++) fb[y * pitch + x] = c;
}

static void vline(u8 *fb, int pitch, int x, u8 c)
{
    int y;
    for (y = 0; y < PEGCCHK_H; y++) fb[y * pitch + x] = c;
}

/* 枠と縦線は「画面全体が一様にずれるか (同期) / 下へ行くほどずれるか
 * (1 行の読み出し幅)」を写真で分けるためのもの (票 §3 段 0 のテスト画)。 */
static void draw_pattern(u8 *fb, int pitch)
{
    int y;
    for (y = 0; y < PEGCCHK_H; y++) {
        int x;
        for (x = 0; x < PEGCCHK_W; x++) fb[y * pitch + x] = 0;
    }
    for (y = 0; y < PEGCCHK_H; y += PEGCCHK_GRID) hline(fb, pitch, y, 8);
    hline(fb, pitch, 0, 15);
    hline(fb, pitch, PEGCCHK_H - 1, 15);
    vline(fb, pitch, 0, 15);
    vline(fb, pitch, 1, 12);
    vline(fb, pitch, PEGCCHK_W / 2, 10);
    vline(fb, pitch, PEGCCHK_W - 2, 12);
    vline(fb, pitch, PEGCCHK_W - 1, 15);
    for (y = 0; y < PEGCCHK_H; y++) fb[y * pitch + y] = 14;
}
