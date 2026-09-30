/* ======================================================================== */
/*  TIME_MATH.H — µs 時計の「判定」と「算数」だけ (票 §1-5)                 */
/*                                                                          */
/*  PIT も PIC も触らない。スナップショットを採る側 (kernel/ktime.c) から    */
/*  切り出してあるのは、**p1/p2 の 3 分岐と 71 分の桁あふれがホストでしか   */
/*  確実に踏めない**から。実 PIT では「呼び出しから p1 読みまでに境界を      */
/*  越える」機械があり、位相を待っても 0/0 と 0/1 を撃ち分けられない        */
/*  (往復 8 の中継 1)。                                                     */
/*    試験: tools/tests/test_time_math.py                                   */
/*    記録: tools/tests/time_math_tdd.md                                    */
/* ======================================================================== */

#ifndef TIME_MATH_H
#define TIME_MATH_H

/* 判定の結果 */
#define TIME_DECIDE_OK     0   /* out を採用してよい */
#define TIME_DECIDE_RETRY  1   /* 境界のどちら側か不明 → 読み直す */

/* 踏んだ分岐 (kselftest / ホスト試験が「全部踏めたか」を見る観測点)。
 * W4 は位相待ちでは網羅できないので、注入した入力列で 3 分岐を数える。 */
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

/* us = tick × period_us + (reload − count) × period_us ÷ reload。
 * **tick との積は 64bit で行う** — u32 だと 71 分で桁あふれる
 * (429496 tick × 10000µs = 4.29×10^9 > 2^32-1)。
 *
 * 出力は **`unsigned int`** で受ける (どの環境でも 32bit)。カーネルの `u32` は
 * `unsigned long` だが、ホストではそれが 64bit になり「上下に割る」という
 * 試験そのものが素通しになる — pit_math と同じ理由 (pit_clock_tdd.md 末尾)。 */
void time_us_from(unsigned int tick, unsigned int count, unsigned int reload,
                  unsigned int period_us,
                  unsigned int *lo, unsigned int *hi);

void time_branch_reset(void);

/* ------------------------------------------------------------------------ */
/*  単調性のクランプ (票 §1-5 追記)                                          */
/*                                                                           */
/*  p1/p2 の挟み込みは「8254 の再ロード」と「8259 の IRR bit0」が原子的に     */
/*  動くことを前提にしている。**NP21/W はそこを模擬しない** — PIT の count は */
/*  経過サイクルから計算され、IRQ0 は別立てのタイマ事象で上がるので、         */
/*    (a) count は新周期に戻っているのに IRR がまだ立っていない → p1=p2=0 で  */
/*        古い tick と小さな count が組み、**前回より小さい値**になる         */
/*    (b) IRR が先に立って p1=1 (t+1) なのに count は旧周期の終わり →         */
/*        いったん (t+2) 相当まで跳び、ISR が走った後の読みで戻る             */
/*  の 2 つが p1/p2 の検査をすり抜ける。実機でも数百 ns の窓で同じ形になる。   */
/*                                                                           */
/*  そこで最後に**前回値でクランプ**する。us < last なら last を返す          */
/*  (戻り 1 = クランプした。呼び手が回数を数える)。`us == last` は            */
/*  **クランプではない** — 同じ µs を 2 回読むのは正常なので数えない。        */
/*  `out` が NULL なら書かないだけ。                                          */
int time_clamp(unsigned long long us, unsigned long long last,
               unsigned long long *out);

#endif /* TIME_MATH_H */
