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
#include "ktime.h"          /* 私有 import の注入口 (E08 段2まで処理を保持) */
#include "os32_kapi_shared.h"   /* OS32_ERR_AGAIN / OS32_ERR_NOSYS */

/* 3 回失敗したら諦める。1 回の読みは数 µs なので、同じ周期の中で境界を
 * 3 回も踏むことはない (踏むなら PIT が止まっているか mode が違う)。 */
/* TIME_RETRY_MAX is shared with the feed declaration in ktime.h. */

/* ------------------------------------------------------------------------ */
/*  試験専用の注入口 (往復 8 の中継 1)                                       */
/*                                                                           */
/*  起動時の呼出しは E08 段1で外した。ホストの旧経路トレースを維持し、     */
/*  注入処理そのものの撤去は段2で行う。n = 0 が既定。                       */
/* ------------------------------------------------------------------------ */
/* 型・保存領域は kernel/time/ktime.h と私有 import の互換口として残す。 */
struct time_test_feed time_test_feed[TIME_RETRY_MAX];
volatile int time_test_feed_n = 0;   /* > 0 でこの列を使う (最後の要素を繰り返す) */

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

        if (time_test_feed_n > 0) {
            int k = (attempt < time_test_feed_n) ? attempt
                                                 : (time_test_feed_n - 1);
            p1 = time_test_feed[k].p1;
            p2 = time_test_feed[k].p2;
            if (time_test_feed[k].count >= 0) {
                count = (unsigned int)time_test_feed[k].count;
            }
        }

        /* 5. 判定 */
        if (time_decide(p1, p2, t, count, &snap) == TIME_DECIDE_OK) {
            time_us_from(snap.tick, snap.count, ps->reload, ps->period_us,
                         &out_lo, &out_hi);
            rc = 0;
            break;
        }
    }

    /* 6. 単調性のクランプ (**同じ IF=0 の中で**。単一の書き手)。
     *    注入中 (time_test_feed_n > 0) は通さない — 境界の count を撃ち分ける
     *    試験は「わざと前回より小さい値」を作るので、そこをクランプすると
     *    補間そのものを観測できなくなる。製品経路 (n = 0) では必ず通る。 */
    if (rc == 0 && time_test_feed_n == 0) {
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
