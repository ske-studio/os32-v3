/* Complete fake device; reference interpreter below does not consume plan
 * nodes, layouts or executor branches. Input is S1 data plus synthetic tables. */
#define GF_S2_ENTRY s2_entry
#include "gfinit_host.c"
#undef GF_S2_ENTRY
#include "../../kdebug/gfinit_exec.c"
#define TRACE_MAX 8192U
struct trace_item {u32 dir,key,value;};
static struct trace_item expected[TRACE_MAX], actual[TRACE_MAX];
static u32 expected_count,actual_count, reference_values[RECOVER_TARGET_MAX];
static u32 clock_value,clock_jump,wait_total,clock_fail,wait_fail,bus_fail;
static u32 vpll_reads;
static u32 seen[256], ref_head,ref_skip;
static struct recover_target exec_targets[RECOVER_TARGET_MAX];
static struct recover_rule exec_rules[RECOVER_NODE_MAX];
static struct recover_opcode_layout exec_layouts[RECOVER_RULE_MAX];
static struct gfinit_exec execution;
static void trace_add(struct trace_item *trace,u32 *count,u32 dir,u32 key,u32 value)
{
    CHECK(*count<TRACE_MAX);
    if(*count<TRACE_MAX)trace[(*count)++]=(struct trace_item){dir,key,value};
}
static int exec_bus_write(void *v,const struct recover_bus_access *a,u32 value)
{
    (void)v;
    if(bus_fail)return -1;
    trace_add(actual,&actual_count,a->dir_substep,a->key,value);
    if(a->dir_substep==REC_SELECT)indices[a->key]=value;else values[a->key]=value;
    return 0;
}
static int exec_bus_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    (void)v;
    if(bus_fail)return -1;
    if (core.entered && core.plan && core.cursor < core.plan->node_count) {
        const struct recover_plan_node *n = &core.plan->nodes[core.cursor];
        if (a->key == core.plan->args[n->arg_first].value &&
            n->main_id == RECOVER_VPLL_MAIN && n->pc == RECOVER_VPLL_PC &&
            ((n->substep >= RECOVER_VPLL_FIRST && n->substep <= RECOVER_VPLL_LAST) ||
             (n->substep >= RECOVER_VPLL_REPEAT_FIRST && n->substep <= RECOVER_VPLL_REPEAT_LAST)))
            vpll_reads++;
    }
    *value=a->dir_substep==REC_SELECT?indices[a->key]:values[a->key];
    return 0;
}
static int exec_clock(void *v,u32 *out)
{(void)v;if(clock_fail)return -1;*out=clock_value;clock_value+=clock_jump;clock_jump=0;return 0;}
static int exec_delay(void *v,u32 us)
{
    (void)v;if(wait_fail)return -1;
    trace_add(actual,&actual_count,RECOVER_NONE,0,us);
    clock_value+=us;wait_total+=us;return 0;
}
static const struct gfinit_exec_ops executor_ops={exec_clock,exec_delay};
static struct recover_ops executor_bus;
/* Deliberately synthetic permissions: exact decoded operand expressions, no
 * hardware permission claim. The separate S5 bus test executes the production
 * COPY/capture/PLL permissions as well as this synthetic control coverage. */
