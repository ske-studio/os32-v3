/* NV44A PLL2: MIT nouveau nvkm/subdev/bios/init.c:985-995;
 * sequence model: nvkm/subdev/devinit/nv04.c:173-331 (see gfinit_pll.h). */
#include "internal.h"
#include "../../kdebug/gfinit_pll.h"

int gf_init_pll(struct gf_init_context *ctx, u32 reg, u32 khz)
{
    struct gfinit_pll_limits limits;
    struct gfinit_pll_solution solution;
    struct gfinit_pll_sequence seq;
    u32 sources[GF_PLL_WRITES_MAX], values[GF_PLL_WRITES_MAX], count = 0;

    if (gfinit_pll_parse(ctx->rom, ctx->rom_size, GF_INIT_PLL_TABLE, reg, &limits) ||
        gfinit_pll_solve(&limits, khz, &solution) ||
        gfinit_pll_sequence(reg, &limits, &solution, &seq)) return -1;
    for (u32 i = 0; i < seq.count; i++) {
        u32 source = seq.writes[i].source, j = 0;
        if (!source) continue;
        for (; j < count && sources[j] != source; j++) { }
        if (j == count) {
            sources[count] = source;
            if (ctx->io.read(ctx->io.opaque, KPEEK_NV_BAR_MMIO, source, 4, &values[count])) return -1;
            count++;
        }
    }
    for (u32 i = 0; i < seq.count; i++) {
        const struct gfinit_pll_write *w = &seq.writes[i];
        u32 j = 0, old, value = w->value;
        for (; j < ctx->saved_count && ctx->saved[j].offset != w->reg; j++) { }
        if (j == ctx->saved_count) {
            for (j = 0; j < count && sources[j] != w->reg; j++) { }
            if (j < count) old = values[j];
            else if (ctx->io.read(ctx->io.opaque, KPEEK_NV_BAR_MMIO, w->reg, 4, &old)) return -1;
            if (gf_init_save(ctx, KPEEK_NV_BAR_MMIO, w->reg, 4, old)) return -1;
        }
        if (w->source) {
            for (j = 0; sources[j] != w->source; j++) { }
            value |= values[j] & w->keep;
        }
        if (ctx->io.write(ctx->io.opaque, KPEEK_NV_BAR_MMIO, w->reg, 4, value)) return -1;
    }
#if GF_VPLL_PROBE
    /* Retained diagnostic probe: first coefficients, switch, repeat using
     * the original source snapshots, then return the switch to its default. */
    if (ctx->main_id==RECOVER_VPLL_MAIN && ctx->fail_pc==RECOVER_VPLL_PC && reg==GF_PLL_VIDEO) {
        u32 old;
        if (ctx->io.read(ctx->io.opaque,0,RECOVER_VPLL_SWITCH_OFF,4,&old) ||
            gf_init_save(ctx,0,RECOVER_VPLL_SWITCH_OFF,4,old) ||
            ctx->io.write(ctx->io.opaque,0,RECOVER_VPLL_SWITCH_OFF,4,RECOVER_VPLL_SWITCH_ON)) return -1;
        for (u32 i=0;i<seq.count;i++) {
            const struct gfinit_pll_write *w=&seq.writes[i];
            u32 value=w->value;
            for (u32 j=0;j<count;j++) if (sources[j]==w->source) value|=values[j]&w->keep;
            if (ctx->io.write(ctx->io.opaque,0,w->reg,4,value)) return -1;
        }
        if (ctx->io.write(ctx->io.opaque,0,RECOVER_VPLL_SWITCH_OFF,4,RECOVER_VPLL_SWITCH_DEFAULT)) return -1;
    }
#endif
    gf_init_commit(ctx);
    return 0;
}
