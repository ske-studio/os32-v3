/* Fixed C composition, GF6200_4C_DESIGN §§3-4 and stage-2a decisions.
 * Hardware values/order come from the stage-1 table (nouveau MIT references
 * there); this file only lowers it to existing recover accesses/captures. */
#include "gfinit_plan_c.h"
#include "gfinit_bus.h"
#include "recover_gf_c.h"
#if !GF_VPLL_PROBE
#define C_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define C_OFF(f) __builtin_offsetof(struct gfinit_mode, f)
#define C_ROW_WORDS 14U
#define C_PENDING_WORDS (C_ROW_WORDS + 1U)
#define C_EXTRA_NODES 540U
#define C_EXTRA_ARGS 2100U
#define C_EXTRA_LAYOUTS 194U
#define C_EXTRA_RULES 260U
#define C_EXTRA_CAPTURES 13U
#define C_FP_ON_MASK 0x10000011U
#define C_PLL_RESTORE 4U
#define C_GPIO_EXT 0x60081cU
#define C_CRTC850 0x600850U
#define C_SEL_CLK 0x680524U
#define C_PLL_SELECT 0x68050cU
#define C_PLL_STATUS 0x680510U
#define C_CONFIG 0x600804U
#define C_START 0x600800U
#define C_DACCLK 0x68052cU
#define C_TEST 0x680608U
#define C_GENERAL 0x680600U
#define C_DACCLK_MASK 0x101U
#define C_CONFIG_MASK 3U
#define C_CR_LOCK 0x80U
#define C_CR_PITCH_EXTRA 0x47U
#define C_PLL_STAGE_WRITE 1U
#define C_PLL_COEFF_WRITE 2U
#define C_PLL_PRIMARY_WRITE 3U
/* Boundaries in the fixed stage-1 row contract (not ROM offsets). */
enum c_row_boundary { C_ROW_P=2, C_ROW_SD=15, C_ROW_L1=26,
    C_ROW_L2=46, C_ROW_L3=120, C_ROW_COMMIT=190,
    C_ROW_DPMS_OFF_WAIT=5, C_ROW_DPMS_ON_WAIT=200 };
