/* One pure calculation for both diagnostic C and the video service. */
#include "calc.h"
STATIC_ASSERT(GF_MODE_DCB_OR == 0 && GF_MODE_DCB_CONNECTOR == 0 &&
              (GF_MODE_DCB_HEADS & (1U << GF_MODE_HEAD)), mode_dcb_head_a_or0);
int gfinit_mode_calc(const struct gfinit_pll_limits *limits,
                    const struct gfinit_mode_saved *saved, struct gfinit_mode *o)
{
    *o = (struct gfinit_mode){0};
    gf_mode_timing(o);
    gf_mode_format(o);
    gf_mode_pitch(o);
    gf_mode_start(o);
    gf_mode_dac_format(o);
    gf_mode_output(o);
    gf_mode_cursor(o);
    gf_mode_fifo(o);
    gf_mode_extended(saved, o);
    return gf_mode_pll_program(limits, saved, o);
}
