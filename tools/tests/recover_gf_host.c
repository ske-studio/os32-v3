/* Real recover checker with the GF table; synthetic layouts only (S2 owns ROM
 * layouts). No physical I/O. ILP32 qemu runner. */
#include "../../kdebug/recover.c"
#include "../../kdebug/recover_gf.c"
#include "gfinit_fixture.h"
#include "recover_gf_inventory.h"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static unsigned failures;
static void print(const char *s)
{
    unsigned n = 0; while (s[n]) n++;
    u32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { print("FAIL " #x "\n"); failures++; } } while (0)
static struct gfinit_plan gf_fixture_buffer;
static struct recover_profile gf_fixture_profile;
static void gf_fixture_init(void)
{
    CHECK(!recover_gf_template.rules && !recover_gf_template.rule_count &&
          !recover_gf_template.targets && !recover_gf_template.target_count &&
          !recover_gf_template.layouts && !recover_gf_template.layout_count);
    u8 *bytes = (u8 *)&gf_fixture_buffer;
    for (u32 i = 0; i < sizeof(gf_fixture_buffer); i++) bytes[i] = 0xa5;
    CHECK(recover_gf_profile_expand(&gf_fixture_profile, &gf_fixture_buffer) == 0);
    CHECK(gf_fixture_profile.rules == gf_fixture_buffer.rules &&
          gf_fixture_profile.targets == gf_fixture_buffer.targets &&
          gf_fixture_profile.layouts == gf_fixture_buffer.layouts);
    /* Only the occupied parts of the three arrays may be changed. */
    for (u32 i = 0; i < sizeof(gf_fixture_buffer); i++) {
        u8 *at = bytes + i;
        if ((at >= (u8 *)gf_fixture_buffer.rules &&
             at < (u8 *)(gf_fixture_buffer.rules + gf_fixture_profile.rule_count)) ||
            (at >= (u8 *)gf_fixture_buffer.targets &&
             at < (u8 *)(gf_fixture_buffer.targets + gf_fixture_profile.target_count)) ||
            (at >= (u8 *)gf_fixture_buffer.layouts &&
             at < (u8 *)(gf_fixture_buffer.layouts + gf_fixture_profile.layout_count))) continue;
        CHECK(*at == 0xa5);
    }
    struct recover_profile reset = gf_fixture_profile;
    CHECK(recover_gf_profile_expand(&reset, 0) == 0);
    CHECK(!reset.rules && !reset.rule_count && !reset.targets && !reset.target_count &&
          !reset.layouts && !reset.layout_count && reset.exprs == gf_fixture_profile.exprs);
    u32 crc = recover_profile_crc(&gf_fixture_profile);
    gf_fixture_buffer.rules[0].write_mask ^= 1;
    gf_fixture_buffer.layouts[gf_fixture_profile.layout_count-1].substep ^= 1;
    gf_fixture_buffer.targets[gf_fixture_profile.target_count-1].offset ^= 1;
    gf_fixture_profile.rule_count++;
    CHECK(recover_gf_profile_expand(&gf_fixture_profile, &gf_fixture_buffer) == 0);
    CHECK(recover_profile_crc(&gf_fixture_profile) == crc);
}
static struct recover core;
static struct recover_profile profile;
static u32 flow[RECOVER_NODE_MAX * RECOVER_BITS_WORDS], valid[RECOVER_BITS_WORDS];
static struct recover_plan_buffers buffers = {flow, valid};
static u32 values[RECOVER_TARGET_MAX], indices[RECOVER_TARGET_MAX];
static u32 n1_completed, writes, cleanup_calls, data_writes, inject_key, inject_when, n1key;
static struct recover_identity ident = {.boot=1, .image_version=1, .kdebug_version=1,
    .bdf=13, .device=0x022110de, .bar0=0x24000000};
static u32 rawcommand;
static u32 rawbars[] = {0x24000000, 0x30000008, 0x25000000};
static u32 tick(void *v) { (void)v; return 0; }
static int identity(void *v, struct recover_identity *id) { (void)v; *id=ident; return 0; }
static int ok(void *v) { (void)v; return 0; }
static int raw(void *v, u32 *command, u32 bar[3])
{ (void)v; *command=rawcommand; for (u32 i=0;i<3;i++) bar[i]=rawbars[i]; return 0; }
static int read_fake(void *v, const struct recover_bus_access *a, u32 *value)
{
    (void)v;
    *value=a->dir_substep==REC_SELECT ? indices[a->key] : values[a->key];
    return 0;
}
static int write_fake(void *v, const struct recover_bus_access *a, u32 value)
{
    (void)v; writes++;
    if(a->space==REC_PCI_CFG && a->offset==4) rawcommand=value;
    if(a->space==REC_PCI_CFG && a->offset>=0x10 && a->offset<=0x18) {
        u32 bar=(a->offset-0x10)/4;
        rawbars[bar]=value;
        if(value==~0U) {
            values[a->key]=0xff000000U | (bar==1 ? 8 : 0);
            return 0;
        }
    }
    if (a->dir_substep==REC_SELECT) indices[a->key]=value;
    else {
        const struct recover_target *t=&profile.targets[a->key];
        values[a->key]=a->dir_substep==REC_DATA && t->space==REC_VGA_INDEXED &&
            t->port==RECOVER_CRTC && t->index==RECOVER_CR1F ?
            (value==RECOVER_CR1F_UNLOCK?RECOVER_CR1F_UNLOCKED:0):value;
    }
    if (a->key==n1key && a->dir_substep==REC_DATA) {
        data_writes++;
        if (inject_when==REC_AFTER) values[inject_key]=1;
    }
    /* cfg and PBUS are separate reads, but writes affect the same device. */
    if (a->key==2) values[0]=value;
    if (a->key==3) values[1]=value;
    return 0;
}
static int route_read_fake(void *v,u32 *out) { (void)v; *out=0; return 0; }
static int route_write_fake(void *v,u32 value) { (void)v; (void)value; return 0; }
static int abort_fake(void *v,u32 session) { (void)v; (void)session; return 0; }
static int cleanup_fake(void *v) { (void)v; cleanup_calls++; return 0; }
static int event_fake(void *v,enum recover_state s,u32 session)
{ (void)v; (void)s; (void)session; return 0; }
static const struct recover_ops ops = {tick,identity,ok,raw,read_fake,write_fake,
    route_read_fake,route_write_fake,ok,ok,abort_fake,cleanup_fake,event_fake,0,0,0};
static const u32 four[] = {REC_IMM,REC_ARG_READ,REC_IMM,REC_IMM};
static const u32 two[] = {REC_IMM,REC_IMM};
static const struct recover_opcode_layout layouts[] = {
    {256,0,0,0,RECOVER_NONE,0},
    {256,REC_WRITE,2,two,REC_WRITE,0},
    {257,REC_SELECT,2,two,REC_SELECT,0},
    {257,REC_DATA,2,two,REC_DATA,0},
};
static struct recover_plan_node nodes[8];
static struct recover_plan_arg args[16];
static struct recover_plan_entry entries[5];
static struct recover_plan_dep links[2];
static struct recover_plan plan;
static u32 hash;
static void setup(void)
{
    rawcommand=0x102; rawbars[0]=0x24000000; rawbars[1]=0x30000008; rawbars[2]=0x25000000;
    profile=gf_fixture_profile;
    profile.trial_limit=1000; profile.opcode_limit=1000; profile.restore_limit=1000;
    profile.max_steps=4096; profile.max_polls=65536; profile.restore_max_polls=65536;
    profile.layouts=layouts; profile.layout_count=ARRAY_SIZE(layouts);
    profile.crc=recover_profile_crc(&profile);
    recover_init(&core,&ops,0,true,false);
    core.cache=(struct recover_bar_measurement){.identity=ident,.measurement_session=1};
    for(u32 i=0;i<3;i++) core.cache.bar[i]=(struct recover_bar_extent){
        rawbars[i],rawbars[i]&~15U,0x1000000,1};
    for(u32 i=0;i<RECOVER_TARGET_MAX;i++) values[i]=indices[i]=0;
    for(u32 i=0;i<3;i++) values[7+i]=rawbars[i];
    n1_completed=writes=cleanup_calls=data_writes=0; inject_key=RECOVER_NONE; inject_when=0;
}
static void inventory_test(void)
{
    CHECK(recover_profile_ok(&gf_fixture_profile)==0);
    CHECK(ARRAY_SIZE(inventory)==619 && ARRAY_SIZE(gf_effects)==619);
    u32 counts[5]={0};
    for(u32 i=0;i<ARRAY_SIZE(inventory);i++) {
        const struct gf_classification *c=&inventory[i];
        const struct gf_effect *e=&gf_effects[i];
        CHECK(c->effect==i && c->kind<5); counts[c->kind]++;
        if(c->kind==REC_GF_PLL || c->kind==REC_GF_PLL_UNUSED) {
            CHECK(e->expression==GF_PLL);
            if(c->kind==REC_GF_PLL) {
                const struct recover_rule *w=rule_by_id(&gf_fixture_profile,c->rule);
                CHECK(w && w->phase==REC_RUN && w->pc==e->pc && gf_fixture_profile.targets[w->key].offset==e->reg);
            } else CHECK(c->rule==RECOVER_NONE &&
                ((e->pc==0xe728 && (e->reg==0x4038 || e->reg==0x403c || e->reg==0x4600)) ||
                 (e->pc==0xe623 && e->reg==0x1584)));
            continue;
        }
        if(c->kind!=REC_GF_RULE) {
            const struct recover_rule *w=rule_by_id(&gf_fixture_profile,c->rule);
            CHECK(w && w->phase==REC_RUN && w->pc==e->pc && w->substep==i);
            CHECK(w && w->value.mode==REC_EXPR &&
                (c->kind==REC_GF_COPY ? (w->value.expr_id==REC_GF_EXPR_COPY || w->value.expr_id==REC_GF_EXPR_COPY_NV) :
                 w->value.expr_id==REC_GF_SAVED_BAR));
            continue;
        }
        const struct recover_rule *w=rule_by_id(&gf_fixture_profile,c->rule);
        CHECK(w!=0); if(!w) continue;
        const struct recover_target *t=&gf_fixture_profile.targets[w->key];
        struct recover_bus_access bus;
        u32 dir=e->space ? (e->reg%2 ? REC_DATA : REC_SELECT) : REC_WRITE;
        CHECK(recover_target_address(t,dir,&bus)==0);
        CHECK(bus.offset==e->reg && bus.width==e->width && t->head==e->head);
        CHECK(w->pc==e->pc && w->phase==REC_RUN && w->substep==i);
        CHECK(e->expression==GF_CONSTANT || e->expression==GF_SELECT);
        if(e->space) CHECK(t->port==e->port && t->index==e->index);
        u32 full=width_mask(e->width), mask=(~e->keep|e->value)&full;
        if(e->reg==0x1850) { CHECK(w->value.mode==REC_LITERAL && w->value.bits==0); continue; }
        CHECK(w->write_mask==mask && w->value.mask==mask && w->value.bits==e->value);
        for(u32 j=0;j<4;j++) {
            u32 old=(j==0 ? 0 : j==1 ? full : j==2 ? 0x55555555U : 0xaaaaaaaaU)&full;
            u32 next=(old&e->keep)|e->value;
            CHECK(value_ok(&w->value,t->width,next));
            CHECK(((old^next)&~w->write_mask)==0);
            if(mask) { u32 bit=mask&(~mask+1); CHECK(!value_ok(&w->value,t->width,next^bit)); }
        }
        if(e->flags&GF_N1_CFG_ZERO) {
            CHECK(w->flags&REC_N1); CHECK(w->main_id==3 && w->pc==0xe81e);
            CHECK(w->guard_count==4 && w->requires_count==3);
            for(u32 j=0;j<4;j++) CHECK(w->guards[j].key==j &&
                w->guards[j].mask==~0U && w->guards[j].expected==0 &&
                w->guards[j].when==(REC_BEFORE|REC_AFTER));
        }
    }
    CHECK(counts[0]==513 && counts[1]==76 && counts[4]==25 && counts[2]==2 && counts[3]==3);
    CHECK(gf_fixture_profile.vram_reserved.base==0 && gf_fixture_profile.vram_reserved.len==1024);
    for(u32 i=0;i<gf_fixture_profile.target_count;i++) {
        const struct recover_target *t=&gf_fixture_profile.targets[i];
        if(t->space==REC_BAR_MMIO && t->offset!=0x1700) CHECK(t->access!=REC_RW);
    }
    for(u32 i=1;i<REC_GF_PREPARE_COUNT;i++) {
        const struct recover_rule *w=rule_by_id(&profile,recover_gf_prepare[i]);
        CHECK(w && w->requires_count==1 && w->requires[0]==recover_gf_prepare[i-1]);
    }
    for(u32 i=1;i<REC_GF_SIZE_BARS_COUNT;i++) {
        const struct recover_rule *w=rule_by_id(&profile,recover_gf_size_bars[i]);
        CHECK(w && w->requires_count==1 && w->requires[0]==recover_gf_size_bars[i-1]);
    }
    print("inventory: 513 rules, 76 PLL covered / 25 chip-excluded / 0 pending, 2 COPY, 3 temporary saved, 0 alias gap; 619 accounted\n");
}
static void node(u32 at,const struct recover_rule *w,u32 dir,u32 value)
{
    nodes[at]=(struct recover_plan_node){.phase=w->phase,.main_id=w->main_id,
        .pc=w->pc,.opcode=dir==REC_WRITE?256:257,.substep=w->substep,.ctx=0,
        .rule_id=w->id,.expr_id=RECOVER_NONE,.arg_first=2*at,.arg_count=2,
        .next={at+1,at+1}};
    args[2*at]=(struct recover_plan_arg){REC_IMM,w->key};
    args[2*at+1]=(struct recover_plan_arg){REC_IMM,value};
}
/* Exact GF rules, only opcode layouts adapted to their source-row substeps. */
static struct recover_opcode_layout trace_layouts[6];
static void trace_plan(void)
{
    setup();
    const struct recover_rule *n1=0,*sel=0;
    for(u32 i=0;i<profile.rule_count;i++) if(gf_fixture_profile.rules[i].flags&REC_N1) n1=&gf_fixture_profile.rules[i];
    CHECK(n1); if(!n1) return;
    sel=rule_by_id(&profile,n1->requires[0]); n1key=n1->key; values[n1key]=0x25;
    node(0,&gf_fixture_profile.rules[0],REC_WRITE,0); node(1,&gf_fixture_profile.rules[1],REC_WRITE,0);
    entries[0]=(struct recover_plan_entry){REC_PREPARE,RECOVER_NONE,0};
    for(u32 i=0;i<3;i++) {
        nodes[2+i]=(struct recover_plan_node){.phase=REC_RUN,.main_id=i,.pc=0x100+i,
            .opcode=256,.substep=0,.rule_id=RECOVER_NONE,.expr_id=RECOVER_NONE,
            .next={3+i,3+i}};
        entries[1+i]=(struct recover_plan_entry){REC_RUN,i,2+i};
    }
    node(5,sel,REC_SELECT,1); node(6,n1,REC_DATA,5);
    nodes[6].expr_id=REC_GF_RMW; nodes[6].arg_count=4;
    args[13]=(struct recover_plan_arg){REC_ARG_READ,n1key};
    args[14]=(struct recover_plan_arg){REC_IMM,0xdf};
    args[15]=(struct recover_plan_arg){REC_IMM,0};
    nodes[7]=(struct recover_plan_node){.phase=REC_RUN,.main_id=3,.pc=0xe824,
        .opcode=256,.rule_id=RECOVER_NONE,.expr_id=RECOVER_NONE,
        .next={RECOVER_NONE,RECOVER_NONE}};
    entries[4]=(struct recover_plan_entry){REC_RUN,3,5};
    links[0]=(struct recover_plan_dep){6,gf_fixture_profile.rules[0].id,0};
    links[1]=(struct recover_plan_dep){6,gf_fixture_profile.rules[1].id,1};
    trace_layouts[0]=layouts[0]; trace_layouts[1]=layouts[1];
    trace_layouts[2]=(struct recover_opcode_layout){257,sel->substep,2,two,REC_SELECT,0};
    trace_layouts[3]=(struct recover_opcode_layout){257,n1->substep,4,four,REC_DATA,0};
    profile.layouts=trace_layouts; profile.layout_count=4;
    profile.crc=recover_profile_crc(&profile);
    plan=(struct recover_plan){.version=1,.profile_crc=profile.crc,.nodes=nodes,
        .node_count=8,.args=args,.arg_count=16,.ctx_count=1,.deps=links,.dep_count=2,
        .entries=entries,.entry_count=5};
}
static int run(void *unused);
static int operation(struct recover *r,void *arg)
{
    (void)arg;
    for(u32 i=0;i<8;i++) {
        if(recover_plan_enter(r,i)) return -1;
        if(i==6 && inject_when==REC_BEFORE) values[inject_key]=1;
        if(nodes[i].rule_id!=RECOVER_NONE) {
            const struct recover_opcode_layout *l=layout_for(&profile,&nodes[i]);
            u32 key=args[2*i].value, value=i==6 ? 5 : args[2*i+1].value;
            if(recover_access(r,i,l->dir_substep,key,gf_fixture_profile.targets[key].width,&value)) return -1;
            if(i==6) n1_completed++;
        }
        if(recover_plan_leave(r,0)) return -1;
    }
    return 0;
}
static int run(void *unused)
{
    (void)unused;
    if(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)) return -2;
    if(trial_begin(&core,&profile,hash,1000)) return -3;
    return trial_step(&core,&core.token,true,operation,0);
}
static int size_operation(struct recover *r,void *arg)
{
    (void)arg;
    for(u32 i=0;i<plan.node_count;i++) {
        const struct recover_plan_node *n=&plan.nodes[i];
        const struct recover_opcode_layout *l=layout_for(&profile,n);
        u32 key=plan.args[n->arg_first].value, value=0;
        if(recover_plan_enter(r,i)) return -1;
        if(n->rule_id!=RECOVER_NONE) {
            const struct recover_rule *w=rule_by_id(&profile,n->rule_id);
            if(!evaluate(r,n,w,0,&value)) return -1;
        }
        if(recover_access(r,i,l->dir_substep,key,gf_fixture_profile.targets[key].width,&value)) return -1;
        if(l->dir_substep==REC_READ) {
            u32 bar=key-7;
            if(recover_bar_probe(r,bar,r->bars[bar].raw,value,0,0,0,0)) return -1;
        }
        if(recover_plan_leave(r,0)) return -1;
    }
    return 0;
}
static int prepare_operation(struct recover *r,void *arg)
{
    (void)arg;
    for(u32 i=0;i<plan.node_count;i++) {
        const struct recover_plan_node *n=&plan.nodes[i];
        const struct recover_opcode_layout *l=layout_for(&profile,n);
        u32 key=plan.args[n->arg_first].value;
        u32 value=plan.args[n->arg_first+1].value;
        if(n->expr_id==REC_GF_PMC_ENABLE) value=values[key]|1U;
        if(n->expr_id==REC_GF_CR11_UNLOCK) value=values[key]&0x7fU;
        if(recover_plan_enter(r,i) ||
            recover_access(r,i,l->dir_substep,key,gf_fixture_profile.targets[key].width,&value) ||
            recover_plan_leave(r,0)) return -1;
    }
    return 0;
}
static void fixed_test(void)
{
    setup();
    profile.layouts=gf_fixture_profile.layouts;
    profile.layout_count=gf_fixture_profile.layout_count;
    profile.crc=recover_profile_crc(&profile);
    plan=recover_gf_size_bars_plan; plan.profile_crc=profile.crc;
    CHECK(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)==0);
    CHECK(writes==0);
    CHECK(trial_begin(&core,&profile,hash,1000)==0);
    CHECK(trial_step(&core,&core.token,true,size_operation,0)==0);
    CHECK(rawcommand==0x102 && rawbars[0]==0x24000000 &&
        rawbars[1]==0x30000008 && rawbars[2]==0x25000000);
    CHECK(trial_end(&core,&core.token,REC_NORMAL)==0);
    CHECK(core.cache.bar[0].valid && core.cache.bar[1].valid && core.cache.bar[2].valid);
    setup();
    profile.layouts=gf_fixture_profile.layouts;
    profile.layout_count=gf_fixture_profile.layout_count;
    profile.crc=recover_profile_crc(&profile);
    plan=recover_gf_prepare_plan; plan.profile_crc=profile.crc;
    CHECK(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)==0 && writes==0);
    for(u32 i=0;i<plan.node_count;i++) CHECK(node_ok(&profile,&plan,i));
    values[12]=0xa5;
    CHECK(trial_begin(&core,&profile,hash,1000)==0);
    CHECK(trial_step(&core,&core.token,true,prepare_operation,0)==0);
    CHECK(values[12]==0x25 && indices[12]==0x11);
    CHECK(trial_end(&core,&core.token,REC_NORMAL)==0);
    print("fixed: SIZE_BARS bind/probe/restore PASS; PREPARE indexed READ bind/execute PASS\n");
}
static void trace_test(void)
{
    trace_plan(); CHECK(run(0)==0); CHECK(data_writes==1 && values[n1key]==5);
    CHECK(trial_end(&core,&core.token,REC_NORMAL)==0);
    CHECK(core.s0_verified && !core.s1_verified && core.cold_latched);
    CHECK(cleanup_calls==1);
    for(u32 when=REC_BEFORE;when<=REC_AFTER;when++) for(u32 key=0;key<4;key++) {
        trace_plan(); inject_when=when; inject_key=key;
        CHECK(run(0)<0); CHECK(cleanup_calls==1 && n1_completed==0);
        CHECK(data_writes==(when==REC_AFTER)); CHECK(core.state==REC_FAILED);
        u32 before=writes, value=0;
        CHECK(recover_access(&core,7,REC_WRITE,2,4,&value)<0 && writes==before);
    }
    trace_plan();
    plan.node_count=1; plan.entry_count=1; plan.dep_count=0;
    nodes[0].rule_id=RECOVER_NONE; /* W layout with no classified rule */
    nodes[0].next[0]=nodes[0].next[1]=RECOVER_NONE;
    CHECK(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)<0 && writes==0);
    for(u32 bad=0;bad<6;bad++) {
        trace_plan();
        if(bad==0) nodes[6].rule_id=RECOVER_NONE;
        if(bad==1) args[13].value=0x20;
        if(bad==2) nodes[6].pc++;
        if(bad==3) nodes[6].main_id=2;
        if(bad==4) links[0].producer=7;
        if(bad==5) args[10].value=3; /* wrong alias target */
        CHECK(run(0)==-2 && writes==0 && cleanup_calls==0);
    }
    print("trace: N1 normal; 8 pre/post failures; unknown/value/pc/main/dependency/alias deny\n");
}
#include "recover_cleanup_host.inc"

void _start(void)
{
    gf_fixture_init();
    setup(); inventory_test(); fixed_test(); trace_test(); cleanup_envelope_tests();
    print(failures?"GF profile FAIL\n":"GF profile PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failures?1:0));
    __builtin_unreachable();
}
