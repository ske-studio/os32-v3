#include "pit_time.h"
#include "types.h"

/* 退役した私有 import の保存領域。判定経路では増分しない。 */
unsigned int time_branch_hits[TIME_BR_COUNT];

void time_branch_reset(void)
{
    int i;
    for (i = 0; i < TIME_BR_COUNT; i++) time_branch_hits[i] = 0;
}

int time_decide(int p1, int p2, unsigned int t, unsigned int count,
                struct time_snapshot *out)
{
    if (out == 0) return TIME_DECIDE_RETRY;

    if (p1) {
        /* 再ロードはラッチ**より前**に済んでいる。count は新しい周期のもの
         * なので、古い tick と組ませると時刻が 1 周期ぶん戻る。 */
        out->tick  = t + 1;
        out->count = count;
        return TIME_DECIDE_OK;
    }
    if (p2) {
        /* ラッチの前後どちらで再ロードが起きたか分からない。
         * **採用しない** (どちらに寄せても最大 1 周期ずれる)。 */
        return TIME_DECIDE_RETRY;
    }
    out->tick  = t;
    out->count = count;
    return TIME_DECIDE_OK;
}

void time_us_from64(unsigned int raw_lo, unsigned int raw_hi,
                    unsigned int count, unsigned int reload,
                    unsigned int clk_hz, unsigned int *lo, unsigned int *hi)
{
    unsigned long long raw = ((unsigned long long)raw_hi << BITS_PER_U32) | raw_lo;
    unsigned int elapsed = (count <= reload) ? (reload - count) : 0;
    unsigned long long cycles = raw * reload + elapsed;
    unsigned long long us = 0;

    /* 商と剰余に分け、cycles * 10^6 の中間オーバーフローを避ける。
     * 64bit 除算は lib/divdi3.c の自前 helper で解く。 */
    if (clk_hz != 0) {
        us = (cycles / clk_hz) * 1000000ULL +
             (cycles % clk_hz) * 1000000ULL / clk_hz;
    }
    if (lo) *lo = (unsigned int)(us & (unsigned long long)U32_MAX);
    if (hi) *hi = (unsigned int)(us >> BITS_PER_U32);
}
