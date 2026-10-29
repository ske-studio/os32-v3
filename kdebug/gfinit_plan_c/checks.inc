/* CHECK metadata is immutable plan input. Each item reads exactly once;
 * recover owns comparison/logging/failure, and the caller retains the value.
 * Saved operands reuse the existing single-use capture contract. */
static void c_check(struct c_builder *b,u32 source,u32 key,u32 mask,
                    u32 expected,u32 keep,u32 producer,u32 saved_key)
{
    if (source==REC_CHECK_TARGET)c_select(b,key);
    bool saved=producer!=RECOVER_NONE;
    if (saved && b->baseline) { expected=c_baseline(b,saved_key); saved=false; }
    struct recover_plan_arg a[]={{REC_IMM,source},{REC_IMM,key},{REC_IMM,mask},
        {REC_IMM,expected},{REC_IMM,keep},{REC_IMM,b->out->c_check_count++}};
    u32 id=b->out->plan.node_count;
    if(saved) {
        u32 slot=b->out->plan.capture_count++;
        a[REC_CHECK_EXPECTED]=(struct recover_plan_arg){REC_ARG_SAVED,b->p->count+slot};
        b->d->captures[slot]=(struct recover_capture){b->p->count+slot,producer,id,saved_key,0};
    }
    c_emit(b,saved?GF_C_CHECK_SAVED:GF_C_CHECK,RECOVER_NONE,RECOVER_NONE,a,C_COUNT(a));
}
/* The two reserved exit slots do not renumber the 2c1 CHECK items. */
static void c_check_exit(struct c_builder *b,u32 source,u32 mask,u32 expected,u32 slot)
{
    c_check(b,source,RECOVER_NONE,mask,expected,~0U,RECOVER_NONE,0);
    u32 id=b->out->plan.node_count-1;
    b->d->args[b->d->nodes[id].arg_first+REC_CHECK_SLOT].value=slot;
}
static void c_check_literal(struct c_builder *b,u32 key,u32 mask,u32 expected)
{ c_check(b,REC_CHECK_TARGET,key,mask,expected,~0U,RECOVER_NONE,0); }
static void c_check_mmio(struct c_builder *b,u32 reg,u32 mask,u32 expected)
{ c_check_literal(b,c_key(b,GF_C_MMIO,reg,0,4),mask,expected); }
static void c_check_in(struct c_builder *b)
{
    u32 first=b->out->plan.node_count;
    /* Recheck identity and decode before the pre-Q saved reads. */
    c_check(b,REC_CHECK_IDENTITY,RECOVER_NONE,~0U,1,~0U,RECOVER_NONE,0);
    /* The three BAR snapshot entries are the existing recover_gf regs. */
    for(u32 i=0;i<b->p->count;i++) {
        u32 key=b->p->regs[i].key;
        if(key<REC_GF_T_BAR0 || key>REC_GF_T_BAR2)continue;
        c_select(b,key);
        struct recover_plan_arg a[]={{REC_IMM,REC_CHECK_TARGET},{REC_IMM,key},
            {REC_IMM,~0U},{REC_ARG_SAVED,i},{REC_IMM,~0U},{REC_IMM,b->out->c_check_count++}};
        if (b->baseline) a[REC_CHECK_EXPECTED]=(struct recover_plan_arg){REC_IMM,
            b->baseline->bars[key-REC_GF_T_BAR0].raw};
        c_emit(b,b->baseline?GF_C_CHECK:GF_C_CHECK_SAVED,RECOVER_NONE,RECOVER_NONE,a,C_COUNT(a));
    }
    c_check_literal(b,REC_GF_T_COMMAND,~0U,RECOVER_COMMAND_OVERRIDE);
    for(u32 key=REC_GF_T_CFG50;key<=REC_GF_T_PBUS1854;key++)c_check_literal(b,key,~0U,0);
    c_check(b,REC_CHECK_ROUTE,RECOVER_NONE,RECOVER_ROUTE_MASK,0,~0U,RECOVER_NONE,0);
    for(u32 i=first;i<b->out->plan.node_count;i++)
        if(b->d->nodes[i].substep==GF_C_CHECK || b->d->nodes[i].substep==GF_C_CHECK_SAVED)
            b->d->nodes[i].flags=REC_NODE_CHECK_ENTRY;
}
static void c_check_out(struct c_builder *b,const struct gfinit_mode *m,
                         const u32 *producers,const u32 *keys,u32 pll_first)
{
    /* DESIGN §§3-4: coefficients are checked only after 50C selection.
     * 510 is evidence only; TEST excludes sense and compares bit20 only. */
    c_check_mmio(b,C_PLL_SELECT,~GF_MODE_PLLSEL_VPLL_A,m->pllsel); /* bit28 reads 0 on NV44A */
    c_check_mmio(b,C_PLL_STATUS,0,0);
    const struct gfinit_pll_write *q=m->sequence.writes;
    c_check_mmio(b,GF_PLL_VIDEO,~GF_PLL_VIDEO_KEEP,q[C_PLL_PRIMARY_WRITE].value);
    c_check_mmio(b,GF_PLL_VIDEO_COEFF,~GF_PLL_VIDEO_S2_KEEP,q[C_PLL_COEFF_WRITE].value);
    c_check_mmio(b,GF_PLL_VIDEO_STAGE,GF_PLL_CTRL_SINGLE,q[C_PLL_STAGE_WRITE].value);
    for(u32 i=0;i<C_COUNT(c_fields);i++)if(c_fields[i].reg==C_SEL_CLK)
        c_check(b,REC_CHECK_TARGET,keys[i],~0U,0,~GF_MODE_SEL_CLK_CLEAR,producers[i],keys[i]);
    u32 gate=c_key(b,GF_C_MMIO,GF_PLL_GATE,0,4);
    c_check(b,REC_CHECK_TARGET,gate,~0U,0,~0U,pll_first,gate);
    c_check_mmio(b,C_CONFIG,C_CONFIG_MASK,m->config);
    c_check_mmio(b,C_START,~0U,m->start);
    for(u32 index=0;index<=GF_MODE_CR_LINE_COMPARE;index++) {
        u32 expected=m->cr[index];
        if(index==GF_MODE_CR_VE || index==GF_MODE_CR_MODE)expected|=C_CR_LOCK;
        c_check_literal(b,c_key(b,GF_C_CR,0,index,1),GF_MODE_BYTE_MASK,expected);
    }
    const u32 extra[]={GF_MODE_CR_PITCH_HI,GF_MODE_CR_RPC1,GF_MODE_CR_LSR,
        GF_MODE_CR_PIXEL,RECOVER_CR44,GF_MODE_CR_PITCH11,GF_MODE_CR_BURST,GF_MODE_CR_LWM,C_CR_PITCH_EXTRA};
    for(u32 i=0;i<C_COUNT(extra);i++)
        c_check_literal(b,c_key(b,GF_C_CR,0,extra[i],1),
            extra[i]==GF_MODE_CR_RPC1 ? C_CR_RPC1_MASK : GF_MODE_BYTE_MASK,m->cr[extra[i]]);
    for(u32 i=0;i<C_COUNT(c_fields);i++)if(c_fields[i].index==GF_MODE_CR_LCD && c_fields[i].kind==GF_C_CR)
        c_check(b,REC_CHECK_TARGET,keys[i],GF_MODE_BYTE_MASK,0,~GF_MODE_LCD_CLEAR,producers[i],keys[i]);
    c_check_literal(b,c_key(b,GF_C_SR,0,1,1),GF_MODE_BYTE_MASK,GF_MODE_DAC_ENABLE);
    c_check_literal(b,c_key(b,GF_C_MISC,0,0,1),GF_MODE_BYTE_MASK,m->misc);
    c_check_mmio(b,C_GENERAL,~0U,m->general);
    c_check_mmio(b,C_DACCLK,C_DACCLK_MASK,m->dacclk);
    c_check_mmio(b,C_TEST,GF_MODE_TEST_CONTROL,m->test_control);
    for(u32 key=REC_GF_T_CFG50;key<=REC_GF_T_PBUS1854;key++)c_check_literal(b,key,~0U,0);
    c_check_literal(b,REC_GF_T_COMMAND,~0U,RECOVER_COMMAND_OVERRIDE);
    c_check_exit(b,REC_CHECK_ROUTE,RECOVER_ROUTE_MASK,RECOVER_ROUTE_RGB_IN,GF_C_EXIT_ROUTE_SLOT);
    c_check_exit(b,REC_CHECK_CELL,~0U,1,GF_C_EXIT_CELL_SLOT);
}