static void synthetic_profile(void)
{
    profile=gf_fixture_profile;profile.regs=0;profile.count=0;
    profile.quiesce=0;profile.quiesce_count=0;profile.index_save_targets=0;profile.index_save_count=0;
    profile.layouts=exec_layouts;
    profile.targets=exec_targets;
    for(u32 i=0;i<profile.target_count;i++)exec_targets[i]=gf_fixture_profile.targets[i];
    for(u32 i=0;i<decoded.instruction_count;i++)if(decoded.insns[i].opcode==0x4a || decoded.insns[i].opcode==0x4b) {
        u32 reg=decoded.insns[i].target;
        if(key_for(&profile,reg,0,0,0)==RECOVER_NONE)
            exec_targets[profile.target_count++]=(struct recover_target){.space=REC_BAR_MMIO,.offset=reg,.width=4,.index=RECOVER_NONE,.access=REC_COLD};
    }
    for(u32 i=0;i<decoded.plan.arg_count;i++)if(decoded.args[i].kind==REC_ARG_SAVED)decoded.args[i].value-=gf_fixture_profile.count;
    for(u32 i=0;i<decoded.plan.capture_count;i++)decoded.captures[i].slot-=gf_fixture_profile.count;
    for(u32 i=0;i<profile.layout_count;i++)exec_layouts[i]=gf_fixture_profile.layouts[i];
    profile.rules=exec_rules;profile.rule_count=0;
    for(u32 i=0;i<decoded.plan.node_count;i++) {
        struct recover_plan_node *n=&decoded.nodes[i];
        struct recover_opcode_layout *l=0;
        for(u32 j=0;j<profile.layout_count;j++)if(exec_layouts[j].opcode==n->opcode && exec_layouts[j].substep==n->substep)l=&exec_layouts[j];
        CHECK(l!=0);if(!l)return;
        if(l->dir_substep==RECOVER_NONE || l->dir_substep==REC_READ)continue;
        u32 expr=RECOVER_NONE;
        if(l->argc==4)expr=REC_GF_RMW;
        if(n->substep>=GF_PLL_SUBSTEP && n->substep<GF_PLL_SUBSTEP+3*GF_PLL_SUBSTEP_STRIDE && l->argc==4)expr=REC_GF_PLL_SAVED;
        if(n->opcode==0x37 && l->argc==6)expr=REC_GF_EXPR_COPY;
        if(n->opcode==0x5f)expr=REC_GF_EXPR_COPY_NV;
        if(l->argc==2 && decoded.args[n->arg_first+1].kind==REC_ARG_SAVED)expr=REC_GF_SAVED_BAR;
        u32 key=decoded.args[n->arg_first].value;
        u32 id=0;
        for(;id<profile.rule_count;id++)if(exec_rules[id].key==key && exec_rules[id].main_id==n->main_id && exec_rules[id].pc==n->pc && exec_rules[id].substep==n->substep)break;
        if(id<profile.rule_count){n->rule_id=id;n->expr_id=expr;continue;}
        profile.rule_count++;
        exec_rules[id]=(struct recover_rule){.id=id,.key=key,.phase=REC_RUN,
            .main_id=n->main_id,.pc=n->pc,.substep=n->substep,
            .write_mask=width_mask(profile.targets[key].width),.flags=n->opcode==0x65?REC_FORCE:0,
            .value={.mode=expr==RECOVER_NONE?REC_LITERAL:REC_EXPR,
                .mask=expr==RECOVER_NONE?width_mask(profile.targets[key].width):0,
                .bits=expr==RECOVER_NONE?decoded.args[n->arg_first+1].value:0,
                .max=width_mask(profile.targets[key].width),.expr_id=expr}};
        if(n1_target(&profile.targets[key]) && l->dir_substep==REC_DATA) {
            /* Keep real N1 rows for the one authorized display transition. */
            if(n->pc==0xe81e)exec_rules[id].flags|=REC_N1;
            else {exec_rules[id].value.mask|=RECOVER_SR_BLANK;exec_rules[id].value.bits|=RECOVER_SR_BLANK;}
        }
        if(profile.targets[key].space==REC_VGA_INDEXED && profile.targets[key].port==RECOVER_CRTC &&
           profile.targets[key].index==RECOVER_CR44 && l->dir_substep==REC_DATA && expr!=RECOVER_NONE) {
            exec_rules[id].value.mask=255;exec_rules[id].value.bits=decoded.args[n->arg_first+3].value;
        }
        n->rule_id=id;n->expr_id=expr;
    }
    decoded.plan.unresolved_count=0;
    profile.trial_limit=1000;profile.opcode_limit=1000;profile.restore_limit=1000;
    profile.max_steps=4096;profile.max_polls=65536;profile.restore_max_polls=65536;
}
static void exec_arm(void)
{
    contract_arm();core.in_call=true;core.in_opcode=true;
    executor_bus=ops;executor_bus.read=exec_bus_read;executor_bus.write=exec_bus_write;core.ops=&executor_bus;
    clock_value=clock_jump=wait_total=clock_fail=wait_fail=bus_fail=actual_count=expected_count=0;
    execution=(struct gfinit_exec){.rom=rom,.rom_size=sizeof(rom),.ops=&executor_ops};
}
static u32 ref_key(u32 reg,u32 port,u32 index)
{
    u32 key=key_for(&profile,reg,port,index,ref_head);CHECK(key!=RECOVER_NONE);return key;
}
static u32 ref_read(u32 key){return ref_skip?0:reference_values[key];}
static void ref_write(u32 key,u32 value,u32 dir)
{
    const struct recover_target *t=&profile.targets[key];
    value &= width_mask(t->width);
    if(!ref_skip) {
        trace_add(expected,&expected_count,dir,key,value);
        if(dir!=REC_SELECT)reference_values[key]=value;
    }
    if(dir==REC_DATA && t->space==REC_VGA_INDEXED && t->port==RECOVER_CRTC && t->index==RECOVER_CR44)
        ref_head=value==3;
}
static void ref_index(u32 port,u32 index,u32 keep,u32 value)
{
    u32 key=ref_key(0,port,index);
    ref_write(key,index,REC_SELECT);
    ref_write(key,(ref_read(key)&keep)|value,REC_DATA);
}
static void ref_delay(u32 us)
{
    if(ref_skip)return;
    while(us){u32 n=us>1000?1000:us;trace_add(expected,&expected_count,RECOVER_NONE,0,n);us-=n;}
}
static u32 ref_shift(u32 v,u32 s){return s<128?v>>s:v<<(256-s);}
static void reference_pll(u32 reg,u32 khz)
{
    if(ref_skip)return;
    struct gfinit_pll_limits l;struct gfinit_pll_solution s;
    CHECK(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,reg,&l)==0);
    CHECK(gfinit_pll_solve(&l,khz,&s)==0);
    u32 gate=ref_read(ref_key(GF_PLL_GATE,0,0));
    u32 old=ref_read(ref_key(reg==GF_PLL_VIDEO?reg:reg-4,0,0));
    u32 nm1=s.n1*256+s.m1,nm2=s.n2*256+s.m2;
