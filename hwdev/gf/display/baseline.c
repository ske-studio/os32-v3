/* Read once after main4. Only complete captures become reusable inputs. */
#include "access.h"
#include "state.h"
static int baseline_point(struct gf_display_context *c)
{
    struct gf_init_context *init=c->init;
    u32 now;
    if (c->io.point(c->io.opaque) || c->io.timing.clock_us(c->io.opaque,&now)) return -1;
    init->elapsed_us=now-init->started_us;
    return init->elapsed_us>=GF_INIT_EXEC_US ? -1 : 0;
}
int gf_display_baseline(struct gf_display_context *c)
{
    if (c->baseline_valid) return 0;
    struct gfinit_mode_saved saved={0};
    u32 gate,pmc;
    for (u32 i=0;i<GF_DISPLAY_COUNT(c_fields);i++) {
        const struct c_saved_field *f=&c_fields[i];
        u32 value;
        if (baseline_point(c) ||
            gf_display_value(c,f->kind,f->reg,f->index,f->width,&value)) return -1;
        for (u32 j=0;j<f->width;j++)
            ((u8 *)&saved)[f->saved_offset+j]=(u8)(value>>(8*j));
    }
    if (baseline_point(c) || gf_display_read(c,GF_PLL_GATE,4,&gate) ||
        gf_display_read(c,RECOVER_PMC_ENABLE,4,&pmc) || baseline_point(c)) return -1;
    c->baseline=saved;c->baseline_c040=gate;c->baseline_pmc=pmc;
    c->baseline_valid=true;
    return 0;
}
