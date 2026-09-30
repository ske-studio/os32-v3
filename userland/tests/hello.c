/* ======================================================================== */
/*  HELLO.C — OS32 最初の外部プログラム                                     */
/*                                                                          */
/*  KernelAPIを通じてカーネル関数を呼び出すテスト。                          */
/*                                                                          */
/*  ビルド (-3s でスタック規約統一):                                          */
/*    wcc386 -s -zl -zls -mf -3s hello.c -fo=hello.obj                     */
/*    wlink FORMAT RAW BIN NAME hello.bin FILE hello.obj                    */
/*          OPTION NODEFAULTLIBS, START=main, OFFSET=0x400000              */
/*                                                                          */
/*  -3s により:                                                             */
/*    - main 関数もスタック規約 (引数は [esp+8] から)                        */
/*    - 関数ポインタ呼び出しもスタック規約 (push → call → add esp)           */
/*    - リンカのエントリシンボルは main (アンダースコアなし)                  */
/* ======================================================================== */

#include "os32api.h"

void main(int argc, char **argv, KernelAPI *api)
{
    /* コンソールにメッセージ表示 */
    api->kprintf(0xA1, "%s", "=== Hello from external program! ===\n");
    api->kprintf(0xE1, "%s", "  KernelAPI magic: ");
    if (api->magic == 0x4B415049UL) {
        api->kprintf(0xC1, "%s", "KAPI OK\n");
    } else {
        api->kprintf(0x41, "%s", "INVALID!\n");
    }
    api->kprintf(0xE1, "%s", "  API version: ");
    if (api->version >= 1) {
        api->kprintf(0xC1, "%d\n", api->version);
    } else {
        api->kprintf(0x41, "%s", "INVALID!\n");
    }
    api->kprintf(0xA1, "%s", "=== Program done. ===\n");
}
