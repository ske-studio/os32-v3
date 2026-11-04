/* ======================================================================== */
/*  KTIME.H — µs 時計の退役した私有 import (票 TASK_HAL_WIRING §1-5)       */
/*                                                                          */
/*  時計そのもの (`sys_time_now`) の宣言は include/sys.h。                   */
/*  起動時の注入試験はホストへ移設済み。ここは私有 import の互換口。       */
/*  型・保存領域・get/set/clear は残すが、値を書いても時計には影響しない。  */
/* ======================================================================== */

#ifndef __KTIME_H
#define __KTIME_H

/* Three failed snapshots exhaust one call; shared with the retained array. */
#define TIME_RETRY_MAX 3

struct time_test_feed {
    int p1;      /* 0 / 1 */
    int p2;      /* 0 / 1 */
    int count;   /* 旧注入値。現在は時計に使わない */
};

/* 保存領域だけを保持する。clear は n を 0 に戻す。 */
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
