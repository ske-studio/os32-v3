/* ======================================================================== */
/*  TIME_MATH.C — µs 時計の判定と算数だけ (I/O なし)                        */
/*    → kernel/time_math.h の先頭、tools/tests/time_math_tdd.md             */
/* ======================================================================== */

#include "time_math.h"

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
        time_branch_hits[TIME_BR_T1]++;
        out->tick  = t + 1;
        out->count = count;
        return TIME_DECIDE_OK;
    }
    if (p2) {
        /* ラッチの前後どちらで再ロードが起きたか分からない。
         * **採用しない** (どちらに寄せても最大 1 周期ずれる)。 */
        time_branch_hits[TIME_BR_RETRY]++;
        return TIME_DECIDE_RETRY;
    }
    time_branch_hits[TIME_BR_T0]++;
    out->tick  = t;
    out->count = count;
    return TIME_DECIDE_OK;
}

void time_us_from(unsigned int tick, unsigned int count, unsigned int reload,
                  unsigned int period_us,
                  unsigned int *lo, unsigned int *hi)
{
    unsigned long long us;
    unsigned int frac = 0;

    if (reload != 0) {
        /* mode 2 のカウンタは reload から 1 へ減る。経過ぶんは reload − count。
         * ラッチ値が reload を超えることは無いが、注入試験が境界外を渡しても
         * 巻き込まない (負にならない)。 */
        unsigned int elapsed = (count <= reload) ? (reload - count) : 0;
        frac = (unsigned int)(((unsigned long long)elapsed *
                               (unsigned long long)period_us) / reload);
    }

    /* **ここが 64bit でないと 71 分で戻る。** */
    us = (unsigned long long)tick * (unsigned long long)period_us +
         (unsigned long long)frac;

    if (lo) *lo = (unsigned int)(us & 0xFFFFFFFFULL);
    if (hi) *hi = (unsigned int)(us >> 32);
}

int time_clamp(unsigned long long us, unsigned long long last,
               unsigned long long *out)
{
    /* 逆行だけを押さえる。等しいときは「クランプした」と数えない —
     * 1 周期のあいだに同じ µs を 2 回読むのは正常で、そこまで数えると
     * ktime_clamp_count が「模擬の粗さ」の指標として使えなくなる。 */
    if (us < last) {
        if (out) *out = last;
        return 1;
    }
    if (out) *out = us;
    return 0;
}
