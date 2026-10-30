/* ------------------------------------------------------------------------ */
/*  W4: µs 時計 (票 TASK_HAL_WIRING §1-5)                                   */
/*                                                                          */
/*  三分岐の網羅は**入力列の注入**で行う (往復 8 の中継 1): 実 PIT では      */
/*  呼び出しから p1 読みまでに境界を越える機械があり、位相を待っても         */
/*  0/0 と 0/1 を撃ち分けられない。実 PIT 側は「残り 1〜2 count を見てから   */
/*  読む」を期限つきで試し、**踏めた分岐を記録して、踏めなかったものは       */
/*  未検証と報告する** (失敗にしない)。                                      */
/* ------------------------------------------------------------------------ */
#define KSEL_TIME_READS      10000
#define KSEL_TIME_PHASE_TICKS 100    /* 位相待ちの期限 */
#define KSEL_TIME_PHASE_POLL  1000   /* IF=0 の観測ループの上限 */

static void ksel_time_feed(int n, int p1a, int p2a, int p1b, int p2b)
{
    time_test_feed[0].p1 = p1a; time_test_feed[0].p2 = p2a;
    time_test_feed[0].count = -1;
    time_test_feed[1].p1 = p1b; time_test_feed[1].p2 = p2b;
    time_test_feed[1].count = -1;
    time_test_feed[2] = time_test_feed[1];
    kdi_set_time_test_feed_n(n);
}

