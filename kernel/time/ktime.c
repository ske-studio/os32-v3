/* ======================================================================== */
/*  KTIME.C — µs 時計 sys_time_now (票 TASK_HAL_WIRING §1-5)                */
/*                                                                          */
/*  時間源は tick_count (1-0 の後はどちらのクロックでもちょうど 10ms) と     */
/*  PIT ch0 のラッチ読み。`us = tick × period + 経過ぶん` を 64bit で組む。  */
/*                                                                          */
/*  **難しいのは周期の境界**: ラッチの直前に再ロードが起きると、新しい周期の */
/*  count を古い tick と組ませて時刻が 1 周期ぶん戻る。そこでラッチを        */
/*  PIC1 の IRR bit0 (= IRQ0 が立っているか) で**挟んで読む** (票 §1-5 R8)。 */
/*                                                                          */
/*  契約: この時計が正しいのは **IRQ0 が失われない範囲**、つまりシステム全体 */
/*  で IF=0 の区間が 1 周期 (10ms) 未満のとき。2 回以上の境界を IF=0 で跨ぐと */
/*  tick が 1 つ落ち、この時計も tick_count も 10ms 遅れる — 検出はしない。  */
/*  tick_count の u32 周回 (497 日) も扱わない。                            */
/*                                                                          */
/*  算数と判定は kernel/time/time_math.c (ホストで全分岐を踏む)。ここは           */
/*  スナップショットの採り方だけ。                                          */
/* ======================================================================== */

#include "types.h"
#include "../../arch/x86/kernel/idt.h"
#include "io.h"
#include "../../include/platform_timer.h"
#include "sys.h"
#include "time_math.h"
#include "time_splash_math.h"
#include "ktime.h"          /* 退役した私有 import の互換宣言 */
#include "os32_kapi_shared.h"   /* OS32_ERR_AGAIN / OS32_ERR_NOSYS */

/* 3 回失敗したら諦める。1 回の読みは数 µs なので、同じ周期の中で境界を
 * 3 回も踏むことはない (踏むなら PIT が止まっているか mode が違う)。 */
/* TIME_RETRY_MAX is shared with the feed declaration in ktime.h. */

/* 退役した私有 import の保存領域。get/set/clear は残すが時計は参照しない。 */
struct time_test_feed time_test_feed[TIME_RETRY_MAX];
volatile int time_test_feed_n = 0;

void time_test_feed_clear(void)
{
    time_test_feed_n = 0;
}

/* ------------------------------------------------------------------------ */
/*  単調性の最後の砦 (票 §1-5 追記)                                          */
/*                                                                           */
/*  p1/p2 の挟み込みは 8254 の再ロードと 8259 の IRR bit0 が原子的に動くこと  */
/*  を前提にしている。NP21/W はそこを模擬せず (count は経過サイクルから計算、 */
/*  IRQ0 は別立てのタイマ事象)、実機でも数百 ns の窓がある。すり抜ける形の    */
/*  説明は kernel/time/time_math.h の time_clamp の注記。                         */
/*                                                                           */
/*  **書き手はここだけ** — 更新もクランプもスナップショットと同じ irq_save の */
/*  中で行うので、読み出しの途中で s_last_us が動くことはない。              */
/* ------------------------------------------------------------------------ */
static unsigned long long s_last_us = 0;
u32 ktime_clamp_count = 0;

