/* ======================================================================== */
/*  KTIME.H — µs 時計の**試験専用の注入口** (票 TASK_HAL_WIRING §1-5)       */
/*                                                                          */
/*  時計そのもの (`sys_time_now`) の宣言は include/sys.h。                   */
/*  E08 段1で起動時の注入試験はホストへ移した。ここは私有 import の互換口。  */
/*  段2までは注入処理も有効で、test_dep_dir_b_timer.py が旧経路を検査する。 */
/*  n = 0 が既定。型・保存領域・get/set/clear の実体は今後も保持する。       */
/* ======================================================================== */

#ifndef __KTIME_H
#define __KTIME_H

/* Three failed snapshots exhaust one call; shared with the injection array. */
#define TIME_RETRY_MAX 3

struct time_test_feed {
    int p1;      /* 0 / 1 */
    int p2;      /* 0 / 1 */
    int count;   /* < 0 = 実測のラッチ値をそのまま使う */
};

/* 試行 i は feed[min(i, n-1)] を使う (最後の要素を繰り返す)。
 * n = 0 (既定) なら注入しない。3 = sys_time_now の再試行上限。 */
/* Import ABI hashes this declaration spelling; the C definition uses
 * TIME_RETRY_MAX and the compiler checks that both array bounds agree. */
extern struct time_test_feed time_test_feed[3];
extern volatile int time_test_feed_n;

void time_test_feed_clear(void);

/* ------------------------------------------------------------------------ */
/*  単調性クランプの回数 (**製品の観測点**。試験専用ではない)                */
/*                                                                           */
/*  `sys_time_now` が「前回より小さい値」を押さえた回数。0 でないのは         */
/*  8254 の再ロードと 8259 の IRR が原子的でない機械 (NP21/W は模擬しない)。  */
/*  kselftest が 1 万回読みの後に表示する。実機での回数は W4 の記録項目。     */
/*  型は u32 (`unsigned long`) — カーネル内だけで読む。                       */
extern u32 ktime_clamp_count;

#endif /* __KTIME_H */
