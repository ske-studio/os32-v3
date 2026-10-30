/* CPU 校正が**丸めに負けていない**こと (票 TASK_SERIAL_VFAST 往復 3)。
 *
 * 直す前は校正ループを 1 周だけ回して tick で割っていた。実機の
 * PC-9821Ra266 (266MHz) では 1 周が 1 tick に満たず `elapsed = 0 → 1` に
 * 丸められ、`s_loops_per_tick` が実際の 1/7〜1/13 になっていた。すると
 * `cpu_delay_us(5)` が 0.5µs しか待たず、シリアルの送信ループが TxRDY を
 * 待てずに `_halt()` へ落ちて **1 バイト約 2ms の固定費**になる
 * (実機実測: 9600 で 389B/s、38400 でも 437B/s)。
 *
 * **NP21/W でも丸めは起きる** (実測 rounds = 16 / ticks = 5 = 1 周 ≒ 0.31
 * tick。旧コードの生の elapsed はそこでも 0 で、補正が 200,000 を入れていた)。
 * 実機 266MHz は 1 周 0.1 tick 未満でもっと深く落ちる。症状が出るかどうかの
 * 境目は「予算が 1 文字時間を割り込むか」だけなので、ここで起動時に見る。 */
static void test_cpu_calibrate(void)
{
    u32 lpt = cpu_loops_per_tick();

    /* 測れていれば必ず下限を超える。フォールバックに倒れたら測れていない。 */
    check(lpt >= CALIBRATE_MIN_LPT,
          "cpu calib: loops_per_tick is above the floor");
    check(lpt != 0, "cpu calib: loops_per_tick was actually set");

    /* **必要な tick 数を本当に測れたか。** ここが 5 未満なら、周回を
     * 打ち切ってしまったか PIT が止まっている = 値は当てにならない。 */
    check(cpu_calib_ticks >= CALIBRATE_MIN_TICKS,
          "cpu calib: measured at least CALIBRATE_MIN_TICKS ticks");
    check(cpu_calib_rounds >= 1, "cpu calib: ran at least one round");
    /* 打ち切りに当たっていない (当たっていたら PIT を疑う)。 */
    check(cpu_calib_rounds < CALIBRATE_MAX_ROUNDS,
          "cpu calib: did not hit the round cap");

    /* **結果が測った値そのものか。** `lpt == 合計ループ / 経過 tick` を
     * 周回数に関わらず照合する。直す前のここは `|| cpu_calib_rounds > 1` が
     * 付いていて、**複数周回ったら何であれ通る**ザルだった (往復 4 の非
     * blocker)。合計は rounds × CALIBRATE_LOOPS = 最大 4000 万で u32 に収まる。
     * フォールバックに倒れた場合だけ式から外れるので、それは別に許す
     * (倒れたこと自体は下の check が落とす)。 */
    check(cpu_calib_ticks == 0
          || lpt == (cpu_calib_rounds * CALIBRATE_LOOPS) / cpu_calib_ticks
          || lpt == CALIBRATE_FALLBACK_LPT,
          "cpu calib: loops_per_tick == total loops / ticks");

    /* **打ち切りに当たったら測れていない** (PIT が止まっている疑い)。
     * 上の `< CALIBRATE_MAX_ROUNDS` と同じことを「失敗」として言い直す —
     * 当たったときに何が起きたかを名前で残すため。 */
    check(cpu_calib_rounds != CALIBRATE_MAX_ROUNDS,
          "cpu calib: did NOT give up at the round cap");

    /* **フォールバック値そのものだったら測れていない。**
     * cpu_calibrate_compute() は ticks == 0 か極端に小さい結果のときだけ
     * この値を返すので、一致したら測定が成立していない
     * (8MHz 実機でたまたま一致する確率は無視する — その場合も
     *  「測れたかどうか分からない」ので落ちてよい)。 */
    check(lpt != CALIBRATE_FALLBACK_LPT,
          "cpu calib: result is a real measurement, not the fallback");
}
