#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif
/* ======================================================================== */
/*  TEST3.C — KernelAPI v3 + PAGE FAULT 復帰テスト                          */
/*                                                                          */
/*  テスト内容:                                                              */
/*    1. kprintf の動作確認 (%d, %u, %x, %s, %c)                            */
/*    2. 意図的なNULLポインタアクセスでPAGE FAULT復帰テスト                  */
/*       (引数 "crash" 付きで実行時のみ)                                    */
/* ======================================================================== */




#include "os32api.h"

#define GREEN  0xA2
#define CYAN   0x62
#define WHITE  0xE1

void main(int argc, char **argv, KernelAPI *api)
{
    api->kprintf(CYAN, "%s", "=== KernelAPI v3 Test ===\n");

    /* バージョン確認 */
    api->kprintf(WHITE, "  version: %d\n", api->version);

    /* kprintf テスト */
    api->kprintf(GREEN, "  decimal: %d, %d\n", 42, -7);
    api->kprintf(GREEN, "  unsigned: %u\n", 12345);
    api->kprintf(GREEN, "  hex: 0x%x\n", 0xDEAD);
    api->kprintf(GREEN, "  string: %s\n", "hello");
    api->kprintf(GREEN, "  char: %c%c%c\n", 'A', 'B', 'C');
    api->kprintf(GREEN, "  percent: 100%%\n");
    api->kprintf(GREEN, "  mixed: %s=%d (0x%x)\n", "val", 255, 255);

    api->kprintf(CYAN, "%s", "=== v3 Test Done ===\n");
}
