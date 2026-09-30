/* ======================================================================== */
/*  CPU_CALIBRATE_MATH.C — 校正の「いつ止めるか」と「いくつか」            */
/*                                                                          */
/*  ここには副作用のある行を 1 つも置かない (tick_count も I/O も見ない)。  */
/*  切り出してあるのは、**エミュレータでは踏めない丸め**をホストで試験する  */
/*  ため。NP21/W でも 1 周では 5 tick に届かない (実測 rounds = 16 / 1 周 ≒  */
/*  0.31 tick)。**旧コードの生の elapsed はそこでも 0** で、補正 (0 → 1) が  */
/*  200,000 を入れていた — 真値 640,000 の約 1/3.2。実機 266MHz は 1 周が    */
/*  0.1 tick 未満でもっと深く 0 に落ち、1/7〜1/13 の桁になる。               */
/*    試験: tools/tests/test_cpu_calibrate.py                               */
/*    記録: tools/tests/cpu_calibrate_tdd.md                                */
/* ======================================================================== */

#include "cpu_calibrate_math.h"

int cpu_calibrate_enough(u32 ticks, u32 rounds)
{
    /* **十分な tick を稼げたか**が本来の条件。1 tick では ±100% ぶれる。 */
    if (ticks >= (u32)CALIBRATE_MIN_TICKS) {
        return 1;
    }
    /* 打ち切り。PIT が死んでいれば ticks は永久に 0 なので、これが無いと
     * 起動が戻らない。通常の機械はここに掛からない
     * (266MHz でも 50 周ほどで 5 tick に届く)。 */
    if (rounds >= (u32)CALIBRATE_MAX_ROUNDS) {
        return 1;
    }
    return 0;
}

u32 cpu_calibrate_compute(u32 total_loops, u32 ticks)
{
    u32 lpt;

    /* 打ち切りまで tick が 1 つも進まなかった = 測れていない。
     * **合計を 1 で割らない** — 「1 tick で 4000 万周」という値が入り、
     * cpu_delay_us(us) がその比で回って永久に返らなくなる。 */
    if (ticks == 0) {
        return (u32)CALIBRATE_FALLBACK_LPT;
    }

    lpt = total_loops / ticks;

    /* 極端に小さい値も「測れていない」と見なす (8MHz 相当へ倒す)。 */
    if (lpt < (u32)CALIBRATE_MIN_LPT) {
        return (u32)CALIBRATE_FALLBACK_LPT;
    }
    return lpt;
}
