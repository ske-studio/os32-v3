/* Fixed C composition, GF6200_4C_DESIGN §§3-4 and stage-2a decisions.
 * Hardware values/order come from the stage-1 table (nouveau MIT references
 * there); this file only lowers it to existing recover accesses/captures. */
#include "gfinit_plan_c.h"
#include "gfinit_bus.h"
#include "recover_gf_c.h"
#include "gf_stage_state.h"
#if !GF_VPLL_PROBE
#define C_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define C_OFF(f) __builtin_offsetof(struct gfinit_mode, f)
#define C_EXTRA_NODES 540U
#define C_EXTRA_ARGS 2100U
#define C_EXTRA_LAYOUTS 194U
#define C_EXTRA_RULES 260U
/* Preserve the old target reservation after dropping its duplicate table. */
#define C_EXTRA_TARGETS 107U
#define C_EXTRA_CAPTURES 13U
#define C_PLL_RESTORE 4U
#define C_GPIO_EXT 0x60081cU
#define C_CRTC850 0x600850U
#define C_SEL_CLK 0x680524U
#define C_PLL_SELECT 0x68050cU
#define C_CR_RPC1_MASK 0xd7U /* CR1A bits 3/5 read back set on NV44A */
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
static const u32 base_kinds[]={REC_IMM,REC_IMM,REC_IMM,REC_IMM,REC_IMM,REC_IMM};
static const u32 c_imm[] = {REC_IMM, REC_IMM};
static const u32 c_live[] = {REC_IMM, REC_ARG_READ, REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static const u32 c_saved[] = {REC_IMM, REC_ARG_SAVED, REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static const u32 c_branch[] = {REC_ARG_READ};
static const u32 c_vram_kinds[] = {REC_IMM, REC_IMM, REC_IMM, REC_IMM};
static const u32 c_check_kinds[] = {REC_IMM,REC_IMM,REC_IMM,REC_IMM,REC_IMM,REC_IMM};
static const u32 c_check_saved_kinds[] = {REC_IMM,REC_IMM,REC_IMM,REC_ARG_SAVED,REC_IMM,REC_IMM};
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
    *v = (a[0] & GF_LOAD_FP_ON_MASK) != 0;
    return 0;
}
struct c_builder {
    struct gfinit_plan *out;
    struct gfinit_decode *d;
    struct recover_profile *p;
    u32 ctx, select_base;
    u32 branch, pll_first, producers[GF_STAGE_FIELDS], keys[GF_STAGE_FIELDS];
    const struct gf_stage_state *baseline;
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
struct c_saved_field { u32 offset, kind, reg, index, width, keep, add, bits; };
#include "gfinit_plan_c/baseline.inc"
#include "gfinit_plan_c/checks.inc"
static int c_load_section(void *saved, u32 section)
{
    struct c_builder *b=saved;
    static const u32 sections[]={GF_C_Q,GF_C_P,GF_C_SD,GF_C_L1,GF_C_L2,GF_C_L3,GF_C_COMMIT};
    if (section==GF_LOAD_SD) {c_marker(b,GF_C_VRAM);c_vram(b);}
    c_marker(b,sections[section]);
    return 0;
}
static int c_load_wait(void *saved, u32 ms, bool fp_only)
{
    struct c_builder *b=saved;
    c_wait(b,ms);
    if (fp_only) b->d->nodes[b->branch].next[0]=b->out->plan.node_count;
    return 0;
}
static int c_load_row(void *saved, const struct gfinit_mode *mode, u32 i)
{
    struct c_builder *b=saved;
    const struct gf_mode_row *row=&gf_mode_rows[i];
    if (row->kind==GF_C_AR || row->kind==GF_C_AR_GATE || row->kind==GF_C_DAC_MASK) {
        u32 value=row->expr==GF_C_STATE ? ((const u8 *)mode)[row->source] : row->bits;
        struct recover_plan_arg a[]={{REC_IMM,i},{REC_IMM,value}};
        c_emit(b,GF_C_COLOR,RECOVER_NONE,RECOVER_NONE,a,C_COUNT(a));
        return 0;
    }
    if (row->kind==GF_C_DAC_INDEX || row->kind==GF_C_DAC_DATA) return 0;
    u32 key=c_key(b,row->kind,row->reg,row->index,row->width);
    b->branch=RECOVER_NONE;
    if(row->flags & REC_GF_C_FP_ON) {
        struct recover_plan_arg a={REC_ARG_READ,key};
        b->branch=c_emit(b,GF_C_BRANCH,RECOVER_NONE,GF_C_EXPR_FP_ON,&a,1);
    }
    c_select(b,key);
    u32 expr=RECOVER_NONE,keep=row->keep,bits=row->bits,add=0;
    u32 source=key,producer=RECOVER_NONE,mask=row->width==1 ? GF_MODE_BYTE_MASK : ~0U;
    if(row->expr==GF_C_STATE) {
        bits=0;
        for(u32 j=0;j<row->width;j++)bits|=(u32)((const u8 *)mode)[row->source+j]<<(8*j);
        for(u32 j=0;j<C_COUNT(c_fields);j++)if(c_fields[j].offset==row->source) {
            expr=GF_C_EXPR_SAVED;source=b->keys[j];producer=b->producers[j];
            keep=c_fields[j].keep;add=c_fields[j].add;bits=c_fields[j].bits;
        }
    } else if(row->expr==GF_C_LIVE) expr=GF_C_EXPR_LIVE;
    else if(row->expr==GF_C_PLL) {
        const struct gfinit_pll_write *w=&mode->sequence.writes[row->source];
        if(w->reg!=row->reg || w->source!=row->reg) return -1;
        keep=w->keep;bits=w->value;expr=GF_C_EXPR_LIVE;
        if(row->source==C_PLL_RESTORE) {expr=GF_C_EXPR_SAVED;producer=b->pll_first;}
    }
    struct gfinit_plan *out=b->out;
    struct gfinit_decode *d=b->d;
    struct recover_profile *p=b->p;
    u32 rule=REC_GF_C_RULE_FIRST+i;
    u32 id=out->plan.node_count;
    struct recover_plan_arg a[]={{REC_IMM,key},{REC_IMM,bits},
        {REC_IMM,keep},{REC_IMM,add},{REC_IMM,bits},{REC_IMM,mask}};
    if(expr!=RECOVER_NONE) {
        a[1]=(struct recover_plan_arg){REC_ARG_READ,source};
        if(expr==GF_C_EXPR_SAVED && b->baseline) {
            expr=GF_C_EXPR_BASE;
            a[1]=(struct recover_plan_arg){REC_IMM,c_baseline(b,source)};
        } else if(expr==GF_C_EXPR_SAVED) {
            u32 slot=out->plan.capture_count++;
            a[1]=(struct recover_plan_arg){REC_ARG_SAVED,p->count+slot};
            d->captures[slot]=(struct recover_capture){p->count+slot,producer,id,source,0};
        }
    }
    c_rule(b,rule,key,rule,bits,expr,row->verify);
    c_layout(b,rule,expr==RECOVER_NONE?2:6,
        expr==RECOVER_NONE?c_imm:expr==GF_C_EXPR_LIVE?c_live:expr==GF_C_EXPR_BASE?base_kinds:c_saved,
        p->targets[key].space==REC_VGA_INDEXED?REC_DATA:REC_WRITE,0);
    c_emit(b,rule,rule,expr,a,expr==RECOVER_NONE?2:6);
    if(row->expr==GF_C_PLL && row->source==0)b->pll_first=id;
    return 0;
}
static const struct gf_load_io c_load_io={c_load_section,c_load_row,c_load_wait};
#endif
static int c_append(struct gfinit_plan *out, struct gfinit_decode *d,
                        struct recover_profile *p, u32 bar1_length,
                        const struct gfinit_pll_limits *limits, const struct gf_stage_state *baseline)
{
#if GF_VPLL_PROBE
    /* Separate diagnostic rules reuse the C ID namespace. Never mix profiles. */
    (void)out;(void)d;(void)p;(void)bar1_length;(void)limits;(void)baseline;
    return -1;
#else
    struct gfinit_mode mode;
    const struct gfinit_mode_saved empty={0};
    if (!out || !d || !p || bar1_length < GF_MODE_PITCH*GF_MODE_HEIGHT ||
        gfinit_mode_calc(limits,&empty,&mode) ||
        mode.pll.error > GF_MODE_CLOCK_KHZ/GF_PLL_MARGIN_DIV) return -1;
    if(out->plan.node_count+C_EXTRA_NODES>RECOVER_NODE_MAX ||
       out->plan.arg_count+C_EXTRA_ARGS>GF_INIT_ARGS ||
       out->plan.capture_count+C_EXTRA_CAPTURES>RECOVER_CAPTURE_MAX ||
       out->plan.entry_count==RECOVER_ENTRY_MAX || out->plan.ctx_count==RECOVER_CTX_MAX ||
       p->target_count+C_EXTRA_TARGETS>GF_PLAN_TARGETS ||
       p->rule_count+C_EXTRA_RULES>GF_PLAN_RULES ||
       p->layout_count+C_EXTRA_LAYOUTS>GF_PLAN_LAYOUTS || p->expr_count+4>GF_PLAN_EXPRS) return -1;
    struct c_builder b={.out=out,.d=d,.p=p,.ctx=out->plan.ctx_count++,
        .select_base=REC_GF_C_RULE_FIRST+gf_mode_row_count,.baseline=baseline,.pll_first=RECOVER_NONE};
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
    out->exprs[p->expr_count++]=(struct recover_expr){GF_C_EXPR_BASE,5,base_kinds+1,c_value};
    c_layout(&b,GF_C_SELECT,2,c_imm,REC_SELECT,0);
    c_layout(&b,GF_C_READ,1,c_imm,REC_READ,0);
    c_layout(&b,GF_C_SECTION,2,c_imm,RECOVER_NONE,0);
    c_layout(&b,GF_C_WAIT,1,c_imm,RECOVER_NONE,REC_LAYOUT_MODE_WAIT);
    c_layout(&b,GF_C_ROUTE_SET,2,c_imm,RECOVER_NONE,REC_LAYOUT_MODE_ROUTE);
    c_layout(&b,GF_C_CHECK,REC_CHECK_ARGS,c_check_kinds,RECOVER_NONE,REC_LAYOUT_MODE_CHECK);
    c_layout(&b,GF_C_CHECK_SAVED,REC_CHECK_ARGS,c_check_saved_kinds,RECOVER_NONE,REC_LAYOUT_MODE_CHECK);
    c_layout(&b,GF_C_BRANCH,1,c_branch,RECOVER_NONE,REC_LAYOUT_CHECK_EXPR);
    c_layout(&b,GF_C_COLOR,2,c_imm,RECOVER_NONE,REC_LAYOUT_MODE_COLOR);
    for(u32 i=0;i<gf_mode_row_count;i++) {
        const struct gf_mode_row *t=&gf_mode_rows[i];
        if(t->kind<=GF_C_GR || t->kind==GF_C_MISC)
            (void)c_key(&b,t->kind,t->reg,t->index,t->width);
    }
    c_marker(&b,GF_C_CHECK_IN);
    c_check_in(&b);
    for(u32 i=0;i<C_COUNT(c_fields);i++) {
        const struct c_saved_field *f=&c_fields[i];
        b.keys[i]=c_key(&b,f->kind,f->reg,f->index,f->width);
        b.producers[i]=c_read(&b,b.keys[i]);
    }
    if (gf_load_state(&mode,&b,&c_load_io)) return -1;
    c_marker(&b,GF_C_R);
    struct recover_plan_arg route[]={{REC_IMM,RECOVER_ROUTE_RGB_IN},{REC_IMM,RECOVER_ROUTE_MASK}};
    c_emit(&b,GF_C_ROUTE_SET,RECOVER_NONE,RECOVER_NONE,route,C_COUNT(route));
    c_marker(&b,GF_C_CHECK_OUT);c_check_out(&b,&mode,b.producers,b.keys,b.pll_first);
    if(out->c_check_count>GF_C_EXIT_ROUTE_SLOT)return -1;
    u32 last=out->plan.node_count-1;
    d->nodes[last].next[0]=d->nodes[last].next[1]=RECOVER_NONE;
    p->vram_reserved=(struct recover_range){0,GF_MODE_PITCH*GF_MODE_HEIGHT};
    p->cold_required=true;
    p->crc=recover_profile_crc(p);out->plan.profile_crc=p->crc;
    return 0;
#endif
}

int gfinit_plan_build_c(struct gfinit_plan *out, struct gfinit_decode *d,
                        struct recover_profile *p, u32 length,
                        const u8 *rom, u32 size)
{
    struct gfinit_pll_limits limits;
    if (gfinit_pll_parse(rom,size,GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&limits) ||
        gfinit_plan_prefix(out,d,p)) return -1;
    return c_append(out,d,p,length,&limits,0);
}
int gfinit_plan_rebuild_c(struct gfinit_plan *out, struct gfinit_decode *d,
                          struct recover_profile *p, const struct gf_stage_state *s)
{
    /* Fresh arrays, retained capacity and initial ROM/PLL identity. No decode. */
    out->plan=(struct recover_plan){.version=1,.rom_crc=s->rom_crc,.rom_size=s->rom_size,
        .nodes=d->nodes,.args=d->args,.captures=d->captures,
        .entries=out->entries,.deps=out->deps};
    for(u32 i=0;i<p->target_count;i++)out->targets[i]=p->targets[i];
    for(u32 i=0;i<p->rule_count;i++)out->rules[i]=p->rules[i];
    for(u32 i=0;i<p->layout_count;i++)out->layouts[i]=p->layouts[i];
    for(u32 i=0;i<p->expr_count;i++)out->exprs[i]=p->exprs[i];
    p->targets=out->targets;p->rules=out->rules;p->layouts=out->layouts;p->exprs=out->exprs;
    return c_append(out,d,p,s->bars[GF_MODE_VRAM_BAR].length,&s->limits,s);
}
int gfinit_plan_baseline(struct gfinit_plan *out, struct recover_profile *p,
                         struct gf_stage_state *s, const u8 *rom, u32 size)
{
#if GF_VPLL_PROBE
    (void)out;(void)p;(void)s;(void)rom;(void)size;
    return 0;
#else
    struct c_builder b={.out=out,.p=p};
    if (gfinit_pll_parse(rom,size,GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&s->limits) ||
        p->target_count+GF_STAGE_VALUES>GF_PLAN_TARGETS) return -1;
    for(u32 i=0;i<C_COUNT(c_fields);i++) {
        const struct c_saved_field *f=&c_fields[i];
        s->keys[i]=c_key(&b,f->kind,f->reg,f->index,f->width);
    }
    s->keys[GF_STAGE_GATE]=c_key(&b,GF_C_MMIO,GF_PLL_GATE,0,4);
    s->keys[GF_STAGE_PMC]=REC_GF_T_PMC_ENABLE;
    s->rom_crc=out->plan.rom_crc;s->rom_size=size;
    return 0;
#endif
}

int gfinit_plan_c_control(struct gfinit_plan *out, struct recover *r)
{
    if(!out || r->plan!=&out->plan || !r->entered || r->cursor>=out->plan.node_count)return -1;
    const struct recover_plan_node *n=&out->plan.nodes[r->cursor];
    if(n->phase!=REC_MODE_C || n->opcode!=GF_C_OPCODE)return -1;
    if(n->substep==GF_C_WAIT || n->substep==GF_C_ROUTE_SET) {
        u32 ignored;return recover_plan_access(r,&ignored);
    }
    if(n->substep!=GF_C_CHECK && n->substep!=GF_C_CHECK_SAVED)return -1;
    u32 slot=out->plan.args[n->arg_first+REC_CHECK_SLOT].value,actual=RECOVER_NONE;
    if(slot>=GF_C_CHECK_MAX)return -1;
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
    /* Completion and mismatch are separate; a later bus error still returns -1. */
    return 0;
}

/* Only the fixed stage-1 AR and palette rows are reachable through this path. */
int gfinit_plan_c_color(void *bus, struct recover *r, u32 ordinal, u32 value)
{
    const struct gf_mode_row *row = &gf_mode_rows[ordinal];
    if (ordinal == GF_MODE_ROW_DAC) {
        if (row->kind != GF_C_DAC_MASK || row->bits != value ||
            gf_mode_rows[ordinal+1].kind != GF_C_DAC_INDEX ||
            gf_mode_rows[ordinal+2].kind != GF_C_DAC_DATA) return -1;
        return gfinit_bus_mode_dac(bus,r);
    }
    if (row->kind != GF_C_AR && row->kind != GF_C_AR_GATE) return -1;
    return gfinit_bus_mode_ar(bus,row->kind == GF_C_AR, row->index, value);
}
