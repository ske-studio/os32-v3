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
#define CHECK_MMIO(reg,mask,value) c_check_mmio(b,reg,mask,value)
#define CHECK_INDEX(kind,index,mask,value) c_check_literal(b,c_key(b,kind,0,index,1),mask,value)
#define CHECK_SAVED(i,mask,keep) do { \
    u32 key=(i)==GF_DISPLAY_COUNT(c_fields) ? c_key(b,GF_C_MMIO,GF_PLL_GATE,0,4) : keys[i]; \
    u32 producer=(i)==GF_DISPLAY_COUNT(c_fields) ? pll_first : producers[i]; \
    c_check(b,REC_CHECK_TARGET,key,mask,0,keep,producer,key); \
} while (0)
#include "../../hwdev/gf/display/checks.inc"
#undef CHECK_MMIO
#undef CHECK_INDEX
#undef CHECK_SAVED
    for(u32 key=REC_GF_T_CFG50;key<=REC_GF_T_PBUS1854;key++)c_check_literal(b,key,~0U,0);
    c_check_literal(b,REC_GF_T_COMMAND,~0U,RECOVER_COMMAND_OVERRIDE);
    c_check_exit(b,REC_CHECK_ROUTE,RECOVER_ROUTE_MASK,RECOVER_ROUTE_RGB_IN,GF_C_EXIT_ROUTE_SLOT);
    c_check_exit(b,REC_CHECK_CELL,~0U,1,GF_C_EXIT_CELL_SLOT);
}
