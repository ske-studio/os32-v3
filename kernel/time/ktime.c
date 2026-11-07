/* Common microsecond clock: saved-IF provider read, monotonic clamp, user output. */

#include "types.h"
#include "clock_source.h"
#include "io.h"
#include "sys.h"
#include "time_math.h"
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
    u32 out_lo = 0, out_hi = 0;
    unsigned int saved = irq_save();
    int rc = platform_clock_read_us(&out_lo, &out_hi);

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
