/* Execute the shared descriptors, without an alternate hardware sequence. */
#include "access.h"

int gf_load_row(struct gf_display_context *c, const struct gfinit_mode *m, u32 ordinal)
{
    const struct gf_mode_row *r=&gf_mode_rows[ordinal];
    u32 reg=r->reg, value=r->bits, old=0;
    c->ordinal=ordinal;
    if (c->io.point(c->io.opaque)) return -1;
    if (r->flags & GF_LOAD_FP_ON) {
        if (gf_display_read(c,reg,r->width,&old)) return -1;
        c->fp_on=(old & GF_LOAD_FP_ON_MASK)!=0;
        if (!c->fp_on) return 0;
    }
    if (r->kind==GF_C_CR || r->kind==GF_C_SR || r->kind==GF_C_GR) {
        if (gf_display_write(c,reg,1,r->index)) return -1;
        reg++;
    }
    if (r->expr==GF_C_STATE) {
        value=0;
        for (u32 j=0;j<r->width;j++) value|=(u32)((const u8 *)m)[r->source+j]<<(8*j);
    } else if (r->expr==GF_C_LIVE) {
        if (gf_display_read(c,reg,r->width,&old)) return -1;
        value=(old & r->keep)|r->bits;
    } else if (r->expr==GF_C_PLL) {
        const struct gfinit_pll_write *w=&m->sequence.writes[r->source];
        struct gf_init_context *tx=c->load_context;
        if (reg==GF_PLL_GATE && c->pll_gate_valid) old=c->pll_gate;
        else if (gf_display_read(c,w->source,r->width,&old)) return -1;
        if (!r->source) {c->pll_gate=old;c->pll_gate_valid=true;}
        u32 i=0;
        for (;i<tx->saved_count && tx->saved[i].offset!=reg;i++) { }
        if (i==tx->saved_count && gf_init_save(tx,GF_MODE_REGISTER_BAR,reg,r->width,old)) return -1;
        value=(old & w->keep)|w->value;
    }
    if (r->kind==GF_C_AR || r->kind==GF_C_AR_GATE) {
        if (gf_display_read(c,GF_MODE_AR_STATUS,1,&old)) return -1;
        if (r->kind==GF_C_AR && gf_display_write(c,reg,1,r->index)) return -1;
    }
    if (r->kind==GF_C_DAC_DATA) {
        for (u32 j=0;j<r->count;j++)
            if (gf_display_write(c,reg,1,c->palette[j])) return -1;
        return 0;
    }
    if (gf_display_write(c,reg,r->width,value)) return -1;
    if (r->expr==GF_C_PLL && r->source+1==m->sequence.count)
        gf_init_commit(c->load_context);
    return 0;
}
