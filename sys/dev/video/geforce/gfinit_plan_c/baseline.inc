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
