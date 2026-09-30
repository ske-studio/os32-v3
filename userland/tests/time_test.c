/* ======================================================================== */
/*  TIME_TEST.C — µs 時計 sys_time_now の CPL=3 受入 (KAPI v59)              */
/*                                                                          */
/*  票: docs/tasks/v3/TASK_HAL_WIRING.md §1-5 / 受入 W4                     */
/*                                                                          */
/*  カーネル側 (kernel/kselftest.c の test_time_now) は CPL=0 から呼ぶので、  */
/*  **CPL=3 のポインタ契約は踏めない** — `ring3_user_range_ok` も            */
/*  `ring3_user_range_writable` も `ring3_in_syscall` が 0 のあいだは素通し    */
/*  するからで、int 0x80 を通った本物の呼び出しはここでしか作れない。         */
/*                                                                           */
/*  見るもの (`time_test` だけで走るのは 1〜5。6 は壊すので明示的に頼む):     */
/*    1. 正常系 — **非恒等写像のアプリスタック**へ 64 ビットが揃って返る      */
/*    2. 単調性 — 連続読みで逆行しない (上位語も含めて比べる)                 */
/*    3. NULL — lo / hi のどちらが NULL でも負、出力は不変                    */
/*    4. 交差 — 2 本の 4 バイト範囲が重なる (差 0〜3) なら負。差 4 は通る      */
/*    5. ヒープ — malloc した番地 (これも非恒等写像) へ書ける                 */
/*    6. `time_test ro` — **読み取り専用の USER ページ**を出力に渡す。        */
/*       OS32 は CR0.WP = 0 なので、カーネルが止めなければ #PF も起きずに     */
/*       書けてしまう (Codex 往復 10)。**期待は「アプリが kill される」**で、 */
/*       この行の後に何も出なければ合格。出力が続いたら不合格。              */
/*                                                                           */
/*  ⚠ 未実装 (PM の NP21/W 側で見る): 「4 バイトが帯の境界を跨ぐ」と          */
/*  「共有ライブラリの .text を出力に渡す」は、アプリ側から番地を安全に       */
/*  作れない (帯の定数はカーネル側にしか無く、shlib は attach した GUI アプリ */
/*  でないと .text を持たない)。前者は `ring3_user_range_ok` が、後者は      */
/*  `ring3_user_range_writable` の PTE 検査が受け持つ。                      */
/* ======================================================================== */

#include "os32api.h"
#include <stdlib.h>          /* newlib malloc — アプリのヒープ (非恒等写像) */

#define TIME_READS 2000

void main(int argc, char **argv, KernelAPI *api)
{
    u32 lo = 0, hi = 0;          /* アプリのスタック (非恒等写像) */
    u32 plo, phi;
    u32 *heap;
    int rc, i, fails = 0, back = 0;

    if (api->version < 59) {
        api->kprintf(0x41, "KAPI v%d < 59: sys_time_now absent\n", api->version);
        return;
    }

    /* --- 1. 正常系 --- */
    rc = api->sys_time_now(&lo, &hi);
    api->kprintf(rc == 0 ? 0xE1 : 0x41,
                 "1 stack out: rc=%d lo=%u hi=%u\n", rc, lo, hi);
    if (rc != 0) fails++;

    /* --- 2. 単調性 --- */
    plo = lo; phi = hi;
    for (i = 0; i < TIME_READS; i++) {
        if (api->sys_time_now(&lo, &hi) != 0) { back = -1; break; }
        if (hi < phi || (hi == phi && lo < plo)) { back = 1; break; }
        plo = lo; phi = hi;
    }
    api->kprintf(back == 0 ? 0xE1 : 0x41,
                 "2 monotonic over %d reads: %s\n", TIME_READS,
                 back == 0 ? "no going back" : "**WENT BACKWARDS**");
    if (back != 0) fails++;

    /* --- 3. NULL は負、出力は不変 --- */
    lo = 0x11111111UL; hi = 0x22222222UL;
    rc = api->sys_time_now(&lo, (u32 *)0);
    if (rc >= 0 || lo != 0x11111111UL) fails++;
    api->kprintf(rc < 0 && lo == 0x11111111UL ? 0xE1 : 0x41,
                 "3a hi=NULL: rc=%d lo unchanged=%d\n",
                 rc, lo == 0x11111111UL);
    rc = api->sys_time_now((u32 *)0, &hi);
    if (rc >= 0 || hi != 0x22222222UL) fails++;
    api->kprintf(rc < 0 && hi == 0x22222222UL ? 0xE1 : 0x41,
                 "3b lo=NULL: rc=%d hi unchanged=%d\n",
                 rc, hi == 0x22222222UL);

    /* --- 4. 2 本の範囲が交差する --- */
    {
        /* 8 バイトぶんの場所を取り、lo と hi の距離を 0〜4 で変える。
         * 差が 0〜3 なら 4 バイトずつの範囲が重なり、「上下が同じ
         * スナップショット」という約束が壊れる。 */
        static u8 pad[16];
        u32 *base = (u32 *)pad;
        int d;
        for (d = 0; d <= 4; d++) {
            u32 *a = base;
            u32 *b = (u32 *)((u8 *)base + d);
            int want_ok = (d >= 4);
            rc = api->sys_time_now(a, b);
            if ((rc == 0) != want_ok) fails++;
            api->kprintf((rc == 0) == want_ok ? 0xE1 : 0x41,
                         "4 gap=%d: rc=%d (expect %s)\n",
                         d, rc, want_ok ? "0" : "negative");
        }
    }

    /* --- 5. ヒープ (これも非恒等写像) --- */
    heap = (u32 *)malloc(2 * sizeof(u32));
    if (!heap) {
        api->kprintf(0x41, "5 heap: malloc failed\n");
        fails++;
    } else {
        heap[0] = 0; heap[1] = 0;
        rc = api->sys_time_now(&heap[0], &heap[1]);
        if (rc != 0) fails++;
        api->kprintf(rc == 0 ? 0xE1 : 0x41,
                     "5 heap out: rc=%d lo=%u hi=%u\n", rc, heap[0], heap[1]);
        free(heap);
    }

    api->kprintf(fails ? 0x41 : 0xC1, "TIME %s (%d failure(s))\n",
                 fails ? "FAIL" : "PASS", fails);

    /* --- 6. 読み取り専用の USER ページ (明示的に頼んだときだけ) --- */
    if (argc > 1 && argv[1][0] == 'r' && argv[1][1] == 'o') {
        /* このプログラム自身の .text。**期待は kill** — この後の行が
         * 出たら、カーネルが読み取り専用の USER ページへの書き込みを
         * 止めていない (CR0.WP = 0 なのでハードウェアも止めない)。
         * 共有ライブラリの .text を渡す版は GUI を attach した試験が要る。 */
        u32 *code = (u32 *)(void *)main;
        api->kprintf(0xE1, "6 passing own .text (%p) as lo: expect kill\n",
                     (void *)code);
        rc = api->sys_time_now(code, &lo);
        api->kprintf(0x41, "6 **NOT KILLED**: rc=%d (page was writable)\n", rc);
    }
}