static void test_time_now(void)
{
    const struct pit_setup *ps = pit_get_setup();
    u32 lo = 0, hi = 0, plo = 0, phi = 0;
    u32 t0lo = 0, t0hi = 0, t1lo = 0, t1hi = 0;
    u32 clamp0;
    unsigned int hit_t0, hit_t1, hit_retry;
    int back = 0;
    int rc, i;

    /* --- 素の読み --- */
    rc = sys_time_now(&lo, &hi);
    check(rc == 0, "time:first read ok");
    check(hi == 0, "time:hi=0 at boot");
    check(lo > 0 || tick_count == 0, "time:us advance");

    /* --- **1 万回連続で逆行しない** ---
     * 単調性を持たせているのは p1/p2 の判定**だけではない**。8254 の再ロードと
     * 8259 の IRR が原子的でない機械 (NP21/W は模擬しない。実機も数百 ns の窓が
     * ある) では判定表を正しく通しても前回より小さい値が出るので、
     * `sys_time_now` が最後に前回値でクランプする。押さえた回数はその場で
     * 表示する — 0 なら「この機械では p1/p2 だけで足りていた」の記録になる。 */
    clamp0 = ktime_clamp_count;
    plo = lo; phi = hi;
    for (i = 0; i < KSEL_TIME_READS; i++) {
        if (sys_time_now(&lo, &hi) != 0) { back = -1; break; }
        if (hi < phi || (hi == phi && lo < plo)) { back++; break; }
        plo = lo; phi = hi;
    }
    check(back == 0, "time:10k reads monotonic");
    clamp0 = ktime_clamp_count - clamp0;
    kprintf(KLOG_ATTR, "[selftest] time: clamped %u of %d reads\n",
            (unsigned int)clamp0, (int)KSEL_TIME_READS);
    /* 全部がクランプなら時計が止まっている (補間が効いていない)。 */
    check(clamp0 < (u32)KSEL_TIME_READS, "time:clamp count sane");

    /* --- 既知の待ちを挟む (校正済み delay と突き合わせる) --- */
    check(sys_time_now(&t0lo, &t0hi) == 0, "time:bracket b4");
    cpu_delay_us(1000);
    check(sys_time_now(&t1lo, &t1hi) == 0, "time:bracket after");
    /* 1ms の待ちが 0.2ms〜20ms に見えれば配線は正しい (校正の精度そのものは
     * cpu_calibrate の試験が持つ。ここは「時計が進むか」だけ)。 */
    check(t1hi == t0hi && t1lo > t0lo + 200 && t1lo < t0lo + 20000,
          "time:1ms delay plausible");
    /* 外れたら測った値を出す (2026-09-24 の NP21/W で 1 回だけ FAIL。
     * cpu_delay_us の校正がずれたのか、時計の補間がずれたのかを分ける)。 */
    if (!(t1hi == t0hi && t1lo > t0lo + 200 && t1lo < t0lo + 20000)) {
        kprintf(KLOG_ATTR, "[selftest] time: 1ms delay measured %u us (hi %u->%u)\n",
                (unsigned int)(t1lo - t0lo), (unsigned int)t0hi,
                (unsigned int)t1hi);
    }

    /* --- 注入: p1/p2 の三分岐を全部踏む --- */
    time_branch_reset();
    ksel_time_feed(1, 0, 0, 0, 0);            /* 両方 0 → t をそのまま */
    check(sys_time_now(&lo, &hi) == 0, "time:p1=0 p2=0 ok");
    ksel_time_feed(1, 1, 0, 1, 0);            /* p1 = 1 → t + 1 */
    check(sys_time_now(&lo, &hi) == 0, "time:p1=1 -> t+1");
    ksel_time_feed(2, 0, 1, 0, 0);            /* 1 回やり直して成功 */
    check(sys_time_now(&lo, &hi) == 0, "time:0/1 retry ok");
    /* **3 回とも 0/1 を作る** — p2 だけ強制すると境界後の p1=1 で成功し得る */
    ksel_time_feed(1, 0, 1, 0, 1);
    check(sys_time_now(&lo, &hi) == OS32_ERR_AGAIN, "time:0/1 x3=EAGAIN");
    /* 負のときは出力を触らない。2 回目の -EAGAIN は同じ入力列の同じ経路
     * なので、返り値と「出力が不変」を 1 本で見る。 */
    lo = 0xDEADBEEFUL; hi = 0xFEEDFACEUL;
    check(sys_time_now(&lo, &hi) == OS32_ERR_AGAIN &&
          lo == 0xDEADBEEFUL && hi == 0xFEEDFACEUL, "time:neg keeps out");
    time_test_feed_clear();
    check(time_branch_hits[TIME_BR_T0] >= 1 &&
          time_branch_hits[TIME_BR_T1] >= 1 &&
          time_branch_hits[TIME_BR_RETRY] >= 4,
          "time:3 branches hit");

    /* --- 注入: 周期の境界の count ---
     * **2 本の読みは同じ irq_save の中で**。あいだに tick 境界が入ると
     * 「はじめ」の側が 1 周期ぶん大きくなり、補間の比較が偶発的に落ちる
     * (10ms に数 µs の窓。踏むまで気づけない形なので先に塞ぐ)。
     * 2 本とも同じ注入経路なので、成功の判定は 1 本にまとめる。
     * 注入中はクランプを通らない (kernel/time/ktime.c) ので、
     * 「終わり」→「はじめ」の巻き戻りがそのまま観測できる。 */
    time_test_feed[0].p1 = 0; time_test_feed[0].p2 = 0;
    time_test_feed[0].count = 1;              /* 周期の終わり */
    kdi_set_time_test_feed_n(1);
    {
        unsigned int saved = irq_save();
        rc = sys_time_now(&t0lo, &t0hi);
        time_test_feed[0].count = (int)ps->reload;  /* 周期のはじめ */
        if (sys_time_now(&t1lo, &t1hi) != 0) rc = -1;
        irq_restore(saved);
    }
    check(rc == 0, "time:count=1/reload ok");
    /* 同じ tick なら「終わり」のほうが大きい (補間が効いている) */
    check(t0lo > t1lo || t0hi > t1hi || tick_count == 0,
          "time:frac moves");
    time_test_feed_clear();

    /* --- 実 PIT での位相待ち (**IF=1 で待つ**。IF=0 では IRQ0 が止まり
     * tick の期限が進まない。往復 9 の中継 3) --- */
    time_branch_reset();
    {
        u32 deadline = tick_count + KSEL_TIME_PHASE_TICKS;
        int done = 0;
        while (tick_count < deadline && !done) {
            unsigned int saved = irq_save();
            int poll;
            for (poll = 0; poll < KSEL_TIME_PHASE_POLL; poll++) {
                u32 c;
                outp(PIT_MODE, PIT_LATCH_TIMER0);
                c = (u32)(u8)inp(PIT_CNTR0);
                c |= (u32)((u8)inp(PIT_CNTR0)) << 8;
                if (c <= 2) {                  /* 残り 1〜2 count */
                    (void)sys_time_now(&lo, &hi);
                    done = 1;
                    break;
                }
            }
            irq_restore(saved);
        }
    }
    hit_t0    = time_branch_hits[TIME_BR_T0];
    hit_t1    = time_branch_hits[TIME_BR_T1];
    hit_retry = time_branch_hits[TIME_BR_RETRY];
    /* **踏めなかった分岐は失敗にしない** — 網羅は注入が持つ。 */
    if (hit_t0 == 0 || hit_t1 == 0 || hit_retry == 0) {
        /* 踏めなかった分岐は実機では UNVERIFIED。網羅は注入の表が持つ。 */
        kprintf(KLOG_ATTR, "[selftest] time: real-PIT t0=%d t1=%d retry=%d"
                      " (unhit = UNVERIFIED)\n",
                (int)hit_t0, (int)hit_t1, (int)hit_retry);
    }
    /* 位相窓を 1 度も捕まえられなくても失敗にしない (実装レビュー往復 1 の
     * 非 blocker 3: 正常な時計でも狭い窓は取り逃がし得る。網羅は注入の表)。 */
    if (hit_t0 + hit_t1 + hit_retry == 0) {
        kprintf(KLOG_ATTR, "[selftest] time: real-PIT phase window not caught (UNVERIFIED)\n");
    }
    time_branch_reset();

    /* --- CPL=0 の直呼びは書き込み検査の対象外 (Approve 後の注意 2) ---
     * 常駐シェル / gshell はカーネル帯のローカル変数を渡す。`ring3_in_syscall`
     * が 0 のあいだは素通しでなければ、それらが全部落ちる。 */
    check(ring3_user_range_writable((u32)&lo, sizeof(u32)) == 1,
          "time:CPL0 wr check off");
    check(ring3_user_ranges_writable((u32)&lo, sizeof(u32),
                                     (u32)&hi, sizeof(u32)) == 1,
          "time:CPL0 2-range same");

    /* --- 呼び出しコストと IF=0 の最長区間 (Approve 後の注意 3) ---
     * この時計の IF=0 区間は sys_time_now 1 回ぶんそのもの。1000 回を
     * 挟んで測り、1 回あたりの µs を報告する (合否にはしない — 機械差が
     * そのまま出る)。 */
    if (sys_time_now(&t0lo, &t0hi) == 0) {
        for (i = 0; i < 1000; i++) (void)sys_time_now(&lo, &hi);
        if (sys_time_now(&t1lo, &t1hi) == 0 && t1hi == t0hi && t1lo > t0lo) {
            /* 1 回あたりの µs = この時計の IF=0 区間そのもの。 */
            kprintf(KLOG_ATTR, "[selftest] time: 1000 reads %u us (%u us/call)\n",
                    (unsigned int)(t1lo - t0lo),
                    (unsigned int)((t1lo - t0lo) / 1000u));
        }
    }
}
