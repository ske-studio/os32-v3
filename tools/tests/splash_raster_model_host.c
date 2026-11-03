/* Independent 0.1 us waveform, consumed by the production H capture. */
static unsigned int model_phase;
#define PROBE_MODEL_PHASE(us) ((us) * 10 + model_phase)
#define _start probe_original_start
#include "splash_probe_host.c"
#undef _start

void _start(void)
{
    (void)probe_hcollect_check;
    for (model_phase = 0; model_phase < 318; model_phase++) {
        setup(1, 0);
        in_us = clock_cost_us = out_us = 1;
        struct SplashRaster *s = pc98_splash_raster_state();
        probe_costs(s);
        probe_eight_us = (unsigned long long)8 * total * 100000 / den;
        REQUIRE(probe_hblank(s) == 64);
        REQUIRE(!probe_errors && irq_on && probe_hd.completed == 64);
        for (u32 i = 0; i < 64; i++) {
            REQUIRE(probe_hsamples[i] >= 31 && probe_hsamples[i] <= 33);
            REQUIRE(probe_highsamples[i] >= 5 && probe_highsamples[i] <= 6);
        }
    }
    finish(0);
}
