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
/*  算数と判定は kernel/time_math.c (ホストで全分岐を踏む)。ここは           */
/*  スナップショットの採り方だけ。                                          */
/* ======================================================================== */

#include "types.h"
#include "idt.h"
#include "io.h"
#include "sys.h"
#include "time_math.h"
#include "ktime.h"          /* 試験専用の注入口 (kselftest だけが使う) */
#include "os32_kapi_shared.h"   /* OS32_ERR_AGAIN / OS32_ERR_NOSYS */

/* 3 回失敗したら諦める。1 回の読みは数 µs なので、同じ周期の中で境界を
 * 3 回も踏むことはない (踏むなら PIT が止まっているか mode が違う)。 */
#define TIME_RETRY_MAX  3

/* ------------------------------------------------------------------------ */
/*  試験専用の注入口 (往復 8 の中継 1)                                       */
/*                                                                           */
/*  実 PIT では「呼び出しから p1 読みまでに境界を越える」機械があり、位相を  */
/*  待っても p1/p2 の 3 分岐を撃ち分けられない。そこで **入力列 {p1,p2,count}**
 *  を与え、time_branch_hits[] で「どの分岐を踏んだか」を数える。            */
/*  **kselftest だけが触る**。n = 0 (既定) では 1 バイトも影響しない。       */
/* ------------------------------------------------------------------------ */
/* 型は kernel/ktime.h (kselftest が読む)。 */
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
/*  説明は kernel/time_math.h の time_clamp の注記。                         */
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
        u8 c_lo, c_hi;
        int p1, p2;

        /* 1. ラッチ前の IRR bit0 (既定も IRR だが明示する) */
        outp(PIC1_CMD, OCW3_IRR);
        p1 = (inp(PIC1_CMD) & 0x01) ? 1 : 0;

        /* 2. カウンタ#0 をラッチして下位 → 上位 */
        outp(PIT_MODE, PIT_LATCH_TIMER0);
        c_lo = (u8)inp(PIT_CNTR0);
        c_hi = (u8)inp(PIT_CNTR0);
        count = ((unsigned int)c_hi << 8) | (unsigned int)c_lo;

        /* 3. ラッチ後の IRR bit0 */
        outp(PIC1_CMD, OCW3_IRR);
        p2 = (inp(PIC1_CMD) & 0x01) ? 1 : 0;

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
        unsigned long long us = ((unsigned long long)out_hi << 32) |
                                (unsigned long long)out_lo;
        if (time_clamp(us, s_last_us, &us)) ktime_clamp_count++;
        s_last_us = us;
        out_lo = (unsigned int)(us & 0xFFFFFFFFULL);
        out_hi = (unsigned int)(us >> 32);
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
