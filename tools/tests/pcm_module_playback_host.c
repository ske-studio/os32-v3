/* Reuse the actual CS4231 fixture; only device I/O is modeled. */
#define main original_pcm_main
#include "pcm_cs4231_host.c"
#undef main

int main(void)
{
    case_open();
    case_write();
    case_close();
    case_reclaim();
    /* Failure after successful IRQ registration, before DMA start. */
    reset_all();
    sim_setup_fail = 1;
    CHECK(pcm_open(PCM_RATE_44100) == OS32_ERR_IO);
    CHECK(sim_irq_reg_calls == 1 && sim_irq_unreg_calls == 1);
    CHECK(!sim_irq_registered && !pool_used[0] && pool_freed[0] == 1);
    CHECK(pcm_module_quiet());
    /* Normal module stop from an actual running driver. */
    CHECK(open_ok(PCM_RATE_44100) == 0);
    fill_staging(PCM_STG_FRAMES);
    CHECK(g_pcm.state == PCM_ST_RUNNING);
    CHECK(pcm_module_quiet());
    CHECK(!sim_irq_registered && !pool_used[0] && pool_freed[0] == 1);
    CHECK(pcm_module_quiet() && pool_freed[0] == 1);
    /* INIT stuck means that PEN=0 cannot be proved after abort writes. */
    CHECK(open_ok(PCM_RATE_44100) == 0);
    fill_staging(PCM_STG_FRAMES);
    dev_init_busy = 1;
    CHECK(!pcm_module_quiet());
    CHECK(!sim_irq_registered && !heap_used);
    CHECK(pool_used[0] && !pool_freed[0] && pool_leaked[0] == 1);
    CHECK(!pcm_module_quiet() && pool_leaked[0] == 1);
    sim_tick_per_read = 1; /* a broken sticky guard must still reach runtime FAIL */
    CHECK(pcm_open(PCM_RATE_44100) == OS32_ERR_IO);
    return failed != 0;
}
