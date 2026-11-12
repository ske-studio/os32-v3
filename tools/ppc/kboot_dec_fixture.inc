/* Foreground diagnostics only: tick callbacks never access ESCC. */
volatile u32 kctx_irq_depth;
volatile u32 f2_delta, f2_ticks, f2_hz;
void ppc_trap_fatal_sink(const struct ppc_frame *f,u32 reason)
{ (void)f; (void)reason; bad(); }
static void rate_fail(void) { say("F2-RATE-FAIL\r\n"); ppc_stop(); }
void fixture_main(void)
{
    say("F2-BEGIN\r\n");
    struct clock_info info;
    if (platform_clock_init(pm_tick_hz())) bad();
    platform_clock_info(&info);
    if (ppc_dec_start(info.counter_hz,pm_tick_hz())) {
        say("F2-START-FAIL\r\n"); ppc_stop();
    }
    say("F2-ARMED\r\n");
    u32 start=ppc_read_tb(), ticks=tick_count;
    ppc_enable();
    while ((u32)(ppc_read_tb()-start)<2*info.counter_hz)
        __asm__ volatile("" ::: "memory");
    ppc_disable();
    u32 delta=ppc_read_tb()-start, elapsed=tick_count-ticks;
    f2_delta=delta; f2_ticks=elapsed; f2_hz=info.counter_hz;
    /* 2s is bounded below 2^32 TB counts at F1's maximum 1GHz. */
    unsigned long long scaled=(unsigned long long)elapsed*info.counter_hz;
    unsigned long long expected=(unsigned long long)delta*100;
    if (scaled<expected*95/100 || scaled>expected*105/100) rate_fail();
    u32 lo=0, hi=0;
    if (platform_clock_read_us(&lo,&hi) || hi || lo<2000000) bad();
    say("F2-PASS\r\n");
    fixture_finish();
}
