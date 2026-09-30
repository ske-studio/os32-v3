/* ======================================================================== */
/*  HEAP_TEST.C — 子プロセス帯レイアウトの検証 (2026-09-04)                  */
/*                                                                          */
/*  本体の固定 1MB 上限撤廃後のレイアウトを実機で確かめる:                   */
/*    - sbrk_heap_limit (= guard_a) と exec_heap の位置を表示               */
/*    - newlib malloc と KAPI mem_alloc をそれぞれ 64KB ずつ限界まで取り、    */
/*      合計を表示。両ヒープが重ならないことは、確保したブロックへ書いた     */
/*      印を最後に読み返して確かめる。                                       */
/* ======================================================================== */

#include "os32api.h"
#include <stdlib.h>

#define CHUNK   (64u * 1024u)
#define MAXN    128            /* 最大 8MB 分 */

/* crt0.asm の入口。sdk/link/app.ld が .text.startup を先頭に置くので、
 * この番地がそのままロードアドレス (= カーネルの MEM_EXEC_LOAD_ADDR)。
 * 定数を書かずにリンク結果を表示するので、帯域が動いてもここは腐らない。 */
extern char _start[];

void main(int argc, char **argv, KernelAPI *api)
{
    static void *mb[MAXN];
    static void *kb[MAXN];
    int nm = 0, nk = 0, i, bad = 0;
    char *probe;

    (void)argc; (void)argv;

    api->kprintf(0xE1, "load                      = 0x%x\n", (unsigned)_start);
    api->kprintf(0xE1, "sbrk_heap_limit (guard_a) = 0x%x\n", api->sbrk_heap_limit);
    probe = (char *)malloc(16);
    api->kprintf(0xE1, "first malloc block         = 0x%x\n", (unsigned)probe);

    while (nm < MAXN) {
        mb[nm] = malloc(CHUNK);
        if (!mb[nm]) break;
        ((unsigned *)mb[nm])[0] = 0x4D000000u | (unsigned)nm;   /* 'M' + index */
        nm++;
    }
    while (nk < MAXN) {
        kb[nk] = api->mem_alloc(CHUNK);
        if (!kb[nk]) break;
        ((unsigned *)kb[nk])[0] = 0x4B000000u | (unsigned)nk;   /* 'K' + index */
        nk++;
    }
    for (i = 0; i < nm; i++) {
        if (((unsigned *)mb[i])[0] != (0x4D000000u | (unsigned)i)) bad++;
    }
    for (i = 0; i < nk; i++) {
        if (((unsigned *)kb[i])[0] != (0x4B000000u | (unsigned)i)) bad++;
    }

    api->kprintf(0xE1, "malloc   : %d x 64KB = %d KB (last block 0x%x)\n",
                 nm, nm * 64, nm ? (unsigned)mb[nm - 1] : 0);
    api->kprintf(0xE1, "mem_alloc: %d x 64KB = %d KB (first 0x%x, last 0x%x)\n",
                 nk, nk * 64, nk ? (unsigned)kb[0] : 0, nk ? (unsigned)kb[nk - 1] : 0);
    api->kprintf(bad ? 0x41 : 0xC1, "overlap check: %s (%d corrupted)\n",
                 bad ? "FAIL" : "OK", bad);

    for (i = 0; i < nk; i++) api->mem_free(kb[i]);
    for (i = 0; i < nm; i++) free(mb[i]);
    free(probe);
    api->kprintf(0xC1, "%s", "heap_test done\n");
}
