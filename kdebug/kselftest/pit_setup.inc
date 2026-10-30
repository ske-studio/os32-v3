static void test_pit_setup(void)
{
    const struct pit_setup *p = pit_get_setup();

    /* 積んでいない (= pit_init が値を記録していない) なら以後は無意味。 */
    check(p->valid != 0, "pit:setup recorded");

    /* **クロックを読んでいること。** 未判定のまま既定値で走ると、
     * 2.4576MHz 機が直す前と同じ分周のままになる。 */
    check(sysclk_detected() != 0, "pit:sysclk detected");

    /* 分周に使ったのが判定値そのものか (既定へ倒れていないか)。 */
    check(p->clk_hz == sysclk_hz(), "pit:clk=sysclk");
    check(p->hz == (unsigned int)PIT_HZ, "pit:hz=PIT_HZ");
    check(p->reload == (unsigned int)(sysclk_hz() / (unsigned long)PIT_HZ),
          "pit:reload=clk/hz");

    /* **どちらのクロックでもちょうど 10ms。** ここが 8125 なら 19968 を
     * 2.4576MHz 機に積んでいる (直す前の姿)。 */
    check(p->period_us == (unsigned int)(1000000UL / (unsigned long)PIT_HZ),
          "pit:period=1/hz");

    /* カウンタ#0 / LSB-MSB / モード2 (レートジェネレータ) / バイナリ。
     * モードが変わると周期そのものの意味が変わる。 */
    check(p->mode == (u8)PIT_MODE_TIMER0,
          "pit:ch0 mode2");
}
