/* S1 script bytes plus synthetic support tables, never a real ROM. */
static unsigned lookup_steps;
#define RECOVER_HOST_LOOKUP() (++lookup_steps)
#define _start gf_profile_entry
#include "recover_gf_host.c"
#undef _start
#include "../../kdebug/gfinit_pll.c"
#include "../../kdebug/gfinit_decode.c"
static struct gfinit_decode decoded;
static u8 rom[65536];
#define output print
static void number(u32 x)
{
    char b[16]; u32 n=0;
    do {b[n++]=(char)('0'+x%10);x/=10;} while(x);
    while(n) {char a[2]={b[--n],0};output(a);}
}
static void put(u32 at,u32 value,u32 width)
{for(u32 i=0;i<width;i++)rom[at+i]=(u8)(value>>(8*i));}
static void fixture(void)
{
    for(u32 i=0;i<sizeof(rom);i++)rom[i]=0;
    for(u32 i=0;i<GF_ARRAY(gf_instructions);i++) {
        const struct gf_instruction *v=&gf_instructions[i];
        for(u32 j=0;j<v->length;j++)rom[v->pc+j]=v->bytes[j];
    }
    for(u32 i=0;i<GF_ARRAY(gf_scripts);i++)
        if(gf_scripts[i].script<9)put(0x5d4c+2*gf_scripts[i].script,gf_scripts[i].start,2);
    /* Synthetic 0x21 limits; no real VBIOS table bytes. */
    put(GF_INIT_PLL_TABLE,0x21,1);put(GF_INIT_PLL_TABLE+1,4,1);
    put(GF_INIT_PLL_TABLE+2,35,1);put(GF_INIT_PLL_TABLE+3,4,1);
    const u32 pll_regs[]={GF_PLL_CORE-4,GF_PLL_MEMORY-4,GF_PLL_VIDEO,0x680520};
    for(u32 i=0;i<4;i++) {
        u32 at=GF_INIT_PLL_TABLE+4+35*i;
        put(at,pll_regs[i],4);put(at+4,100,2);put(at+6,500,2);
        put(at+12,1,2);put(at+16,27,2);
        put(at+20,1,1);put(at+21,255,1);put(at+22,1,1);put(at+23,27,1);
        put(at+29,7,1);put(at+30,1,1);
    }
    for(u32 i=0;i<8;i++) {
        put(0x5daa+9*i,0x3d4,2);put(0x5dac+9*i,0x3c,1);
        put(0x5daf+9*i,0x5c59,2);
    }
    for(u32 i=0;i<3;i++)put(0x5d86+12*i,0x101000,4);
    for(u32 i=0;i<GF_ARRAY(gf_instructions);i++) {
        const struct gf_instruction *v=&gf_instructions[i];
        if(v->opcode!=0x6f)continue;
        for(u32 j=0;j<GF_ARRAY(gf_effects);j++)if(gf_effects[j].pc==v->pc) {
            put(0x5d66+8*v->bytes[1],gf_effects[j].reg,4);
            put(0x5d6a+8*v->bytes[1],gf_effects[j].value,4);break;
        }
    }
}
static void parser_tests(void)
{
    struct gfinit_instruction ins;
    u8 b[64]={0};
    b[0]=0xff; CHECK(gfinit_decode_instruction(b,sizeof(b),0,&ins)<0);
    b[0]=0x33;
    for(u32 n=0;n<256;n++) {
        b[1]=n;
        CHECK((gfinit_decode_instruction(b,sizeof(b),0,&ins)==0)==(n>0 && n<255));
    }
    for(u32 i=0;i<64;i++)b[i]=0;
    b[0]=0x37;
    for(u32 shift=0;shift<256;shift++) {
        b[5]=shift;
        CHECK((gfinit_decode_instruction(b,sizeof(b),0,&ins)==0)==(shift<32 || shift>224));
    }
    for(u32 i=0;i<64;i++)b[i]=0;
    b[0]=0x32;b[5]=4;b[4]=0x70;
    for(u32 n=0;n<=9;n++) {
        b[6]=n;
        CHECK((gfinit_decode_instruction(b,sizeof(b),0,&ins)==0)==(n>=8));
    }
    b[0]=0x58;b[1]=0xfc;b[2]=b[3]=b[4]=255;b[5]=2;
    CHECK(gfinit_decode_instruction(b,sizeof(b),0,&ins)<0);
    fixture();
    for(u32 i=0;i<GF_ARRAY(gf_instructions);i++) {
        const struct gf_instruction *v=&gf_instructions[i];
        CHECK(gfinit_decode_instruction(rom,sizeof(rom),v->pc,&ins)==0);
        CHECK(ins.pc==v->pc && ins.length==v->length && ins.opcode==v->opcode);
        for(u32 len=0;len<v->length;len++) CHECK(gfinit_decode_instruction(rom,v->pc+len,v->pc,&ins)<0);
    }
    rom[0]=0x6f;rom[1]=4;CHECK(gfinit_decode_instruction(rom,sizeof(rom),0,&ins)<0);
    rom[0]=0x75;rom[1]=3;CHECK(gfinit_decode_instruction(rom,sizeof(rom),0,&ins)<0);
    rom[0]=0x39;rom[1]=8;CHECK(gfinit_decode_instruction(rom,sizeof(rom),0,&ins)<0);
    rom[0]=0x6b;rom[1]=5;CHECK(gfinit_decode_instruction(rom,sizeof(rom),0,&ins)<0);
    fixture();rom[0xe81e + 5]^=1;
    CHECK(gfinit_decode_rom(rom,sizeof(rom),&recover_gf_template,&decoded)<0);
    fixture(); CHECK(gfinit_decode_rom(rom,0xe832,&recover_gf_template,&decoded)<0);
    u32 a[]={0xab,0x0c,0xfe,0xf0,0x0f}, result;
    CHECK(gf_copy(a,5,&result)==0 && result==0x3b);
    a[2]=32;CHECK(gf_copy(a,5,&result)<0);
    u32 nv[]={0x12345678,0xa5a5a5a5,4,0xffff,0x100,0xffff0000};
    CHECK(gf_copy_nv(nv,6,&result)==0 && result==0xa5a54467);
    nv[2]=0xff;CHECK(gf_copy_nv(nv,6,&result)==0 && result==0xa5a5adf0);
}
static void decode_inventory(void)
{
    fixture(); CHECK(gfinit_decode_rom(rom,sizeof(rom),&recover_gf_template,&decoded)==0);
    CHECK(decoded.instruction_count==193 && decoded.plan.node_count==972);
    CHECK(decoded.plan.arg_count<=8192 && decoded.plan.unresolved_count==0);
    CHECK(recover_profile_ok(&recover_gf_template)==0);
    for(u32 i=0;i<decoded.plan.node_count;i++) {
        const struct recover_plan_node *n=&decoded.nodes[i];
        const struct recover_opcode_layout *l=layout_for(&recover_gf_template,n);
        CHECK(l && l->argc==n->arg_count);
        if(!l || l->argc!=n->arg_count) {output("layout pc=");number(n->pc);output(" sub=");number(n->substep);output(" argc=");number(n->arg_count);output("\n");}
        if(l) for(u32 j=0;j<n->arg_count;j++) CHECK(l->kinds[j]==decoded.args[n->arg_first+j].kind);
        if(n->rule_id!=RECOVER_NONE) {
            const struct recover_rule *w=rule_by_id(&recover_gf_template,n->rule_id);
            CHECK(w && node_rule(n,w) && w->key==decoded.args[n->arg_first].value);
            CHECK(node_ok(&recover_gf_template,&decoded.plan,i));
        }
        for(u32 edge=0;edge<2;edge++)CHECK(n->next[edge]==RECOVER_NONE || n->next[edge]>i);
    }
    for(u32 i=0;i<3;i++) {
        const struct recover_capture *c=&decoded.captures[i];
        CHECK(c->slot==recover_gf_template.count+i && c->producer_node<c->consumer_node);
        CHECK(decoded.nodes[c->producer_node].pc==decoded.nodes[c->consumer_node].pc);
    }
    const u32 step_bounds[]={208,281,143,127,1};
    for(u32 m=0;m<5;m++) {
        CHECK(decoded.main_steps[m]==step_bounds[m]);
        output("main=");number(m);output(" nodes=");number(decoded.main_nodes[m]);
        output(" steps=");number(decoded.main_steps[m]);output(" wait_us=");number(decoded.main_wait[m]);output("\n");
    }
    output("layouts=");number(recover_gf_template.layout_count);output(" rules=");number(recover_gf_template.rule_count);
    output(" nodes=");number(decoded.plan.node_count);output(" args=");number(decoded.plan.arg_count);
    output(" ctx=");number(decoded.plan.ctx_count);output(" deps=");number(decoded.plan.dep_count);
    output(" entries=");number(decoded.plan.entry_count);output(" call_depth=");number(decoded.max_call);
    output(" repeat_depth=");number(decoded.max_repeat);output("\n");
    /* Complete reference/action inventory: node indices identify every edge;
     * args retain kind (IMM=0, READ=1, SAVED=2), no inferred read values. */
    for(u32 i=0;failures && i<decoded.plan.node_count;i++) {
        const struct recover_plan_node *n=&decoded.nodes[i];
        const struct recover_opcode_layout *l=layout_for(&recover_gf_template,n);
        output("node ");number(i);output(" pc ");number(n->pc);output(" op ");number(n->opcode);
        output(" sub ");number(n->substep);output(" dir ");number(l?l->dir_substep:RECOVER_NONE);
        output(" ctx ");number(n->ctx);output(" head/skip ");number(n->head);output("/");number(n->skip);
        output(" args");
        for(u32 j=0;j<n->arg_count;j++) {output(" ");number(decoded.args[n->arg_first+j].kind);output(":");number(decoded.args[n->arg_first+j].value);}
        output(" next ");number(n->next[0]);output("/");number(n->next[1]);output("\n");
    }
}
struct host_time {u32 sec,nsec;};
static u32 micros(void)
{
    struct host_time t;
    u32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(265),"b"(1),"c"(&t) : "memory");
    return t.sec*1000000U+t.nsec/1000;
}
static struct recover_rule stress_rules[RECOVER_RULE_MAX];
static struct recover_opcode_layout stress_layouts[RECOVER_RULE_MAX];
static struct recover_target contract_targets[3];
static struct recover_rule contract_rules[3];
static struct recover_opcode_layout contract_layouts[3];
static const u32 captured_kind[]={REC_IMM,REC_ARG_SAVED};
static void contract_setup(u32 nodes_count)
{
    setup();
    profile.targets=contract_targets;profile.target_count=3;
    profile.rules=contract_rules;profile.rule_count=3;
    profile.layouts=contract_layouts;profile.layout_count=3;
    profile.regs=0;profile.count=0;profile.quiesce=0;profile.quiesce_count=0;
    profile.index_save_targets=0;profile.index_save_count=0;
    for(u32 i=0;i<3;i++) {
        contract_targets[i]=(struct recover_target){.space=REC_BAR_MMIO,.offset=0x184c+4*i,
            .index=RECOVER_NONE,.width=4,.access=REC_COLD,.read_safe=1};
        contract_rules[i]=(struct recover_rule){.id=200+i,.key=0,.phase=REC_RUN,.main_id=0,
            .pc=0x123,.substep=i,.write_mask=~0U,
            .value={.mode=REC_LITERAL,.mask=~0U,.bits=0,.max=~0U,.expr_id=RECOVER_NONE}};
        contract_layouts[i]=(struct recover_opcode_layout){1,i,2,two,REC_WRITE,0};
        decoded.nodes[i]=(struct recover_plan_node){.phase=REC_RUN,.main_id=0,.pc=0x123,
            .opcode=1,.substep=i,.ctx=0,.rule_id=200+i,.expr_id=RECOVER_NONE,
            .arg_first=4*i,.arg_count=2,.next={i+1,i+1}};
        decoded.args[4*i]=IMM(0);decoded.args[4*i+1]=IMM(0);
    }
    /* Avoid reserved zero-only PBUS targets in this synthetic table. */
    contract_targets[1].offset=0x1700;contract_targets[2].offset=0x101000;
    decoded.nodes[nodes_count-1].next[0]=decoded.nodes[nodes_count-1].next[1]=RECOVER_NONE;
    decoded.entries[0]=(struct recover_plan_entry){REC_RUN,0,0};
    decoded.plan=(struct recover_plan){.version=1,.nodes=decoded.nodes,.node_count=nodes_count,
        .args=decoded.args,.arg_count=12,.ctx_count=1,.entries=decoded.entries,.entry_count=1};
}
static int contract_bind(void)
{
    profile.crc=recover_profile_crc(&profile);decoded.plan.profile_crc=profile.crc;
    return recover_bind_plan(&core,&profile,&decoded.plan,&buffers,&hash);
}
static void contract_arm(void)
{
    core.state=REC_RUNNING;core.profile=&profile;core.valid=true;core.budget=1000;
    core.token.session=1;core.token.identity=ident;
    for(u32 i=0;i<3;i++)core.bars[i]=core.cache.bar[i];
}
static int contract_access(u32 i,u32 value)
{
    const struct recover_plan_node *n=&decoded.nodes[i];
    const struct recover_opcode_layout *l=&contract_layouts[i];
    u32 key=decoded.args[n->arg_first].value;
    if(recover_plan_enter(&core,i) || recover_access(&core,i,l->dir_substep,key,profile.targets[key].width,&value))return -1;
    return recover_plan_leave(&core,0);
}
static void capture_fixture(u32 producer)
{
    contract_setup(3);
    contract_layouts[0]=(struct recover_opcode_layout){1,0,1,fixed_read,REC_READ,0};
    decoded.nodes[0].arg_count=1;decoded.nodes[0].rule_id=RECOVER_NONE;
    contract_layouts[1]=(struct recover_opcode_layout){1,1,4,four,REC_WRITE,0};
    contract_rules[1].value=(struct recover_value){.mode=REC_EXPR,.mask=0xf00,.bits=0,.max=~0U,.expr_id=REC_GF_RMW};
    contract_rules[1].write_mask=0xf00;
    decoded.nodes[1].arg_count=4;decoded.nodes[1].expr_id=REC_GF_RMW;
    decoded.args[5]=READ(0);decoded.args[6]=IMM(0xfffff0ff);decoded.args[7]=IMM(0);
    contract_layouts[2]=(struct recover_opcode_layout){1,2,2,captured_kind,REC_WRITE,0};
    contract_rules[2].value=(struct recover_value){.mode=REC_EXPR,.mask=0,.max=~0U,.expr_id=REC_GF_SAVED_BAR};
    decoded.nodes[2].expr_id=REC_GF_SAVED_BAR;decoded.args[9]=SAVED(0);
    decoded.captures[0]=(struct recover_capture){0,producer,2,0,0};
    decoded.plan.captures=decoded.captures;decoded.plan.capture_count=1;
}
static void capture_tests(void)
{
    for(u32 producer=0;producer<2;producer++) {
        capture_fixture(producer);CHECK(contract_bind()==0);contract_arm();values[0]=0xa5a50f55;
        CHECK(contract_access(0,0)==0);CHECK(contract_access(1,0xa5a50055)==0);
        CHECK(core.capture_valid==1 && core.capture_values[0]==0xa5a50f55);
        CHECK(contract_access(2,0xa5a50f55)==0 && core.capture_valid==0 && values[0]==0xa5a50f55);
        release_plan(&core);CHECK(core.capture_valid==0);
    }
    for(u32 bad=0;bad<8;bad++) {
        capture_fixture(1);
        if(bad==0)decoded.captures[0].slot=1;
        if(bad==1)decoded.captures[0].producer_node=2;
        if(bad==2)decoded.captures[0].target=1;
        if(bad==3)decoded.captures[0].read_ordinal=1;
        if(bad==4)decoded.nodes[2].head=1;
        if(bad==5)decoded.nodes[2].ctx=1;
        if(bad==6)decoded.nodes[0].next[1]=2;
        if(bad==7)decoded.args[9].value=1;
        CHECK(contract_bind()<0 && writes==0);
    }
    capture_fixture(1);CHECK(contract_bind()==0);contract_arm();values[0]=0xf00;
    CHECK(contract_access(0,0)==0);CHECK(contract_access(1,0)==0);
    u32 before=writes;CHECK(trial_end(&core,&core.token,REC_ABORT)<0 || core.plan==0);
    CHECK(core.capture_valid==0);u32 value=0xf00;
    CHECK(recover_access(&core,2,REC_WRITE,0,4,&value)<0 && writes==before);
}
static void port_tests(void)
{
    for(u32 same=0;same<2;same++) {
        contract_setup(3);
        contract_targets[0]=(struct recover_target){.space=REC_VGA_INDEXED,.port=RECOVER_SEQ,
            .index=1,.width=1,.access=REC_COLD,.read_safe=0};
        contract_targets[1]=contract_targets[0];contract_targets[1].port=same?RECOVER_SEQ:RECOVER_GFX;
        contract_targets[1].index=2;
        for(u32 i=0;i<3;i++) {
            u32 key=i==1?1:0,value=i==2?0x20:contract_targets[key].index;
            contract_rules[i].key=key;contract_rules[i].write_mask=255;
            contract_rules[i].value=(struct recover_value){.mode=REC_LITERAL,.mask=255,.bits=value,.max=255,.expr_id=RECOVER_NONE};
            contract_layouts[i].dir_substep=i==2?REC_DATA:REC_SELECT;
            decoded.args[4*i]=IMM(key);decoded.args[4*i+1]=IMM(value);
        }
        static const u32 index_key[]={0};profile.index_save_targets=index_key;profile.index_save_count=1;
        int rc=contract_bind();CHECK(same?rc<0:rc==0);
        if(!same && rc==0) {
            contract_arm();CHECK(contract_access(0,1)==0);CHECK(contract_access(1,2)==0);
            CHECK(contract_access(2,0x20)==0);
            CHECK(core.local_saved_count==1 && core.local_saved[0].dir_substep==REC_SELECT);
        }
    }
}
static void rejection_tests(void)
{
    for(u32 bad=0;bad<9;bad++) {
        capture_fixture(1);
        if(bad==0)decoded.plan.node_count=2049;
        if(bad==1)decoded.plan.arg_count=8193;
        if(bad==2)decoded.nodes[1].next[1]=0;
        if(bad==3)decoded.args[5].kind=REC_IMM;
        if(bad==4)decoded.nodes[1].arg_count=3;
        if(bad==5)decoded.args[4].value=1;
        if(bad==6)contract_layouts[1]=contract_layouts[0];
        if(bad==7)contract_layouts[1].dir_substep=REC_DATA;
        if(bad==8)decoded.plan.unresolved_count=1;
        CHECK(contract_bind()<0 && writes==0);
    }
    fixture();CHECK(gfinit_decode_rom(rom,sizeof(rom),&recover_gf_template,&decoded)==0);
    u32 active[GF_INIT_INSNS]={0};
    u32 call=instruction_at(&decoded,0xe780);
    for(u32 i=0;i<decoded.instruction_count;i++) if(decoded.insns[i].opcode==0x5b)call=i;
    u32 pc=decoded.insns[call].pc,saved=decoded.insns[call].ref;
    for(u32 bad=0;bad<4;bad++) {
        decoded.schedule_count=0;
        decoded.insns[call].ref=bad==0?pc:bad==1?saved+1:bad==2?0x5daa:0xffffffffU;
        CHECK(schedule_block(&decoded,pc,pc+3,0,3,0,0,active)<0);
        for(u32 i=0;i<GF_INIT_INSNS;i++)active[i]=0;
    }
    decoded.insns[call].ref=saved;decoded.schedule_count=0;
    CHECK(schedule_block(&decoded,0xe832,0xe832,0,4,GF_INIT_CALL_DEPTH+1,0,active)<0);
    CHECK(schedule_block(&decoded,0xe832,0xe832,0,4,0,GF_INIT_REPEAT_DEPTH+1,active)<0);
}
static u32 callback_us;
static int timed_read(void *v,const struct recover_bus_access *a,u32 *value)
{u32 t=micros();int rc=read_fake(v,a,value);callback_us+=micros()-t;return rc;}
static int timed_write(void *v,const struct recover_bus_access *a,u32 value)
{u32 t=micros();int rc=write_fake(v,a,value);callback_us+=micros()-t;return rc;}
static void benchmark(u32 count)
{
    contract_setup(3);
    contract_targets[0]=(struct recover_target){.space=REC_VGA_INDEXED,.port=RECOVER_SEQ,
        .index=1,.width=1,.access=REC_COLD,.read_safe=1};
    u32 nr=count==2048?2048:recover_gf_template.rule_count;
    u32 nl=count==2048?2048:recover_gf_template.layout_count;
    for(u32 i=0;i<nr;i++) {
        stress_rules[i]=contract_rules[0];stress_rules[i].id=200+i;
        stress_rules[i].substep=i>=nr-2?nl-2+i-(nr-2):i;
        stress_rules[i].write_mask=255;
        stress_rules[i].value=(struct recover_value){.mode=REC_LITERAL,.mask=255,
            .bits=i==nr-1?0x20:1,.max=255,.expr_id=RECOVER_NONE};
    }
    for(u32 i=0;i<nl;i++)stress_layouts[i]=(struct recover_opcode_layout){1,i,2,two,i==nl-1?REC_DATA:REC_SELECT,0};
    profile.rules=stress_rules;profile.rule_count=nr;profile.layouts=stress_layouts;profile.layout_count=nl;
    for(u32 i=0;i<count;i++) {
        decoded.nodes[i]=(struct recover_plan_node){.phase=REC_RUN,.main_id=0,.pc=0x123,
            .opcode=1,.substep=nl-2+i%2,.rule_id=200+nr-2+i%2,.expr_id=RECOVER_NONE,
            .arg_first=2*i,.arg_count=2,.next={i+1,i+1}};
        decoded.args[2*i]=IMM(0);decoded.args[2*i+1]=IMM(i%2?0x20:1);
    }
    decoded.nodes[count-1].next[0]=decoded.nodes[count-1].next[1]=RECOVER_NONE;
    decoded.plan.node_count=count;decoded.plan.arg_count=2*count;
    lookup_steps=0;u32 before=micros();int rc=contract_bind();
    u32 bind_us=micros()-before,bind_lookups=lookup_steps;CHECK(rc==0);
    contract_arm();lookup_steps=0;
    struct recover_ops timed_ops=ops;timed_ops.read=timed_read;timed_ops.write=timed_write;
    core.ops=&timed_ops;callback_us=0;before=micros();
    for(u32 i=0;i<count && rc==0;i++) {
        u32 value=i%2?0x20:1;
        if(recover_plan_enter(&core,i) || recover_access(&core,i,i%2?REC_DATA:REC_SELECT,0,1,&value) || recover_plan_leave(&core,0)) {CHECK(false);break;}
    }
    u32 run_us=micros()-before-callback_us;
    output("benchmark indexed nodes=");number(count);output(" bind_us=");number(bind_us);
    output(" enter/access/leave_us=");number(run_us);
    output(" bind/run_lookup_comparisons=");number(bind_lookups);output("/");number(lookup_steps);
    output(" rules/layouts=");number(nr);output("/");number(nl);output("\n");
}
static u32 read_order[8], read_count;
static int ordered_read(void *v,const struct recover_bus_access *a,u32 *value)
{if(read_count<8)read_order[read_count++]=a->key;return read_fake(v,a,value);}
static void copy_order_tests(void)
{
    for(u32 nv=0;nv<2;nv++) {
        contract_setup(nv?1:2);profile.rule_count=nv?1:2;profile.layout_count=nv?1:2;
        struct recover_ops ordered=ops;ordered.read=ordered_read;
        u32 at=nv?0:1;
        contract_targets[0].offset=0x1700;contract_targets[0].access=REC_RW;
        contract_targets[1].offset=nv?0x10020c:0x101000;contract_targets[2].offset=0x1300;
        if(!nv) {
            contract_targets[0]=(struct recover_target){.space=REC_VGA_INDEXED,.port=RECOVER_CRTC,
                .index=0x3c,.width=1,.access=REC_COLD,.read_safe=1};
            contract_rules[0].write_mask=255;contract_rules[0].value.mask=255;
            contract_rules[0].value.bits=0x3c;contract_rules[0].value.max=255;
            contract_layouts[0].dir_substep=REC_SELECT;decoded.args[1]=IMM(0x3c);
        }
        contract_rules[at].write_mask=nv?~0U:255;
        contract_rules[at].value=(struct recover_value){.mode=REC_EXPR,.max=nv?~0U:255,
            .expr_id=nv?REC_GF_EXPR_COPY_NV:REC_GF_EXPR_COPY};
        contract_layouts[at]=(struct recover_opcode_layout){1,at,nv?7:6,nv?copy_nv_layout:copy_layout,nv?REC_WRITE:REC_DATA,0};
        decoded.nodes[at].arg_count=nv?7:6;decoded.nodes[at].expr_id=contract_rules[at].value.expr_id;
        u32 base=4*at;
        decoded.args[base]=IMM(0);decoded.args[base+1]=READ(nv?1:0);decoded.args[base+2]=READ(nv?0:1);
        decoded.args[base+3]=IMM(nv?0:0xfe);decoded.args[base+4]=IMM(nv?~0U:0xf0);
        decoded.args[base+5]=IMM(nv?0:0x0f);decoded.args[base+6]=IMM(0);
        CHECK(contract_bind()==0);contract_arm();core.ops=&ordered;
        values[0]=0xab;values[1]=nv?0x12345678:0x0c;
        if(!nv)CHECK(contract_access(0,0x3c)==0);
        read_count=0;CHECK(contract_access(at,nv?0x12345678:0x3b)==0);
        CHECK(read_count==2 && read_order[0]==(nv?1U:0U) && read_order[1]==(nv?0U:1U));
    }
}
static void logical_head_tests(void)
{
    for(u32 group=0;group<4;group++) {
        contract_setup(2);profile.layout_count=2;
        contract_layouts[0]=(struct recover_opcode_layout){group?0x54:0x53,0x20123,1,fixed_read,RECOVER_NONE,REC_LAYOUT_SKIP_HEAD};
        contract_layouts[1]=(struct recover_opcode_layout){0x71,GF_INIT_C,0,0,RECOVER_NONE,0};
        decoded.nodes[0].opcode=group?0x54:0x53;decoded.nodes[0].substep=0x20123;
        decoded.nodes[0].skip=1;decoded.nodes[0].arg_count=1;decoded.nodes[0].rule_id=RECOVER_NONE;
        decoded.args[0]=IMM(group?4:3);
        decoded.nodes[1].opcode=0x71;decoded.nodes[1].substep=GF_INIT_C;
        decoded.nodes[1].head=1;decoded.nodes[1].skip=1;decoded.nodes[1].arg_count=0;decoded.nodes[1].rule_id=RECOVER_NONE;
        if(group>=2) {
            contract_layouts[0].flags=0;
            if(group==2)contract_layouts[0].opcode=decoded.nodes[0].opcode=0x52;
            CHECK(contract_bind()<0 && writes==0);continue;
        }
        CHECK(contract_bind()==0);contract_arm();
        CHECK(recover_plan_enter(&core,0)==0 && recover_plan_leave(&core,0)==0);
        CHECK(core.head==1 && recover_plan_enter(&core,1)==0 && recover_plan_leave(&core,0)==0);
        CHECK(writes==0);
    }
    const u32 unsupported[]={0x52,0x62,0x78,0x37,0x51};
    for(u32 i=0;i<GF_ARRAY(unsupported);i++) {
        decoded.plan.node_count=decoded.plan.arg_count=decoded.error=0;
        decoded.insns[0]=(struct gfinit_instruction){.opcode=unsupported[i],.port=RECOVER_CRTC,.index=RECOVER_CR44};
        decoded.schedule[0]=(struct gfinit_scheduled){0};
        CHECK(expand_step(&decoded,rom,&recover_gf_template,0,1)<0);
    }
    contract_setup(1);profile.rule_count=1;
    contract_targets[0]=(struct recover_target){.space=REC_VGA_INDEXED,.port=RECOVER_SEQ,
        .index=1,.width=1,.access=REC_COLD,.read_safe=0};
    contract_rules[0]=(struct recover_rule){.id=200,.key=0,.phase=REC_RESTORE,.main_id=RECOVER_NONE,
        .pc=RECOVER_NONE,.substep=REC_SELECT,.write_mask=255,.flags=REC_LOCAL,
        .value={.mode=REC_SAVED,.max=255,.expr_id=RECOVER_NONE}};
    static const u32 keys[]={0};profile.index_save_targets=keys;profile.index_save_count=1;
    CHECK(recover_profile_ok(&profile)==0);
    contract_rules[0].substep=REC_DATA;CHECK(recover_profile_ok(&profile)<0);
    contract_rules[0].substep=REC_SELECT;profile.index_save_count=0;CHECK(recover_profile_ok(&profile)<0);
}
#ifndef GF_S2_ENTRY
#define GF_S2_ENTRY _start
#endif
void GF_S2_ENTRY(void)
{
    parser_tests();decode_inventory();capture_tests();port_tests();copy_order_tests();logical_head_tests();rejection_tests();benchmark(972);benchmark(2048);
    output(failures?"GF S2 FAIL\n":"GF S2 PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(failures?1:0));
    __builtin_unreachable();
}