#define PW(r,v) ref_write(ref_key((r),0,0),(v),REC_WRITE)
    if(reg==GF_PLL_VIDEO) {
        u32 stage=ref_read(ref_key(GF_PLL_VIDEO_STAGE,0,0));
        u32 coeff=ref_read(ref_key(GF_PLL_VIDEO_COEFF,0,0));
        PW(GF_PLL_GATE,gate&0xfffcffffU);
        PW(GF_PLL_VIDEO_STAGE,(stage&0xfffffeffU)|(s.n2==s.m2?0x100:0));
        PW(GF_PLL_VIDEO_COEFF,(coeff&0x7fff0000U)|0x80000000U|nm2|(s.n2==s.m2?0x11f:0));
        PW(reg,(old&0xfff80000U)|(s.p<<16)|nm1);PW(GF_PLL_GATE,gate);
#if GF_VPLL_PROBE
        PW(RECOVER_VPLL_SWITCH_OFF, RECOVER_VPLL_SWITCH_ON);
        PW(GF_PLL_GATE,gate&0xfffcffffU);
        PW(GF_PLL_VIDEO_STAGE,(stage&0xfffffeffU)|(s.n2==s.m2?0x100:0));
        PW(GF_PLL_VIDEO_COEFF,(coeff&0x7fff0000U)|0x80000000U|nm2|(s.n2==s.m2?0x11f:0));
        PW(reg,(old&0xfff80000U)|(s.p<<16)|nm1);PW(GF_PLL_GATE,gate);
        PW(RECOVER_VPLL_SWITCH_OFF, RECOVER_VPLL_SWITCH_DEFAULT);
#endif
    } else {
        bool mem=reg==GF_PLL_MEMORY;
        u32 power=mem?ref_read(ref_key(GF_PLL_MEMORY_POWER,0,0)):0;
        u32 p=(old&(mem?0xff88ffffU:0xfff8ffffU))|0xc0000000U|(s.p<<16);
        if(mem){u32 bias=s.p+l.bias_p;if(bias>l.max_p)bias=l.max_p;p|=0x10000000U|(bias<<20);}
        if(s.n2==s.m2)p|=mem?0x1000:0x100;
        if(mem)PW(GF_PLL_MEMORY_POWER,power|0x80000000U);
        PW(reg-4,old|0x10000000U);PW(reg-4,p&0xbfffffffU);
        if(mem){p|=0x800000;PW(reg-4,p&0x3fffffffU);PW(GF_PLL_MEMORY_MIRROR,p&0x3fffffffU);}
        PW(GF_PLL_GATE,gate&(mem?0xffff3fffU:0xfffffcccU));PW(reg,(nm2<<16)|nm1);
        if(mem)PW(GF_PLL_MEMORY_COEFF,(nm2<<16)|nm1);
        PW(reg-4,p);
        if(mem){p&=0xff7fffffU;PW(reg-4,p);PW(GF_PLL_MEMORY_MIRROR,p);PW(GF_PLL_MEMORY_POWER,power);}
        PW(GF_PLL_GATE,gate);
        if(mem){PW(reg-4,p&0xefffffffU);PW(GF_PLL_MEMORY_MIRROR,p&0xefffffffU);}
    }