int sys_time_now(u32 *lo, u32 *hi)
{
    const struct pit_setup *ps = pit_get_setup();
    struct time_snapshot snap;
    unsigned int saved;
    unsigned int out_lo = 0, out_hi = 0;
    int attempt;
    int rc = OS32_ERR_AGAIN;

    /* PIT を積んでいない / mode 2 でない機械では補間の意味が無い。 */
    if (!ps->valid || ps->mode != (u8)PIT_MODE_TIMER0 || ps->reload == 0) {
        return OS32_ERR_NOSYS;
    }

    saved = irq_save();
    for (attempt = 0; attempt < TIME_RETRY_MAX; attempt++) {
        unsigned int count, t;
        struct platform_timer_sample hw;
        int p1, p2;

        platform_timer_sample(&hw);
        p1 = hw.p1;
        count = hw.count;
        p2 = hw.p2;

        /* 4. tick (IF=0 の中なので 1〜3 の間は不変) */
        t = (unsigned int)tick_count;

        /* 5. 判定 */
        if (time_decide(p1, p2, t, count, &snap) == TIME_DECIDE_OK) {
            time_us_from(snap.tick, snap.count, ps->reload, ps->period_us,
                         &out_lo, &out_hi);
            rc = 0;
            break;
        }
    }

    /* 6. 単調性のクランプ (同じ IF=0 の中で。成功時は必ず通す)。 */
    if (rc == 0) {
        unsigned long long us = ((unsigned long long)out_hi << BITS_PER_U32) |
                                (unsigned long long)out_lo;
        if (time_clamp(us, s_last_us, &us)) ktime_clamp_count++;
        s_last_us = us;
        out_lo = (unsigned int)(us & (unsigned long long)U32_MAX);
        out_hi = (unsigned int)(us >> BITS_PER_U32);
    }
    irq_restore(saved);

    /* **負のときは *lo / *hi を触らない** (票 §1-5)。書くのは IF=1 に戻して
     * から — ユーザ番地は非 present があり得て、IF=0 の #PF は避ける。 */
    if (rc == 0) {
        if (lo) *lo = (u32)out_lo;
        if (hi) *hi = (u32)out_hi;
    }
    return rc;
}

/* Splash alone may hold IF=0 across multiple PIT periods. The caller owns
 * IF and the original deadline; these functions neither restore IF nor call
 * tick_dispatch. One entry/exit pair is consumed by one settle attempt. */
static struct {
    struct TimeSplashSample start, end;
    u32 tick;
    unsigned int samples;
} s_splash;

/* All bounds are inclusive PIT input cycles, rounded outward (not reload
 * periods). P2a supplies target-CPU bounds, P2b replaces them after M2 and
 * remeasures the unchanged machine code. Zero means UNSET, never free.
 * No clock family is enabled by P2. */
struct splash_cost_range { u32 min, max; };
struct splash_exit_cost {
    u32 reload, budget, loop_min;
    /* tail: adopted latch -> sample return; enter: that return -> wait.
     * loop: wait bookkeeping -> next sample call; retry: one rejected
     * IRR/latch/IRR attempt including its retry branch; head: call -> latch.
     * tail/enter include caller/settle checks and the bound calculations. */
    struct splash_cost_range tail, enter, loop, retry, head;
};
static const struct splash_exit_cost s_splash_cost[] = {
    { .reload = SYSCLK_1997 / PIT_HZ },
    { .reload = SYSCLK_2458 / PIT_HZ }
};

/* No state is published on failure. Retry only IRR disagreement; zero and
 * counts outside the programmed range are hardware failures, not estimates.
 * retries reports the rejected attempts before the adopted latch, so waiting
 * can account for their independent costs without using a PIT difference. */
static int splash_sample(u32 reload, struct TimeSplashSample *out,
                         unsigned int *retries)
{
    unsigned int tries;
    for (tries = 0; tries < TIME_RETRY_MAX; tries++) {
        struct platform_timer_sample raw;
        platform_timer_sample(&raw);
        if (raw.count == 0 || raw.count > reload)
            return -1;
        if (raw.p1 != raw.p2)
            continue;
        out->count = raw.count;
        out->pending = (u32)raw.p2;
        *retries = tries;
        return 0;
    }
    return -1;
}

int ktime_splash_sample(struct TimeSplashSample *sample)
{
    const struct pit_setup *ps = pit_get_setup();
    struct TimeSplashSample value;
    unsigned int retries;
    u32 tick = tick_count;

    if (!sample || !ps->valid || ps->mode != (u8)PIT_MODE_TIMER0 ||
        ps->reload == 0 || s_splash.samples >= 2 ||
        (s_splash.samples && s_splash.tick != tick) ||
        splash_sample(ps->reload, &value, &retries) != 0 ||
        tick_count != tick) {
        s_splash.samples = 0;
        return -1;
    }
    if (s_splash.samples == 0) {
        s_splash.start = value;
        s_splash.tick = tick;
    } else {
        s_splash.end = value;
    }
    s_splash.samples++;
    *sample = value;
    return 0;
}