static const u32 c_imm[] = {REC_IMM, REC_IMM};
static const u32 c_live[] = {REC_IMM, REC_ARG_READ, REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static const u32 c_saved[] = {REC_IMM, REC_ARG_SAVED, REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static const u32 c_branch[] = {REC_ARG_READ};
static const u32 c_pending[C_PENDING_WORDS] = {REC_IMM};
static const u32 c_vram_kinds[] = {REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static const u32 c_check_kinds[] = {REC_IMM,REC_IMM,REC_IMM,REC_IMM,REC_IMM,REC_IMM};
static const u32 c_check_saved_kinds[] = {REC_IMM,REC_IMM,REC_IMM,REC_ARG_SAVED,REC_IMM,REC_IMM};
/* REC_IMM=0: all of the pending payload is immutable metadata, never reads. */
STATIC_ASSERT(REC_IMM == 0, c_pending_immediates);
STATIC_ASSERT(REC_GF_C_PHASE == REC_MODE_C, c_phase_contract);
static int c_value(const u32 *a, u32 n, u32 *v)
{
    if (n != 5) return -1;
    *v = (((a[0] & a[1]) + a[2]) | a[3]) & a[4];
    return 0;
}
static int c_fp_on(const u32 *a, u32 n, u32 *v)
{
    if (n != 1) return -1;
    *v = (a[0] & C_FP_ON_MASK) != 0;
    return 0;
}
struct c_builder {
    struct gfinit_plan *out;
    struct gfinit_decode *d;
    struct recover_profile *p;
    u32 ctx, select_base;
};
static u32 c_emit(struct c_builder *b, u32 sub, u32 rule, u32 expr,
                   const struct recover_plan_arg *args, u32 argc)
{
    struct recover_plan *p = &b->out->plan;
    u32 id = p->node_count++, first = p->arg_count;
    for (u32 i = 0; i < argc; i++) b->d->args[p->arg_count++] = args[i];
    b->d->nodes[id] = (struct recover_plan_node){.phase=REC_MODE_C,
        .main_id=RECOVER_NONE,.pc=RECOVER_NONE,.opcode=GF_C_OPCODE,
        .substep=sub,.ctx=b->ctx,.head=GF_MODE_HEAD,.rule_id=rule,
        .expr_id=expr,.arg_first=first,.arg_count=argc,.next={id+1,id+1}};
    return id;
}
static void c_layout(struct c_builder *b, u32 sub, u32 n, const u32 *kinds, u32 dir, u32 flags)
{
    b->out->layouts[b->p->layout_count++] =
        (struct recover_opcode_layout){GF_C_OPCODE, sub, n, kinds, dir, flags};
}
static void c_marker(struct c_builder *b, u32 section)
{
    struct recover_plan_arg a[] = {{REC_IMM,section},{REC_IMM,GF_MODE_C}};
    c_emit(b, GF_C_SECTION, RECOVER_NONE, RECOVER_NONE, a, C_COUNT(a));
}
static u32 c_unbound(struct c_builder *b, u32 pending, const struct recover_gf_c_row *row)
{
    struct recover_plan_arg a[C_PENDING_WORDS] = {{REC_IMM,pending}};
    if (row) {
        const u32 values[C_ROW_WORDS] = {row->id,row->key,row->phase,row->kind,
            row->reg,row->index,row->width,row->expr,row->source,row->keep,
            row->bits,row->verify,row->count,row->flags};
        for (u32 i=0;i<C_ROW_WORDS;i++) a[i+1].value=values[i];
    }
    u32 id=c_emit(b, GF_C_UNBOUND, RECOVER_NONE, RECOVER_NONE, a, C_COUNT(a));
    b->out->c_unbound++;
    return id;
}
static void c_wait(struct c_builder *b, u32 ms)
{
    struct recover_plan_arg a = {REC_IMM,ms};
    c_emit(b, GF_C_WAIT, RECOVER_NONE, RECOVER_NONE, &a, 1);
}
static void c_vram(struct c_builder *b)
{
    for (u32 i = 0; i < 2; i++) {
        u32 sub = i ? GF_C_VRAM_COMPARE : GF_C_VRAM_WRITE;
        struct recover_plan_arg a[] = {{REC_IMM,GF_MODE_VRAM_BAR},
            {REC_IMM,GF_MODE_VRAM_OFFSET},{REC_IMM,GF_MODE_VRAM_BYTES},
            {REC_IMM,i ? REC_READ : REC_WRITE}};
        c_layout(b,sub,C_COUNT(a),c_vram_kinds,RECOVER_NONE,REC_LAYOUT_MODE_VRAM);
        c_emit(b,sub,RECOVER_NONE,RECOVER_NONE,a,C_COUNT(a));
    }
}
static u32 c_key(struct c_builder *b, u32 kind, u32 reg, u32 index, u32 width)
{
    struct recover_target t = {.space=REC_BAR_MMIO,.offset=reg,
        .index=RECOVER_NONE,.width=width,.access=REC_COLD,.read_safe=1};
    if (kind==GF_C_CR || kind==GF_C_SR || kind==GF_C_GR) {
        t.space=REC_VGA_INDEXED;t.offset=0;t.index=index;
        t.port=kind==GF_C_CR ? RECOVER_CRTC : kind==GF_C_SR ? RECOVER_SEQ : RECOVER_GFX;
    } else if (kind==GF_C_MISC) {
        t.space=REC_VGA_DIRECT;t.offset=0;t.port=RECOVER_MISC_WRITE;
    }
    for (u32 i=0;i<b->p->target_count;i++) {
        const struct recover_target *q=&b->p->targets[i];
        if(q->space==t.space && q->bar==t.bar && q->offset==t.offset &&
           q->head==t.head && q->port==t.port && q->index==t.index && q->width==t.width) {
            /* C may read this shared key, but RUN keeps the 4a pre-read policy. */
            if (!q->read_safe) for (u32 j=0;j<b->p->rule_count;j++) {
                struct recover_rule *w=&b->out->rules[j];
                if (w->key==i && w->phase==REC_RUN) w->flags|=REC_RUN_NO_PREREAD;
            }
            b->out->targets[i].read_safe=1;
            return i;
        }
    }
    u32 key=b->p->target_count++;
    b->out->targets[key]=t;
    return key;
}
static void c_rule(struct c_builder *b, u32 id, u32 key, u32 sub, u32 bits, u32 expr, u32 verify)
{
    u32 mask=b->p->targets[key].width==1 ? GF_MODE_BYTE_MASK : ~0U;
    b->out->rules[b->p->rule_count++] = (struct recover_rule){.id=id,.key=key,
        .phase=REC_MODE_C,.main_id=RECOVER_NONE,.pc=RECOVER_NONE,.substep=sub,
        .write_mask=mask,.verify_mask=verify,.value={
            .mode=expr==RECOVER_NONE ? REC_LITERAL : REC_EXPR,
            .mask=expr==RECOVER_NONE ? mask : 0,
            .bits=expr==RECOVER_NONE ? bits : 0,.max=mask,.expr_id=expr}};
}
static void c_select(struct c_builder *b, u32 key)
{
    const struct recover_target *t=&b->p->targets[key];
    if (t->space!=REC_VGA_INDEXED) return;
    u32 id=b->select_base+key;
    bool found=false;
    for(u32 i=0;i<b->p->rule_count;i++) if(b->p->rules[i].id==id) found=true;
    if(!found) c_rule(b,id,key,GF_C_SELECT,t->index,RECOVER_NONE,GF_MODE_BYTE_MASK);
    struct recover_plan_arg a[]={{REC_IMM,key},{REC_IMM,t->index}};
    c_emit(b,GF_C_SELECT,id,RECOVER_NONE,a,C_COUNT(a));
}
static u32 c_read(struct c_builder *b,u32 key)
{
    c_select(b,key);
    struct recover_plan_arg a={REC_IMM,key};
    return c_emit(b,GF_C_READ,RECOVER_NONE,RECOVER_NONE,&a,1);
}
/* CHECK metadata is immutable plan input. Each item reads exactly once;
 * recover owns comparison/logging/failure, and the caller retains the value.
 * Saved operands reuse the existing single-use capture contract. */
static void c_check(struct c_builder *b,u32 source,u32 key,u32 mask,
                    u32 expected,u32 keep,u32 producer,u32 saved_key)
{
    if (source==REC_CHECK_TARGET)c_select(b,key);
    bool saved=producer!=RECOVER_NONE;
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
static void c_check_literal(struct c_builder *b,u32 key,u32 mask,u32 expected)
{ c_check(b,REC_CHECK_TARGET,key,mask,expected,~0U,RECOVER_NONE,0); }
static void c_check_mmio(struct c_builder *b,u32 reg,u32 mask,u32 expected)
{ c_check_literal(b,c_key(b,GF_C_MMIO,reg,0,4),mask,expected); }
static void c_check_in(struct c_builder *b)
{
    /* Recheck identity and decode before the pre-Q saved reads. */
    c_check(b,REC_CHECK_IDENTITY,RECOVER_NONE,~0U,1,~0U,RECOVER_NONE,0);
    /* The three BAR snapshot entries are the existing recover_gf regs. */
    for(u32 i=0;i<b->p->count;i++) {
        u32 key=b->p->regs[i].key;
        if(key<REC_GF_T_BAR0 || key>REC_GF_T_BAR2)continue;
        c_select(b,key);
        struct recover_plan_arg a[]={{REC_IMM,REC_CHECK_TARGET},{REC_IMM,key},
            {REC_IMM,~0U},{REC_ARG_SAVED,i},{REC_IMM,~0U},{REC_IMM,b->out->c_check_count++}};
        c_emit(b,GF_C_CHECK_SAVED,RECOVER_NONE,RECOVER_NONE,a,C_COUNT(a));
    }
    c_check_literal(b,REC_GF_T_COMMAND,~0U,RECOVER_COMMAND_OVERRIDE);
    for(u32 key=REC_GF_T_CFG50;key<=REC_GF_T_PBUS1854;key++)c_check_literal(b,key,~0U,0);
    c_check(b,REC_CHECK_ROUTE,RECOVER_NONE,RECOVER_ROUTE_MASK,0,~0U,RECOVER_NONE,0);
}
/* s(x): one read before Q, independent single-use captures for each consumer.
 * The value transformations are the saved fields in gfinit_mode_calc. */
struct c_saved_field { u32 offset, kind, reg, index, width, keep, add, bits; };
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
static void c_check_out(struct c_builder *b,const struct gfinit_mode *m,
                         const u32 *producers,const u32 *keys,u32 pll_first)
{
    /* DESIGN §§3-4: coefficients are checked only after 50C selection.
     * 510 is evidence only; TEST excludes sense and compares bit20 only. */
    c_check_mmio(b,C_PLL_SELECT,~0U,m->pllsel);
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
        c_check_literal(b,c_key(b,GF_C_CR,0,extra[i],1),GF_MODE_BYTE_MASK,m->cr[extra[i]]);
    for(u32 i=0;i<C_COUNT(c_fields);i++)if(c_fields[i].index==GF_MODE_CR_LCD && c_fields[i].kind==GF_C_CR)
        c_check(b,REC_CHECK_TARGET,keys[i],GF_MODE_BYTE_MASK,0,~GF_MODE_LCD_CLEAR,producers[i],keys[i]);
    c_check_literal(b,c_key(b,GF_C_SR,0,1,1),GF_MODE_BYTE_MASK,GF_MODE_DAC_ENABLE);
    c_check_literal(b,c_key(b,GF_C_MISC,0,0,1),GF_MODE_BYTE_MASK,m->misc);
    c_check_mmio(b,C_GENERAL,~0U,m->general);
    c_check_mmio(b,C_DACCLK,C_DACCLK_MASK,m->dacclk);
    c_check_mmio(b,C_TEST,GF_MODE_TEST_CONTROL,m->test_control);
    for(u32 key=REC_GF_T_CFG50;key<=REC_GF_T_PBUS1854;key++)c_check_literal(b,key,~0U,0);
    c_check_literal(b,REC_GF_T_COMMAND,~0U,RECOVER_COMMAND_OVERRIDE);
    /* 2c2 fills these result slots and replaces these rejecting barriers. */
    u32 route=c_unbound(b,GF_C_PENDING_EXIT,0);
    b->d->args[b->d->nodes[route].arg_first+1].value=GF_C_EXIT_ROUTE_SLOT;
    u32 cell=c_unbound(b,GF_C_PENDING_CELL,0);
    b->d->args[b->d->nodes[cell].arg_first+1].value=GF_C_EXIT_CELL_SLOT;
}
#endif
int gfinit_plan_build_c(struct gfinit_plan *out, struct gfinit_decode *d,
                        struct recover_profile *p, u32 bar1_length,
                        const u8 *rom, u32 rom_size)
{
#if GF_VPLL_PROBE
    /* Separate diagnostic rules reuse the C ID namespace. Never mix profiles. */
    (void)out;(void)d;(void)p;(void)bar1_length;(void)rom;(void)rom_size;
    return -1;
#else
    struct gfinit_pll_limits limits;
    struct gfinit_mode mode;
    const struct gfinit_mode_saved empty={0};
    if (!out || !d || !p || bar1_length < GF_MODE_PITCH*GF_MODE_HEIGHT ||
        gfinit_pll_parse(rom,rom_size,GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&limits) ||
        gfinit_mode_calc(&limits,&empty,&mode) ||
        mode.pll.error > GF_MODE_CLOCK_KHZ/GF_PLL_MARGIN_DIV ||
        gfinit_plan_prefix(out,d,p)) return -1;
    if(out->plan.node_count+C_EXTRA_NODES>RECOVER_NODE_MAX ||
       out->plan.arg_count+C_EXTRA_ARGS>GF_INIT_ARGS ||
       out->plan.capture_count+C_EXTRA_CAPTURES>RECOVER_CAPTURE_MAX ||
       out->plan.entry_count==RECOVER_ENTRY_MAX || out->plan.ctx_count==RECOVER_CTX_MAX ||
       p->target_count+recover_gf_c_target_count>GF_PLAN_TARGETS ||
       p->rule_count+C_EXTRA_RULES>GF_PLAN_RULES ||
       p->layout_count+C_EXTRA_LAYOUTS>GF_PLAN_LAYOUTS || p->expr_count+3>GF_PLAN_EXPRS) return -1;
    struct c_builder b={out,d,p,out->plan.ctx_count++,REC_GF_C_RULE_FIRST+recover_gf_c_row_count};
    out->mode_flags=GF_MODE_C;out->c_node=out->plan.node_count;out->c_unbound=0;
    out->c_check_count=0;
    for(u32 i=0;i<GF_C_CHECK_MAX;i++)out->c_checks[i]=(struct gfinit_c_check_result){0};
    out->vram_node=RECOVER_NONE;out->bar1_length=bar1_length;
    out->entries[out->plan.entry_count++]=(struct recover_plan_entry){REC_MODE_C,RECOVER_NONE,out->c_node};
    for(u32 i=0;i<out->c_node;i++)for(u32 j=0;j<2;j++)
        if(d->nodes[i].next[j]==RECOVER_NONE)d->nodes[i].next[j]=out->c_node;
    out->exprs[p->expr_count++]=(struct recover_expr){GF_C_EXPR_LIVE,5,c_live+1,c_value};
    out->exprs[p->expr_count++]=(struct recover_expr){GF_C_EXPR_SAVED,5,c_saved+1,c_value};
    out->exprs[p->expr_count++]=(struct recover_expr){GF_C_EXPR_FP_ON,1,c_branch,c_fp_on};
    c_layout(&b,GF_C_SELECT,2,c_imm,REC_SELECT,0);
    c_layout(&b,GF_C_READ,1,c_imm,REC_READ,0);
    c_layout(&b,GF_C_SECTION,2,c_imm,RECOVER_NONE,0);
    c_layout(&b,GF_C_UNBOUND,C_PENDING_WORDS,c_pending,RECOVER_NONE,REC_LAYOUT_UNBOUND);
    c_layout(&b,GF_C_WAIT,1,c_imm,RECOVER_NONE,REC_LAYOUT_MODE_WAIT);
    c_layout(&b,GF_C_CHECK,REC_CHECK_ARGS,c_check_kinds,RECOVER_NONE,REC_LAYOUT_MODE_CHECK);
    c_layout(&b,GF_C_CHECK_SAVED,REC_CHECK_ARGS,c_check_saved_kinds,RECOVER_NONE,REC_LAYOUT_MODE_CHECK);
    c_layout(&b,GF_C_BRANCH,1,c_branch,RECOVER_NONE,REC_LAYOUT_CHECK_EXPR);
    c_layout(&b,GF_C_COLOR,2,c_imm,RECOVER_NONE,REC_LAYOUT_MODE_COLOR);
    for(u32 i=0;i<recover_gf_c_target_count;i++) {
        const struct recover_gf_c_target *t=&recover_gf_c_targets[i];
        if(t->kind<=GF_C_GR || t->kind==GF_C_MISC)
            (void)c_key(&b,t->kind,t->reg,t->index,t->width);
    }
    c_marker(&b,GF_C_CHECK_IN);
    c_check_in(&b);
    u32 producers[C_COUNT(c_fields)], keys[C_COUNT(c_fields)], pll_first=RECOVER_NONE;
    for(u32 i=0;i<C_COUNT(c_fields);i++) {
        const struct c_saved_field *f=&c_fields[i];
        keys[i]=c_key(&b,f->kind,f->reg,f->index,f->width);
        producers[i]=c_read(&b,keys[i]);
    }
    c_marker(&b,GF_C_Q);
    for(u32 i=0;i<recover_gf_c_row_count;i++) {
        const struct recover_gf_c_row *row=&recover_gf_c_rows[i];
        /* Stage-1 row boundaries: Q, P, S/D, L1, L2, L3, commit, R. */
        if(i==C_ROW_P)c_marker(&b,GF_C_P);
        if(i==C_ROW_SD) {c_marker(&b,GF_C_VRAM);c_vram(&b);c_marker(&b,GF_C_SD);}
        if(i==C_ROW_L1)c_marker(&b,GF_C_L1);
        if(i==C_ROW_L2)c_marker(&b,GF_C_L2);
        if(i==C_ROW_L3)c_marker(&b,GF_C_L3);
        if(i==C_ROW_COMMIT)c_marker(&b,GF_C_COMMIT);
        if(row->kind==GF_C_ROUTE) {c_marker(&b,GF_C_R);c_unbound(&b,GF_C_PENDING_CELL,0);}
        if (row->kind==GF_C_AR || row->kind==GF_C_AR_GATE || row->kind==GF_C_DAC_MASK) {
            u32 value = row->expr==GF_C_STATE ? ((const u8 *)&mode)[row->source] : row->bits;
            struct recover_plan_arg a[]={{REC_IMM,i},{REC_IMM,value}};
            c_emit(&b,GF_C_COLOR,RECOVER_NONE,RECOVER_NONE,a,C_COUNT(a));
            continue;
        }
        if (row->kind==GF_C_DAC_INDEX || row->kind==GF_C_DAC_DATA) continue;
        if(row->kind==GF_C_ROUTE) {
            c_unbound(&b,GF_C_PENDING_ROW,row);continue;
        }
        u32 key=c_key(&b,row->kind,row->reg,row->index,row->width);
        u32 branch=RECOVER_NONE;
        if(row->flags & REC_GF_C_FP_ON) {
            struct recover_plan_arg a={REC_ARG_READ,key};
            branch=c_emit(&b,GF_C_BRANCH,RECOVER_NONE,GF_C_EXPR_FP_ON,&a,1);
        }
        c_select(&b,key);
        u32 expr=RECOVER_NONE,keep=row->keep,bits=row->bits,add=0;
        u32 source=key,producer=RECOVER_NONE,mask=row->width==1 ? GF_MODE_BYTE_MASK : ~0U;
        if(row->expr==GF_C_STATE) {
            bits=0;
            for(u32 j=0;j<row->width;j++)bits|=(u32)((const u8 *)&mode)[row->source+j]<<(8*j);
            for(u32 j=0;j<C_COUNT(c_fields);j++)if(c_fields[j].offset==row->source) {
                expr=GF_C_EXPR_SAVED;source=keys[j];producer=producers[j];
                keep=c_fields[j].keep;add=c_fields[j].add;bits=c_fields[j].bits;
            }
        } else if(row->expr==GF_C_LIVE) expr=GF_C_EXPR_LIVE;
        else if(row->expr==GF_C_PLL) {
            const struct gfinit_pll_write *w=&mode.sequence.writes[row->source];
            if(w->reg!=row->reg || w->source!=row->reg) return -1;
            keep=w->keep;bits=w->value;expr=GF_C_EXPR_LIVE;
            if(row->source==C_PLL_RESTORE) {expr=GF_C_EXPR_SAVED;producer=pll_first;}
        }
        u32 id=out->plan.node_count;
        struct recover_plan_arg a[]={{REC_IMM,key},{REC_IMM,bits},
            {REC_IMM,keep},{REC_IMM,add},{REC_IMM,bits},{REC_IMM,mask}};
        if(expr!=RECOVER_NONE) {
            a[1]=(struct recover_plan_arg){REC_ARG_READ,source};
            if(expr==GF_C_EXPR_SAVED) {
                u32 slot=out->plan.capture_count++;
                a[1]=(struct recover_plan_arg){REC_ARG_SAVED,p->count+slot};
                d->captures[slot]=(struct recover_capture){p->count+slot,producer,id,source,0};
            }
        }
        c_rule(&b,row->id,key,row->id,bits,expr,row->verify);
        c_layout(&b,row->id,expr==RECOVER_NONE?2:6,
            expr==RECOVER_NONE?c_imm:expr==GF_C_EXPR_LIVE?c_live:c_saved,
            p->targets[key].space==REC_VGA_INDEXED?REC_DATA:REC_WRITE,0);
        c_emit(&b,row->id,row->id,expr,a,expr==RECOVER_NONE?2:6);
        if(row->expr==GF_C_PLL && row->source==0)pll_first=id;
        if(branch!=RECOVER_NONE) {
            c_wait(&b,GF_C_FP_WAIT_MS);
            d->nodes[branch].next[0]=out->plan.node_count;
        }
        if(i==C_ROW_DPMS_OFF_WAIT || i==C_ROW_DPMS_ON_WAIT)c_wait(&b,GF_C_DPMS_WAIT_MS);
    }
    c_marker(&b,GF_C_CHECK_OUT);c_check_out(&b,&mode,producers,keys,pll_first);
    if(out->c_check_count>GF_C_EXIT_ROUTE_SLOT)return -1;
    u32 last=out->plan.node_count-1;
    d->nodes[last].next[0]=d->nodes[last].next[1]=RECOVER_NONE;
    p->vram_reserved=(struct recover_range){0,GF_MODE_PITCH*GF_MODE_HEIGHT};
    p->cold_required=true;
    p->crc=recover_profile_crc(p);out->plan.profile_crc=p->crc;
    return 0;
#endif
}

int gfinit_plan_c_control(struct gfinit_plan *out, struct recover *r)
{
    if(!out || r->plan!=&out->plan || !r->entered || r->cursor>=out->plan.node_count)return -1;
    const struct recover_plan_node *n=&out->plan.nodes[r->cursor];
    if(n->phase!=REC_MODE_C || n->opcode!=GF_C_OPCODE)return -1;
    if(n->substep==GF_C_WAIT) {
        u32 ignored;return recover_plan_access(r,&ignored);
    }
    if(n->substep!=GF_C_CHECK && n->substep!=GF_C_CHECK_SAVED)return -1;
    u32 slot=out->plan.args[n->arg_first+REC_CHECK_SLOT].value,actual=RECOVER_NONE;
    if(slot>=GF_C_EXIT_ROUTE_SLOT)return -1;
    int rc=recover_plan_access(r,&actual);
    out->c_checks[slot]=(struct gfinit_c_check_result){actual,!rc || r->reason==REC_VERIFY};
    return rc;
}

/* Internal finite operation only: op13 and the other C barriers stay closed.
 * DESIGN §§3-4: all writes, safe GPU read to drain posted writes, all reads.
 * Row data comes from the stage-1 generator; no frame-sized image or bulk API. */
int gfinit_plan_c_vram(void *bus, struct recover *r, bool write,
                       u32 *bad_offset, u32 *expected, u32 *actual)
{
    struct gfinit_bus *b = bus;
    u8 row[GF_MODE_WIDTH];
    for (u32 y = 0; y < GF_MODE_HEIGHT; y++) {
        if (recover_safe_point(r) || gfinit_mode_row(y,row) ||
            gfinit_bus_mode_row(b,y,write,row,bad_offset,expected,actual)) return -1;
    }
    if (recover_safe_point(r)) return -1;
    if (write) {
        /* Existing A4 read-safe PFB amount descriptor; no new MMIO permission. */
        bool drained = false;
        for (u32 key = 0; key < b->profile->target_count; key++) {
            const struct recover_target *t = &b->profile->targets[key];
            if (t->space != REC_BAR_MMIO || t->bar || t->offset != GF_PLAN_PFB_AMOUNT ||
                t->width != GF_MODE_VRAM_WORD_BYTES || !t->read_safe) continue;
            struct recover_bus_access a;
            u32 value;
            if (recover_target_address(t,REC_READ,&a)) return -1;
            a.key = key;
            if (gfinit_bus_read(b,&a,&value)) return -1;
            drained = true;
            break;
        }
        if (!drained) return -1;
    }
    return *bad_offset == RECOVER_NONE ? 0 : -1;
}

/* Only the fixed stage-1 AR and palette rows are reachable through this path. */
int gfinit_plan_c_color(void *bus, struct recover *r, u32 ordinal, u32 value)
{
    const struct recover_gf_c_row *row = &recover_gf_c_rows[ordinal];
    if (ordinal == GF_MODE_ROW_DAC) {
        if (row->kind != GF_C_DAC_MASK || row->bits != value ||
            recover_gf_c_rows[ordinal+1].kind != GF_C_DAC_INDEX ||
            recover_gf_c_rows[ordinal+2].kind != GF_C_DAC_DATA) return -1;
        return gfinit_bus_mode_dac(bus,r);
    }
    if (row->kind != GF_C_AR && row->kind != GF_C_AR_GATE) return -1;
    return gfinit_bus_mode_ar(bus,row->kind == GF_C_AR, row->index, value);
}
