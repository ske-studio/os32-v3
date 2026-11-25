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
    check(p->hz == (unsigned int)PIT_IRQ_HZ, "pit:hz=PIT_IRQ_HZ");
    check(p->reload == (unsigned int)((sysclk_hz() + PIT_IRQ_HZ / 2) / PIT_IRQ_HZ),
          "pit:reload=nearest clk/hz");

    /* period_us is the floor of the actual rational period, for display. */
    check(p->period_us == (p->reload * PIT_PERIOD_SCALE) /
          (unsigned int)(sysclk_hz() / PIT_CLOCK_DIV), "pit:period=reload/clk");

    /* カウンタ#0 / LSB-MSB / モード2 (レートジェネレータ) / バイナリ。
     * モードが変わると周期そのものの意味が変わる。 */
    check(p->mode == (u8)PIT_MODE_TIMER0,
          "pit:ch0 mode2");
}