#undef PW
}
static u32 reference(u32 pc,u32 depth)
{
    CHECK(depth<24);
    for(u32 budget=0;budget<4096;budget++) {
        u32 op=rom[pc],len=1,t,v,count,port,index,mask,shift,src,dst,base;
        seen[op]++;
        switch(op) {
        case 0x32:case 0x4a:
            port=le(rom,pc+1,2);index=rom[pc+3];mask=rom[pc+4];shift=rom[pc+5];count=rom[pc+6];
            t=ref_key(0,port,index);ref_write(t,index,REC_SELECT);
            v=(ref_read(t)&mask)>>shift;
            if(!ref_skip && v<count) {
                if(op==0x4a)reference_pll(le(rom,pc+7,4),le(rom,pc+11+4*v,4));
                else ref_write(ref_key(le(rom,pc+7,4),0,0),le(rom,pc+11+4*v,4),REC_WRITE);
            }
            len=11+4*count;break;
        case 0x33:
            count=rom[pc+1];v=0;for(u32 k=0;k<count;k++)v=reference(pc+2,depth+1);
            pc=v;continue;
        case 0x36:case 0x71:return pc+1;
        case 0x37:
            t=ref_key(0,le(rom,pc+7,2),rom[pc+9]);ref_write(t,rom[pc+9],REC_SELECT);
            v=(ref_read(t)&rom[pc+10])|(ref_shift(ref_read(ref_key(le(rom,pc+1,4),0,0)),rom[pc+5])&rom[pc+6]);
            ref_write(t,v,REC_DATA);len=11;break;
        case 0x38:ref_skip^=1;break;
        case 0x39:
            base=GF_INIT_IO_FLAG_TABLE+9*rom[pc+1];t=ref_key(0,le(rom,base,2),rom[base+2]);
            ref_write(t,rom[base+2],REC_SELECT);v=(ref_read(t)&rom[base+3])>>rom[base+4];
            if((rom[le(rom,base+5,2)+v]&rom[base+7])!=rom[base+8])ref_skip=1;
            len=2;break;
        case 0x4b:reference_pll(le(rom,pc+1,4),le(rom,pc+5,4));len=9;break;
        case 0x51:
            t=ref_key(0,RECOVER_CRTC,rom[pc+1]);dst=ref_key(0,RECOVER_CRTC,rom[pc+2]);
            ref_write(t,rom[pc+1],REC_SELECT);v=ref_read(t);count=rom[pc+4];
            for(u32 k=0;k<count;k++) {
                if(k)ref_write(t,rom[pc+1],REC_SELECT);
                ref_write(t,rom[pc+3]+k,REC_DATA);ref_write(dst,rom[pc+2],REC_SELECT);ref_write(dst,rom[pc+5+k],REC_DATA);
            }
            ref_write(t,rom[pc+1],REC_SELECT);ref_write(t,v,REC_DATA);len=5+count;break;
        case 0x52:ref_index(RECOVER_CRTC,rom[pc+1],rom[pc+2],rom[pc+3]);len=4;break;
        case 0x53:ref_index(RECOVER_CRTC,rom[pc+1],0,rom[pc+2]);len=3;break;
        case 0x54:count=rom[pc+1];for(u32 k=0;k<count;k++)ref_index(RECOVER_CRTC,rom[pc+2+2*k],0,rom[pc+3+2*k]);len=2+2*count;break;
        case 0x58:count=rom[pc+5];for(u32 k=0;k<count;k++)ref_write(ref_key(le(rom,pc+1,4)+4*k,0,0),le(rom,pc+6+4*k,4),REC_WRITE);len=6+4*count;break;
        case 0x5b:if(!ref_skip)reference(le(rom,pc+1,2),depth+1);len=3;break;
        case 0x5f:
            src=ref_key(le(rom,pc+1,4),0,0);dst=ref_key(le(rom,pc+14,4),0,0);
            v=(ref_read(dst)&le(rom,pc+18,4))|((ref_shift(ref_read(src),rom[pc+5])&le(rom,pc+6,4))^le(rom,pc+10,4));
            ref_write(dst,v,REC_WRITE);len=22;break;
        case 0x62:ref_index(le(rom,pc+1,2),rom[pc+3],0,rom[pc+4]);len=5;break;
        case 0x63:break;
        case 0x65:
            v=ref_skip;ref_skip=0;t=ref_key(0x184c,0,0);src=ref_read(t);dst=ref_key(le(rom,pc+1,4),0,0);
            ref_write(t,src&0xfffff0ffU,REC_WRITE);ref_write(dst,le(rom,pc+5,4),REC_WRITE);ref_delay(10);
            ref_write(dst,le(rom,pc+9,4),REC_WRITE);ref_write(t,src,REC_WRITE);ref_write(ref_key(0x1850,0,0),0,REC_WRITE);
            ref_skip=v;len=13;break;
        case 0x6b:if(!ref_skip)reference(le(rom,GF_INIT_SCRIPT_TABLE+2*rom[pc+1],2),depth+1);len=2;break;
        case 0x6e:t=ref_key(le(rom,pc+1,4),0,0);ref_write(t,(ref_read(t)&le(rom,pc+5,4))|le(rom,pc+9,4),REC_WRITE);len=13;break;
        case 0x6f:base=GF_INIT_MACRO_TABLE+8*rom[pc+1];ref_write(ref_key(le(rom,base,4),0,0),le(rom,base+4,4),REC_WRITE);len=2;break;
        case 0x72:ref_skip=0;break;
        case 0x74:ref_delay(le(rom,pc+1,2));len=3;break;
        case 0x75:
            base=GF_INIT_CONDITION_TABLE+12*rom[pc+1];
            if((ref_read(ref_key(le(rom,base,4),0,0))&le(rom,base+4,4))!=le(rom,base+8,4))ref_skip=1;
            len=2;break;
        case 0x78:ref_index(le(rom,pc+1,2),rom[pc+3],rom[pc+4],rom[pc+5]);len=6;break;
        case 0x7a:t=le(rom,pc+1,4);ref_write(ref_key(t,0,0),le(rom,pc+5,4)|(t==0x200),REC_WRITE);len=9;break;
        default:CHECK(false);return pc;
        }
        pc+=len;
    }
    CHECK(false);return pc;
}
static void compare_trace(void)
{
    CHECK(actual_count==expected_count);
    u32 count=actual_count<expected_count?actual_count:expected_count;
    for(u32 i=0;i<count;i++)if(actual[i].dir!=expected[i].dir || actual[i].key!=expected[i].key || actual[i].value!=expected[i].value) {
        output("trace mismatch at ");number(i);output(" actual ");number(actual[i].key);output(":");number(actual[i].value);
        output(" expected ");number(expected[i].key);output(":");number(expected[i].value);output("\n");CHECK(false);break;
    }
}
static void all_trace(void)
{
    setup();fixture();CHECK(gfinit_decode_rom(rom,sizeof(rom),&gf_fixture_profile,&decoded)==0);
    CHECK(recover_profile_ok(&gf_fixture_profile)==0);
#if GF_VPLL_PROBE
    CHECK(gf_fixture_profile.version==RECOVER_PROFILE_VPLL_VERSION);
    CHECK(gf_fixture_profile.target_count==REC_GF_TARGET_COUNT+1);
    CHECK(gf_fixture_profile.rule_count==REC_GF_RULE_COUNT+7);
    CHECK(gf_fixture_profile.targets[REC_GF_TARGET_COUNT].read_safe);
    for(u32 i=0;i<7;i++) {
        const struct recover_rule *w=&gf_fixture_profile.rules[REC_GF_RULE_COUNT+i];
        CHECK(w->substep==RECOVER_VPLL_STAGE2_FIRST+i);
        CHECK(w->verify_mask==((i==0 || i==6)?~0U:0));
    }
#endif
    synthetic_profile();
    CHECK(recover_profile_ok(&profile)==0);
    for(u32 i=0;i<decoded.plan.node_count;i++)if(!node_ok(&profile,&decoded.plan,i)){output("bad node ");number(i);output(" pc ");number(decoded.nodes[i].pc);output("\n");CHECK(false);return;}
    for(u32 i=0;i<profile.rule_count;i++)if(exec_rules[i].pc==0x5df2 && exec_rules[i].value.mode==REC_EXPR) {
        exec_rules[i].value.mask=0;CHECK(contract_bind()<0 && writes==0);exec_rules[i].value.mask=255;
    }
    CHECK(contract_bind()==0);exec_arm();
    for(u32 i=0;i<profile.target_count;i++)reference_values[i]=values[i]=0;
    vpll_reads=0;
    for(u32 main=0;main<5;main++) {
        ref_head=ref_skip=0;actual_count=expected_count=0;
        u32 pc=decoded.nodes[decoded.entries[main].first].pc;
        reference(pc,0);execution.main_id=main;
        int rc=gfinit_exec_run(&core,&execution);
        if(rc){output("executor rc at pc ");number(execution.fail_pc);output(" reason ");number(core.reason);output("\n");}
        CHECK(rc==0);if(rc)return;
        compare_trace();output("main trace ");number(main);output(" events ");number(actual_count);output(" steps ");number(execution.steps);output("\n");
    }
#if GF_VPLL_PROBE
    output("VPLL reads ");number(vpll_reads);output("\n");
    CHECK(vpll_reads==10);
#else
    output("VPLL reads ");number(vpll_reads);output("\n");
    CHECK(vpll_reads==0);
#endif
    u32 kinds=0;for(u32 i=0;i<256;i++)if(seen[i])kinds++;
    CHECK(kinds==27);output("opcode traces ");number(kinds);output("\n");
}
/* Bounded synthetic control plans, using the real binder and executor. */
static void control_begin(void)
{
    contract_setup(1);profile.rule_count=0;profile.layouts=exec_layouts;profile.layout_count=0;
    decoded.plan.node_count=decoded.plan.arg_count=0;decoded.plan.rom_size=sizeof(rom);
}
static void append_control(u32 op,u32 sub,u32 pc,u32 ctx,u32 argc,u32 a,u32 b,u32 c)
{
    u32 id=decoded.plan.node_count++,base=decoded.plan.arg_count;
    decoded.nodes[id]=(struct recover_plan_node){.phase=REC_RUN,.main_id=0,.pc=pc,.opcode=op,.substep=sub,
        .ctx=ctx,.rule_id=RECOVER_NONE,.expr_id=RECOVER_NONE,.arg_first=base,.arg_count=argc,.next={id+1,id+1}};
    u32 args3[]={a,b,c};for(u32 i=0;i<argc;i++)decoded.args[decoded.plan.arg_count++]=IMM(args3[i]);
    if(ctx>=decoded.plan.ctx_count)decoded.plan.ctx_count=ctx+1;
    for(u32 i=0;i<profile.layout_count;i++)if(exec_layouts[i].opcode==op && exec_layouts[i].substep==sub)return;
    exec_layouts[profile.layout_count++]=(struct recover_opcode_layout){op,sub,argc,control_kinds,RECOVER_NONE,0};
}
static void finish_controls(void)
{
    u32 last=decoded.plan.node_count-1;
    decoded.nodes[last].next[0]=decoded.nodes[last].next[1]=RECOVER_NONE;
    CHECK(contract_bind()==0);exec_arm();
}
static char exec_log[4096];
static u32 exec_log_n;
static bool exec_log_busy;
static int exec_log_get(void *opaque) { (void)opaque; return -1; }
static int exec_log_ready(void *opaque) { (void)opaque; return !exec_log_busy; }
static int exec_log_put(void *opaque, u32 byte)
{
    (void)opaque;
    if (exec_log_n + 1 >= sizeof(exec_log)) return -1;
    exec_log[exec_log_n++] = byte; exec_log[exec_log_n] = 0; return 0;
}
static int exec_log_wait(void *opaque, u32 us) { (void)opaque; (void)us; return 0; }
static bool exec_log_has(const char *text)
{
    for (u32 i = 0; i < exec_log_n; i++) {
        u32 n = 0;
        while (text[n] && exec_log[i + n] && text[n] == exec_log[i + n]) n++;
        if (!text[n]) return true;
    }
    return false;
}
static const struct recover_transport exec_transport = {
    .get=exec_log_get, .uart={exec_log_ready,exec_log_put,exec_log_wait}, .frame_us=1042, .max_polls=4096
};
static void log_tests(void)
{
    for (u32 busy = 0; busy < 2; busy++) {
        control_begin(); append_control(0x74, GF_INIT_C, 0x123, 0, 1, 17, 0, 0); finish_controls();
        exec_log_n=0;exec_log[0]=0;exec_log_busy=busy;core.transport=&exec_transport;
        CHECK((gfinit_exec_run(&core,&execution)==0)==!busy);
        if (busy) CHECK(wait_total==0 && actual_count==0);
        else {
            CHECK(wait_total==17);
            CHECK(exec_log_has("phase=gf_pre event=BEGIN main=0 pc=291 opcode=116 exec=1"));
            CHECK(exec_log_has("phase=gf_pre event=END main=0 pc=291 opcode=116 exec=1"));
            CHECK(exec_log_has("rc=0"));
        }
        core.transport=0;
    }
}
static void depth_tests(void)
{
    for(u32 repeat=0;repeat<2;repeat++)for(u32 extra=0;extra<2;extra++) {
        u32 max=repeat?GF_INIT_REPEAT_DEPTH:GF_INIT_CALL_DEPTH,depth=max+extra;
        control_begin();
        for(u32 i=0;i<depth;i++)append_control(repeat?0x33:0x5b,GF_INIT_C,0x100+i,i,2,1,0,0);
        append_control(0x71,GF_INIT_C,0x200,depth,0,0,0,0);
        for(u32 i=depth;i-->0;)append_control(repeat?0x33:0x5b,GF_INIT_C+(repeat?2:1),0x100+i,i,1,1,0,0);
        append_control(0x71,GF_INIT_C,0x300,0,0,0,0,0);finish_controls();
        int rc=gfinit_exec_run(&core,&execution);CHECK(extra?rc<0:rc==0);
        CHECK(actual_count==0);
        if(extra)CHECK((repeat?execution.repeats:execution.calls)==max && execution.fail_pc==0x100+max);
    }
}
static void budget_tests(void)
{
    CHECK(gf_fixture_profile.opcode_limit == 60U * PIT_HZ);
    CHECK(gf_fixture_profile.max_polls == 65536U);
    /* Production poll budget covers both sides of 4096 nodes and 5000 waits. */
    CHECK(gf_fixture_profile.max_polls >= 2U * 4096U + 2U * 5000U);
    for(u32 extra=0;extra<2;extra++) {
        control_begin();append_control(0x71,GF_INIT_C,0x100,0,0,0,0,0);finish_controls();
        core.token.profile_crc=profile.crc;core.token.profile_version=profile.version;
        core.token.plan_hash=core.bound_hash;core.token.snapshot_crc=snapshot_crc(&core);
        core.state=REC_PAUSED;core.in_call=false;clock_jump=GF_INIT_EXEC_US-1+extra;
        int rc=trial_step(&core,&core.token,true,gfinit_exec_run,&execution);
        CHECK(extra ? rc<0 : rc==0);
        CHECK(extra ? core.reason==REC_TIMEOUT && cleanup_calls==1 : core.state==REC_PAUSED);
    }

    for(u32 extra=0;extra<2;extra++) {
        control_begin();append_control(0x74,GF_INIT_C,0x100,0,1,GF_INIT_WAIT_MAX+extra,0,0);finish_controls();
        int rc=gfinit_exec_run(&core,&execution);CHECK(extra?rc<0:rc==0);
        CHECK(wait_total==(extra?0:GF_INIT_WAIT_MAX));
    }
    for(u32 extra=0;extra<2;extra++) {
        control_begin();append_control(0x71,GF_INIT_C,0x100,0,0,0,0,0);finish_controls();
        clock_jump=GF_INIT_EXEC_US-1+extra;
        int rc=gfinit_exec_run(&core,&execution);CHECK(extra?rc<0:rc==0);
    }
    /* 1365 COPYs charge 4095 nodes/internal reads, then DONE is 4096.
     * One additional control node is rejected without a bus access. */
    for(u32 extra=0;extra<2;extra++) {
        contract_setup(1);profile.rule_count=1;profile.layout_count=1;
        contract_targets[0].offset=0x1700;contract_targets[1].offset=0x1800;
        contract_rules[0].value=(struct recover_value){.mode=REC_EXPR,.max=~0U,.expr_id=REC_GF_EXPR_COPY_NV};
        contract_layouts[0]=(struct recover_opcode_layout){0x5f,0,7,copy_nv_layout,REC_WRITE,0};
        /* Every COPY shares one immutable operand tuple to stay below ARG_MAX. */
        for(u32 i=0;i<1365;i++)
            decoded.nodes[i]=(struct recover_plan_node){.phase=REC_RUN,.main_id=0,.pc=0x123,.opcode=0x5f,
                .rule_id=200,.expr_id=REC_GF_EXPR_COPY_NV,.arg_first=0,.arg_count=7,.next={i+1,i+1}};
        decoded.args[0]=IMM(0);decoded.args[1]=READ(0);decoded.args[2]=READ(0);decoded.args[3]=IMM(0);
        decoded.args[4]=IMM(~0U);decoded.args[5]=IMM(0);decoded.args[6]=IMM(0);
        decoded.plan.arg_count=7;decoded.plan.node_count=1365;decoded.plan.rom_size=sizeof(rom);
        profile.layouts=exec_layouts;exec_layouts[0]=contract_layouts[0];
        append_control(0x71,GF_INIT_C,0x200,0,0,0,0,0);
        if(extra)append_control(0x71,GF_INIT_C,0x201,0,0,0,0,0);
        finish_controls();
        int rc=gfinit_exec_run(&core,&execution);CHECK(extra?rc<0:rc==0);
        CHECK(execution.steps==4096 && actual_count==1365);
    }
    output("budgets: steps 4096/4097, call 16/17, repeat 8/9, wait 5s/+1us, time 60s boundary\n");
}
static void condition_tests(void)
{
    for(u32 op=0;op<2;op++)for(u32 truth=0;truth<2;truth++) {
        control_begin();
        /* Q shares pc/ctx with C. Distinct successors reveal the chosen edge. */
        append_control(op?0x39:0x75,GF_INIT_Q,0x100,0,1,0,0,0);
        exec_layouts[0].dir_substep=REC_READ;
        if(op) {
            append_control(0x39,GF_INIT_C,0x100,0,3,0,3,0);
            u32 base=decoded.nodes[1].arg_first;
            decoded.args[base+3]=IMM(0x80);decoded.args[base+4]=IMM(0xff);decoded.args[base+5]=IMM(7);
            decoded.nodes[1].arg_count=6;decoded.plan.arg_count+=3;exec_layouts[1].argc=6;
            rom[0x82]=truth?7:8;
        } else append_control(0x75,GF_INIT_C,0x100,0,3,0,0xff,truth?2:3);
        append_control(0x74,GF_INIT_C,0x101,0,1,17,0,0);
        append_control(0x71,GF_INIT_C,0x102,0,0,0,0,0);
        decoded.nodes[1].next[1]=3;
        finish_controls();values[0]=2;
        CHECK(gfinit_exec_run(&core,&execution)==0);CHECK(wait_total==(truth?0:17));
    }
    /* A skipped TIME/CONDITION/CALL does not read, wait or enter a frame. */
    control_begin();append_control(0x74,GF_INIT_K+0x100,0x100,0,1,3,0,0);
    append_control(0x75,GF_INIT_K+0x101,0x101,0,1,2,0,0);
    append_control(0x5b,GF_INIT_K+0x102,0x102,0,1,3,0,0);
    append_control(0x63,GF_INIT_C,0x103,0,0,0,0,0);
    append_control(0x71,GF_INIT_C,0x104,0,0,0,0,0);
    for(u32 i=0;i<decoded.plan.node_count;i++)decoded.nodes[i].skip=1;
    finish_controls();bus_fail=1;CHECK(gfinit_exec_run(&core,&execution)==0 && actual_count==0 && wait_total==0);
    output("conditions true/false, IO flag lookup, skipped read/write/TIME/CALL and forced COMPUTE_MEM\n");
}
static void frame_mismatch_tests(void)
{
    for(u32 kind=0;kind<7;kind++) {
        bool repeat=kind>=2 && kind<=3;
        control_begin();append_control(repeat?0x33:0x5b,GF_INIT_C,0x100,0,2,1,0,0);
        if(kind<4)append_control(repeat?0x33:0x5b,GF_INIT_C+(repeat?2:1),
            kind==0?0x101:0x100,kind==1 || kind==2?1:0,1,kind==3?2:1,0,0);
        if(kind==6) { /* missing repeat exit independently */
            decoded.nodes[0].opcode=0x33;exec_layouts[0].opcode=0x33;
        }
        append_control(0x71,GF_INIT_C,0x200,0,0,0,0,0);finish_controls();
        CHECK(gfinit_exec_run(&core,&execution)<0 && actual_count==0);
    }
}
static u32 unsafe_reads;
static int count_unsafe_read(void *v,const struct recover_bus_access *a,u32 *out)
{unsafe_reads++;return exec_bus_read(v,a,out);}
static void pre_read_tests(void)
{
    for(u32 unclassified=0;unclassified<2;unclassified++) {
        contract_setup(1);profile.rule_count=1;profile.layout_count=1;
        contract_targets[0]=(struct recover_target){.space=REC_VGA_INDEXED,.port=RECOVER_CRTC,
            .index=0x57,.width=1,.access=REC_RW,.read_safe=1};
        contract_rules[0]=(struct recover_rule){.id=200,.key=0,.phase=REC_RESTORE,.main_id=RECOVER_NONE,
            .pc=RECOVER_NONE,.substep=REC_DATA,.write_mask=0x7f,
            .value={.mode=REC_LITERAL,.mask=0x7f,.bits=1,.max=255,.expr_id=RECOVER_NONE}};
        contract_arm();core.profile=&profile;core.state=REC_RESTORING;core.auth_phase=REC_RESTORE;
        core.restore_tick=0;core.restore_polls=0;
        CHECK(!cleanup_budget_prepare(&core));
        executor_bus=ops;executor_bus.read=count_unsafe_read;core.ops=&executor_bus;
        core.index_port[0]=0;core.port_selected[0]=unclassified?0:RECOVER_NONE;
        if(unclassified)contract_targets[0].access=REC_UNCLASSIFIED;
        unsafe_reads=0;u32 value=1;
        CHECK(recover_access(&core,RECOVER_NONE,REC_DATA,0,1,&value)<0 && unsafe_reads==0);
    }
}
static void failure_tests(void)
{
    for(u32 fail_kind=0;fail_kind<7;fail_kind++) {
        control_begin();append_control(0x74,GF_INIT_C,0x100,0,1,1001,0,0);
        append_control(0x71,GF_INIT_C,0x101,0,0,0,0,0);finish_controls();
        if(fail_kind==0)clock_fail=1;
        if(fail_kind==1)wait_fail=1;
        if(fail_kind==2)execution.main_id=1;
        if(fail_kind==3)core.expected_node=1;
        if(fail_kind==4) {
            decoded.nodes[0].opcode=0x75;decoded.nodes[0].substep=GF_INIT_Q;decoded.args[0]=IMM(0);
            exec_layouts[0]=(struct recover_opcode_layout){0x75,GF_INIT_Q,1,fixed_read,REC_READ,0};
            release_plan(&core);core.state=REC_IDLE;CHECK(contract_bind()==0);exec_arm();bus_fail=1;
        }
        if(fail_kind==5)core.enabled=false;
        if(fail_kind==6)decoded.plan.unresolved_count=1;
        /* Use the actual trial wrapper: failure owns cleanup exactly once. */
        core.token.profile_crc=profile.crc;core.token.profile_version=profile.version;
        core.token.plan_hash=core.bound_hash;core.token.snapshot_crc=snapshot_crc(&core);
        core.state=REC_PAUSED;core.in_call=false;
        CHECK(trial_step(&core,&core.token,true,gfinit_exec_run,&execution)<0);
        if(fail_kind==5)CHECK(cleanup_calls==0 && actual_count==0); /* denied before trial */
        else CHECK(cleanup_calls==1 && actual_count==0 && !core.plan);
    }
}
static void selection_tests(void)
{
    for(u32 pll=0;pll<2;pll++)for(u32 selected=0;selected<8;selected++) {
        control_begin();u32 op=pll?0x4a:0x32;
        append_control(op,GF_INIT_Q,0x100,0,1,0,0,0);exec_layouts[0].dir_substep=REC_READ;
        for(u32 k=0;k<8;k++) {
            append_control(op,GF_INIT_C+k,0x100,0,3,0x70,4,k);
            decoded.args[decoded.plan.arg_count++]=IMM(8);decoded.nodes[k+1].arg_count=4;exec_layouts[k+1].argc=4;
        }
        for(u32 k=0;k<8;k++) {
            append_control(0x74,GF_INIT_C,0x101+k,0,1,k+1,0,0);
            decoded.nodes[k+1].next[1]=9+k;decoded.nodes[9+k].next[0]=decoded.nodes[9+k].next[1]=17;
        }
        decoded.nodes[8].next[0]=17;
        append_control(0x71,GF_INIT_C,0x200,0,0,0,0,0);finish_controls();values[0]=selected<<4;
        CHECK(gfinit_exec_run(&core,&execution)==0 && wait_total==selected+1 && actual_count==1);
    }
    output("32/4A: all eight selection branches\n");
}
static void force_reset_test(void)
{
    setup();fixture();
    decoded=(struct gfinit_decode){0};
    decoded.plan=(struct recover_plan){.version=1,.rom_size=sizeof(rom),.nodes=decoded.nodes,.args=decoded.args,
        .entries=decoded.entries,.entry_count=1,.ctx_count=1,.captures=decoded.captures,.capture_count=1};
    decoded.entries[0]=(struct recover_plan_entry){REC_RUN,0,0};decoded.instruction_count=1;decoded.schedule_count=1;
    CHECK(gfinit_decode_instruction(rom,sizeof(rom),0xdf10,&decoded.insns[0])==0);
    decoded.insns[0].effect_first=14;
    CHECK(expand_step(&decoded,rom,&gf_fixture_profile,0,1)==0);
    u32 last=decoded.plan.node_count-1;decoded.nodes[last].next[0]=decoded.nodes[last].next[1]=RECOVER_NONE;
    synthetic_profile();CHECK(contract_bind()==0);exec_arm();
    for(u32 i=0;i<profile.target_count;i++)reference_values[i]=values[i]=0;
    u32 saved=ref_key(0x184c,0,0);reference_values[saved]=values[saved]=0xa5a50f55;
    ref_head=0;ref_skip=1;rom[0xdf1d]=0x71;reference(0xdf10,0);
    CHECK(gfinit_exec_run(&core,&execution)==0);compare_trace();
    CHECK(wait_total==10 && values[saved]==0xa5a50f55 && actual_count==6);
    output("RESET forced during skip: clear/write/wait/write/restore/zero, capture preserved\n");
}

#ifndef GF_S3_ENTRY
#define GF_S3_ENTRY _start
#endif
void GF_S3_ENTRY(void)
{
    gf_fixture_init();
    all_trace();log_tests();depth_tests();budget_tests();condition_tests();selection_tests();force_reset_test();failure_tests();frame_mismatch_tests();pre_read_tests();
    output(failures?"GF S3 FAIL\n":"GF S3 PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(failures?1:0));__builtin_unreachable();
}
