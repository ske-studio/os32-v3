#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif
/* CRASH.C — 意図的にNULLポインタアクセスしてPAGE FAULT復帰をテスト */



#include "os32api.h"
/* カーネルスタックガードの番地は **正典から引く** ([C4])。2026-09-17 まで
 * 0x1FB000 と直書きしていて、決裁 D1 でガードが 0x2FB000 へ動いたときに
 * 「落ちるはずの試験が素通りする」形で腐るところだった。 */
#include "memmap.h"

void main(int argc, char **argv, KernelAPI *api)
{
    volatile int *guard_page;

    api->kprintf(0x42, "Crash test: writing to guard page (0x%x)...\n",
                 (unsigned)MEM_STACK_GUARD);
    guard_page = (volatile int *)MEM_STACK_GUARD;
    *guard_page = 0xDEAD;

    /* ここには来ないはず */
    api->kprintf(0x42, "%s", "ERROR: should not reach here!\n");
}