int ktime_splash_settle(const struct TimeSplashMathInput *input)
{
    const struct pit_setup *ps = pit_get_setup();
    const struct splash_exit_cost *cost = NULL;
    struct TimeSplashMathInput local;
    struct TimeSplashMathResult result;
    unsigned int samples = s_splash.samples;
    u32 tick = s_splash.tick;

    /* Consume even a failed attempt: it must never apply a correction twice. */
    s_splash.samples = 0;
    for (unsigned int i = 0; i < sizeof(s_splash_cost) / sizeof(s_splash_cost[0]); i++) {
        if (s_splash_cost[i].reload == ps->reload)
            cost = &s_splash_cost[i];
    }
    /* This guard precedes every division, pure-function call and tick write.
     * B also covers the no-wait path through saved-IF restore/IRQ0 acceptance. */
    if (!cost || cost->budget == 0)
        return -1;
    if (!input || !ps->valid || ps->mode != (u8)PIT_MODE_TIMER0 ||
        input->reload != ps->reload || samples != 2 || tick_count != tick ||
        input->start.count != s_splash.start.count ||
        input->start.pending != s_splash.start.pending ||
        input->end.count != s_splash.end.count ||
        input->end.pending != s_splash.end.pending ||
        cost->budget >= ps->reload || cost->loop_min == 0)
        return -1;
    local = *input;

    if (local.end.count <= cost->budget) {
        struct TimeSplashSample next;
        u32 limit = cost->budget / cost->loop_min + 1;
        unsigned long long step_max, wait_max, lag_max, a, b;
        unsigned int iteration, retries;

        /* Twait bounds ALL K iterations including failed IRR trials. S is
         * the worst delay from a boundary just after one adopted latch to
         * the following stable latch. Neither is inferred from PIT deltas. */
        step_max = (unsigned long long)cost->loop.max +
                   (TIME_RETRY_MAX - 1) * (unsigned long long)cost->retry.max +
                   cost->head.max + cost->tail.max;
        wait_max = cost->tail.max + (unsigned long long)cost->enter.max +
                   limit * step_max;
        lag_max = cost->tail.max + (unsigned long long)cost->enter.max + step_max;
        if (wait_max >= ps->reload || lag_max + cost->budget >= ps->reload)
            return -1;
        a = cost->tail.min + (unsigned long long)cost->enter.min;
        b = cost->tail.max + (unsigned long long)cost->enter.max;
        for (iteration = 0; iteration < limit; iteration++) {
            if (splash_sample(ps->reload, &next, &retries) != 0)
                return -1;
            a += cost->loop.min + (unsigned long long)retries * cost->retry.min +
                 cost->head.min;
            b += cost->loop.max + (unsigned long long)retries * cost->retry.max +
                 cost->head.max;
            /* Wait < R makes this a boundary crossing even if pending was
             * already set. Only this first crossing may replace the exit. */
            if (next.count > local.end.count)
                break;
            a += cost->tail.min;
            b += cost->tail.max;
        }
        if (iteration == limit || next.count <= cost->budget ||
            local.elapsed_min > ~0ULL - a || local.elapsed_max > ~0ULL - b)
            return -1;
        local.end = next;
        local.elapsed_min += a;
        local.elapsed_max += b;
    }

    /* Under IF=0 pending cannot clear, and any wrap must leave IRQ0 pending.
     * Recompute from the ORIGINAL entry, never add an old exit correction. */
    if (time_splash_math(&local, &result) != 0 ||
        local.end.pending != (u32)(local.start.pending || result.wraps) ||
        result.tick_adjust < 0 || result.tick_adjust > (long long)U32_MAX ||
        tick_count != tick)
        return -1;
    tick_count += (u32)result.tick_adjust;
    return 0;
}
