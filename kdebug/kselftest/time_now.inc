/* W4: 実 PIT の読出し・単調性・校正済み待ちとの突き合わせ。
 * p1/p2 の判定表・再試行・出力契約は実ソースのホスト試験で見る。 */
#define KSEL_TIME_READS 10000

static void test_time_now(void)
{
    u32 lo = 0, hi = 0, plo = 0, phi = 0;
    u32 t0lo = 0, t0hi = 0, t1lo = 0, t1hi = 0;
    u32 clamp0;
    unsigned long long elapsed;
    int back = 0;
    int rc, i;

    rc = sys_time_now(&lo, &hi);
    check(rc == 0, "time:first read ok");

    /* PIT の再ロードと PIC の IRR は原子的でない。クランプ込みで逆行せず、
     * 全読出しが逆行抑止になっていないことを実機の入力で確かめる。
     * 同値で停止する時計はここでは通るため、進行は下の待ちで見る。 */
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
    check(clamp0 < (u32)KSEL_TIME_READS, "time:clamp count sane");

    /* 前後の成功と 0.2〜20ms の進行を1判定にまとめる。下位32bitの
     * 桁上がりも経過時間として扱う。校正の精度は cpu_calibrate が見る。 */
    rc = sys_time_now(&t0lo, &t0hi);
    cpu_delay_us(1000);
    int after = sys_time_now(&t1lo, &t1hi);
    elapsed = (((unsigned long long)t1hi << BITS_PER_U32) | t1lo) -
              (((unsigned long long)t0hi << BITS_PER_U32) | t0lo);
    check(rc == 0 && after == 0 && elapsed > 200 && elapsed < 20000,
          "time:1ms delay plausible");
    kprintf(KLOG_ATTR, "[selftest] time: 1ms delay measured %u us (hi %u->%u, rc %d/%d)\n",
            (unsigned int)elapsed, (unsigned int)t0hi, (unsigned int)t1hi,
            rc, after);
}
