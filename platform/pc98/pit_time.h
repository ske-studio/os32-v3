#ifndef PC98_PIT_TIME_H
#define PC98_PIT_TIME_H
/* 判定の結果 */
#define TIME_DECIDE_OK     0   /* out を採用してよい */
#define TIME_DECIDE_RETRY  1   /* 境界のどちら側か不明 → 読み直す */

/* 旧分岐計数の私有 import 互換口。番号・型・保存領域・reset は保持する。
 * 判定経路では増分しない。試験は返り値・tick/count を直接検査する。 */
#define TIME_BR_T0      0   /* p1 = 0, p2 = 0 → t をそのまま */
#define TIME_BR_T1      1   /* p1 = 1        → t + 1 (再ロードがラッチより前) */
#define TIME_BR_RETRY   2   /* p1 = 0, p2 = 1 → どちらか不明 → やり直し */
#define TIME_BR_COUNT   3

extern unsigned int time_branch_hits[TIME_BR_COUNT];

struct time_snapshot {
    unsigned int tick;    /* 採用する tick_count */
    unsigned int count;   /* 採用する PIT ch0 のラッチ値 */
};

/* p1 = ラッチ**前**の PIC1 IRR bit0、p2 = ラッチ**後**の同じビット。
 * t = IF=0 の中で読んだ tick_count。
 *   p1 == 0 && p2 == 0 → t と count をそのまま (境界を踏んでいない)
 *   p1 == 1            → 再ロードはラッチより前 → t + 1、count は新周期のもの
 *   p1 == 0 && p2 == 1 → 再ロードがラッチの前後どちらか不明 → やり直し
 * **`reload` は判定に要らない** (票の擬似引数からは外した。算数側が使う)。 */
int time_decide(int p1, int p2, unsigned int t, unsigned int count,
                struct time_snapshot *out);

/* raw は物理 IRQ の 64bit 数 (lo/hi は各 32bit)。
 * cycles = raw * reload + (reload - count)
 * us = cycles / clk_hz * 1000000 + (cycles % clk_hz) * 1000000 / clk_hz
 * count > reload は経過 0、clk_hz == 0 は us = 0 とする。
 * cycles と us が 64bit に収まる範囲で使う。
 * ホストの unsigned long は 64bit なので、入出力の語は unsigned int にする。 */
void time_us_from64(unsigned int raw_lo, unsigned int raw_hi,
                    unsigned int count, unsigned int reload,
                    unsigned int clk_hz, unsigned int *lo, unsigned int *hi);

void time_branch_reset(void);


#endif
