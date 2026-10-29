static const struct c_saved_field c_fields[] = {
    {C_OFF(cr)+GF_MODE_CR_ENH,GF_C_CR,0,GF_MODE_CR_ENH,1,~GF_MODE_ENH_CLEAR,0,0},
    {C_OFF(cr)+GF_MODE_CR_LCD,GF_C_CR,0,GF_MODE_CR_LCD,1,~GF_MODE_LCD_CLEAR,0,0},
    {C_OFF(cr)+GF_MODE_CR_SCRATCH4,GF_C_CR,0,GF_MODE_CR_SCRATCH4,1,~0U,0,0},
    {C_OFF(cr)+GF_MODE_CR_SCRATCH4B,GF_C_CR,0,GF_MODE_CR_SCRATCH4B,1,~0U,0,GF_MODE_HEAD_A_SCRATCH},
    {C_OFF(cr)+GF_MODE_CR_LATENCY,GF_C_CR,0,GF_MODE_CR_LATENCY,1,~0U,GF_MODE_LATENCY_ADD,0},
    {C_OFF(gpio_ext),GF_C_MMIO,C_GPIO_EXT,0,4,~0U,0,0},
    {C_OFF(crtc850),GF_C_MMIO,C_CRTC850,0,4,~0U,0,0},
    {C_OFF(sel_clk),GF_C_MMIO,C_SEL_CLK,0,4,~GF_MODE_SEL_CLK_CLEAR,0,0}
};
STATIC_ASSERT(C_COUNT(c_fields) == GF_STAGE_FIELDS, gf_stage_field_count);
static u32 c_baseline(const struct c_builder *b, u32 key)
{
    const struct recover_target *t = &b->p->targets[key];
    if (t->space == REC_BAR_MMIO && t->offset == GF_PLL_GATE)
        return b->baseline->values[GF_STAGE_GATE];
    for (u32 i = 0; i < C_COUNT(c_fields); i++) {
        const struct c_saved_field *f = &c_fields[i];
        if ((f->kind == GF_C_CR && t->space == REC_VGA_INDEXED &&
             t->port == RECOVER_CRTC && t->index == f->index) ||
            (f->kind == GF_C_MMIO && t->space == REC_BAR_MMIO && t->offset == f->reg))
            return b->baseline->values[i];
    }
    return 0;
}
