/* ======================================================================== */
/*  OS32 サードパーティアプリの最小例                                        */
/*                                                                          */
/*  OS32 SDK だけでビルドする。OS のソースツリーは一切参照しない。            */
/*      tar xzf os32-sdk-39.tar.gz                                          */
/*      make OS32_SDK=$(pwd)/os32-sdk-39                                    */
/* ======================================================================== */
#include "os32api.h"
#include "libos32gfx.h"

/* main() はソース中の最初の関数でなければならない (crt0 が先頭へ飛ぶ) */
void main(int argc, char **argv, KernelAPI *api)
{
    int i;
    u32 until;
    (void)argc; (void)argv;

    api->kprintf(0x07, "hello3p: start\n");

    libos32gfx_init(api);
    api->kprintf(0x07, "hello3p: gfx ready\n");

    gfx_clear(0);
    for (i = 0; i < 16; i++) {
        gfx_fill_rect(40 + i * 34, 160, 30, 80, (u8)i);
    }

    /* gfx_present() はダーティ矩形を登録するだけ。
       実際に VRAM へ転送するのは KAPI の gfx_present_dirty()。 */
    gfx_present();
    api->gfx_present_dirty();
    api->kprintf(0x07, "hello3p: presented\n");

    /* 5 秒表示を保持 (PIT 100Hz) */
    until = api->get_tick() + 500;
    while (api->get_tick() < until) {
        /* 保持 */
    }

    libos32gfx_shutdown();
    api->kprintf(0x07, "hello3p: done\n");
}
