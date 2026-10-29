/* Ordering/conditional waits belong only to gf_load_state. */
#include "../internal.h"
static int load_section(void *v, u32 section)
{
    struct gf_display_context *c=v;
    c->section=section;
    return 0;
}
static int load_row(void *v, const struct gfinit_mode *m, u32 ordinal)
{ return gf_load_row(v,m,ordinal); }
static int load_wait(void *v, u32 ms, bool fp_only)
{
    struct gf_display_context *c=v;
    return fp_only && !c->fp_on ? 0 : gf_init_wait(c->load_context,ms*RECOVER_US_PER_MS);
}
int gf_display_modeset(struct gf_display_context *c)
{
    struct gf_init_context tx={.io=c->io};
    static const struct gf_load_io io={load_section,load_row,load_wait};
    if (c->io.timing.clock_us(c->io.opaque,&tx.started_us)) return -1;
    c->load_context=&tx;
    c->pll_gate_valid=false;
    int rc=gf_load_state(&c->mode,c,&io);
    /* A failed PLL never leaves its snapshots referring to an expired stack. */
    if (rc && c->io.restore(c->io.opaque,&tx)) c->reusable=false;
    c->load_context=0;
    return rc;
}
