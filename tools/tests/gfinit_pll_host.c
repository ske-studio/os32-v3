/* S4 uses the production PLL rules and executor with fake MMIO. */
#define GF_S3_ENTRY s3_entry
#include "gfinit_exec_host.c"
#undef GF_S3_ENTRY
static void pll_parser_tests(void)
{
    struct gfinit_pll_limits l;
    fixture();
    u32 end=GF_INIT_PLL_TABLE+4+4*35;
    for(u32 size=0;size<end;size++)CHECK(gfinit_pll_parse(rom,size,GF_INIT_PLL_TABLE,GF_PLL_CORE,&l)<0);
    CHECK(gfinit_pll_parse(rom,end,GF_INIT_PLL_TABLE,GF_PLL_CORE,&l)==0 && l.ref==27000);
    CHECK(gfinit_pll_parse(rom,sizeof(rom),~0U,GF_PLL_CORE,&l)<0);
    CHECK(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,0x402c,&l)<0);
    for(u32 bad=0;bad<12;bad++) {
        fixture();u32 at=GF_INIT_PLL_TABLE+4;
        switch(bad) {
        case 0:put(GF_INIT_PLL_TABLE,0x20,1);break;
        case 1:put(GF_INIT_PLL_TABLE+1,3,1);break;
        case 2:put(GF_INIT_PLL_TABLE+2,30,1);break;
        case 3:put(GF_INIT_PLL_TABLE+3,3,1);break;
        case 4:put(at+35,GF_PLL_CORE-4,4);break;
        case 5:put(at+31,25000,4);break;
        case 6:put(at+22,0,1);break;
        case 7:put(at+20,0,1);break;
        case 8:put(at+4,501,2);break;
        case 9:put(at+12,28,2);break;
        case 10:put(at+29,8,1);break;
        case 11:put(at+30,8,1);break;
        }
        CHECK(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_CORE,&l)<0);
    }
    fixture();put(GF_INIT_PLL_TABLE+4+31,27000,4);
    CHECK(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_CORE,&l)==0);
    /* No matching key uses the first entry (nouveau pll.c:162-175, the Ra266 ROM shape);
     * a matching key still selects its own entry. */
    fixture();put(GF_INIT_PLL_TABLE+4,0,4);put(GF_INIT_PLL_TABLE+4+29,5,1);put(GF_INIT_PLL_TABLE+4+2*35+29,6,1);
    CHECK(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_CORE,&l)==0 && l.max_p==5);
    CHECK(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&l)==0 && l.max_p==6);
    output("PLL parser: truncation at every byte, 4 entries, reg-4, duplicate/missing/version/ranges/ref, first entry when no key\n");
}
static void pll_solver_tests(void)
{
    fixture();struct gfinit_pll_limits l;struct gfinit_pll_solution s;
    CHECK(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_CORE,&l)==0);
    const u32 requests[]={200000,400000,36000};
    for(u32 i=0;i<3;i++) {
        CHECK(gfinit_pll_solve(&l,requests[i],&s)==0);
        output("PLL kHz=");number(requests[i]);output(" N/M/N/M/P=");number(s.n1);output("/");number(s.m1);
        output("/");number(s.n2);output("/");number(s.m2);output("/");number(s.p);
        output(" actual/error=");number(s.actual);output("/");number(s.error);output("\n");
        CHECK(s.n2==1 && s.m2==1 && s.error<=100);
    }
    CHECK(gfinit_pll_solve(&l,400000,&s)==0 && s.n1==237 && s.m1==16 && s.actual==399938 && s.error==62);
    CHECK(gfinit_pll_solve(&l,36000,&s)==0 && s.n1==16 && s.m1==3 && s.p==2 && s.actual==36000);
    l.vco1=(struct gfinit_pll_vco){1000000,2000000,1000,27000,1,255,1,27};
    CHECK(gfinit_pll_solve(&l,1000,&s)==0 && s.p==3 && s.actual==1000);
    l.vco1=(struct gfinit_pll_vco){1,1000000,14000,27000,1,255,1,27};
    CHECK(gfinit_pll_solve(&l,148500,&s)==0 && s.p==0 && s.n1==6 && s.m1==1 && s.actual==162000);
    /* Isolate each of the two single-stage rounding additions. */
    l.vco1=(struct gfinit_pll_vco){1,1000000,1000,27000,1,255,7,7};
    CHECK(gfinit_pll_solve(&l,10000,&s)==0 && s.n1==83 && s.p==5 && s.actual==10004 && s.error==4);
    l.vco1=(struct gfinit_pll_vco){100000,500000,27000,27000,1,255,1,1};
    CHECK(gfinit_pll_solve(&l,148500,&s)==0 && s.n1==11 && s.p==1 && s.actual==148500);
    CHECK(gfinit_pll_solve(&l,0,&s)<0 && gfinit_pll_solve(&l,~0U,&s)<0);
    l.vco1.min_n=l.vco1.max_n=255;l.vco1.min_m=l.vco1.max_m=255;
    CHECK(gfinit_pll_solve(&l,200000,&s)<0); /* input frequency cannot fit */
    l.vco1=(struct gfinit_pll_vco){100000,400000,1000,27000,1,255,1,27};
    l.vco2=(struct gfinit_pll_vco){400000,1000000,25000,200000,4,40,1,4};
    for(u32 i=0;i<3;i++) {
        CHECK(gfinit_pll_solve(&l,requests[i],&s)==0);
        CHECK(s.n2/s.m2>=4 && s.n2/s.m2<=10 && s.error<=100);
        u32 stage1=27000*s.n1/s.m1, stage2=stage1*s.n2/s.m2;
        CHECK(s.actual==(stage2>>s.p));
        output("PLL double kHz=");number(requests[i]);output(" actual/error=");number(s.actual);output("/");number(s.error);output("\n");
    }
    struct gfinit_pll_limits rejected=l;
    rejected.vco2.max_input=400000;
    rejected.vco2.min_n=2;rejected.vco2.max_n=3;rejected.vco2.min_m=rejected.vco2.max_m=1;
    CHECK(gfinit_pll_solve(&rejected,200000,&s)<0); /* forbidden NV4x gain */
    l.vco2.min_n=l.vco2.max_n=1;l.vco2.min_m=l.vco2.max_m=1;
    l.vco2.max_freq=400000;l.vco2.max_input=400000;
    CHECK(gfinit_pll_solve(&l,108000,&s)==0 && s.n2==1 && s.m2==1);
    l.vco2.max_freq=0;l.vco1.max_input=0;CHECK(gfinit_pll_solve(&l,200000,&s)<0);
    struct gfinit_pll_sequence full={.count=GF_PLL_WRITES_MAX};
    pll_write(&full,0,0,0,0);CHECK(full.count==GF_PLL_WRITES_MAX);
    output("PLL solver: single/double, staged rounding, fixed gain, no solution, zero/overflow/input bounds\n");
}
static u32 pll_double;
static void pll_plan(u32 pc)
{
    setup();fixture();
    if(pll_double) for(u32 i=0;i<4;i++) {
        u32 at=GF_INIT_PLL_TABLE+4+35*i;
        put(at+6,400,2);put(at+8,400,2);put(at+10,1000,2);put(at+14,25,2);put(at+18,200,2);
        put(at+24,4,1);put(at+25,40,1);put(at+26,1,1);put(at+27,4,1);
    }
    profile.layouts=gf_fixture_profile.layouts;profile.layout_count=gf_fixture_profile.layout_count;
    decoded=(struct gfinit_decode){0};
    u32 main=pc==0xe623?2:0;
    decoded.plan=(struct recover_plan){.version=1,.rom_size=sizeof(rom),.nodes=decoded.nodes,.args=decoded.args,
        .entries=decoded.entries,.entry_count=1,.ctx_count=1,.captures=decoded.captures};
    for(u32 i=0;i<main;i++) {
        decoded.entries[i]=(struct recover_plan_entry){REC_RUN,i,i};
        decoded.nodes[i]=(struct recover_plan_node){.phase=REC_RUN,.main_id=i,.pc=0xe832,.opcode=0x71,
            .substep=GF_INIT_C,.rule_id=RECOVER_NONE,.expr_id=RECOVER_NONE,.next={i+1,i+1}};
    }
    decoded.plan.node_count=main;decoded.plan.entry_count=main+1;
    decoded.entries[main]=(struct recover_plan_entry){REC_RUN,main,main};
    CHECK(gfinit_decode_instruction(rom,sizeof(rom),pc,&decoded.insns[0])==0);
    decoded.schedule[0]=(struct gfinit_scheduled){.main_id=main};
    CHECK(expand_pll(&decoded,rom,&profile,&decoded.schedule[0],0,pc==0xe623?36000:pc==0xe728?200000:400000)==0);
    u32 last=decoded.plan.node_count-1;decoded.nodes[last].next[0]=decoded.nodes[last].next[1]=RECOVER_NONE;
    CHECK(decoded.plan.unresolved_count==0 && contract_bind()==0);
    exec_arm();
    for(u32 i=0;i<main;i++){execution.main_id=i;CHECK(gfinit_exec_run(&core,&execution)==0);}
    execution.main_id=main;
    for(u32 i=0;i<profile.target_count;i++)reference_values[i]=values[i]=i<4?0:0xa5a55a5a;
    ref_head=ref_skip=0;
}
static u32 write_attempt, fail_at;
static int pll_fail_write(void *v,const struct recover_bus_access *a,u32 value)
{
    if(core.state!=REC_RUNNING) return write_fake(v,a,value);
    if(write_attempt++==fail_at)return -1;
    return exec_bus_write(v,a,value);
}
/* Model Ra266's 4600h readback independently of its write latch. */
static u32 pll_reads[RECOVER_NODE_MAX], pll_old[RECOVER_NODE_MAX];
static u32 mismatch_key, mismatch_after, mismatch_seen;
static int pll_readback(void *v,const struct recover_bus_access *a,u32 *value)
{
    int rc=exec_bus_read(v,a,value);
    if(a->offset==GF_PLL_MEMORY_POWER)*value=0;
    if(core.state==REC_RUNNING && core.cursor<decoded.plan.node_count &&
       a->key==decoded.args[decoded.nodes[core.cursor].arg_first].value) {
        pll_reads[core.cursor]++;pll_old[core.cursor]=*value;
    }
    if(core.state==REC_RUNNING && a->key==mismatch_key && actual_count>=mismatch_after) {
        if(!mismatch_seen)mismatch_seen=actual_count;
        *value^=1U;
    }
    return rc;
}
/* Retain one complete BEGIN line, discarding unrelated capture/guard logs. */
static int pll_log_put(void *opaque,u32 byte)
{
    int rc=exec_log_put(opaque,byte);
    if(byte=='\n' && !exec_log_has("phase=gf_bus event=BEGIN")) {
        exec_log_n=0;exec_log[0]=0;
    }
    return rc;
}
static const struct recover_transport pll_transport={
    .get=exec_log_get,.uart={exec_log_ready,pll_log_put,exec_log_wait},.frame_us=1042,.max_polls=100000
};
/* BEGIN must contain the actual pre-write read, before the bus write occurs. */
static int pll_logged_write(void *v,const struct recover_bus_access *a,u32 value)
{
    if(core.state!=REC_RUNNING)return exec_bus_write(v,a,value);
    u32 node=core.cursor;
    CHECK(node<decoded.plan.node_count);
    if(node>=decoded.plan.node_count)return -1;
    CHECK(pll_reads[node]>=1);
    CHECK(exec_log_has("phase=gf_bus event=BEGIN"));
    struct rec_row row={.n=0};
    row_number(&row," pc=",decoded.nodes[node].pc);
    row_number(&row," bar=",a->bar);row_number(&row," off=",a->offset);
    row_number(&row," width=",a->width);row_number(&row," old=",pll_old[node]);
    row_number(&row," new=",value);row_text(&row,"\n");row.data[row.n]=0;
    CHECK(exec_log_has(row.data));
    /* Clear only the host buffer so each write's BEGIN is checked separately. */
    exec_log_n=0;exec_log[0]=0;
    return exec_bus_write(v,a,value);
}
static void pll_readback_tests(void)
{
    const u32 pcs[]={0xe728,0xe754,0xe623},counts[]={6,15,5};
    for(pll_double=0;pll_double<2;pll_double++) for(u32 k=0;k<3;k++) {
        pll_plan(pcs[k]);
        u32 power=key_for(&profile,GF_PLL_MEMORY_POWER,0,0,0);
        reference_values[power]=0;
        reference_pll(k==2?GF_PLL_VIDEO:k?GF_PLL_MEMORY:GF_PLL_CORE,k==2?36000:k?400000:200000);
        mismatch_key=RECOVER_NONE;mismatch_after=0;
        for(u32 i=0;i<RECOVER_NODE_MAX;i++)pll_reads[i]=pll_old[i]=0;
        exec_log_n=0;exec_log[0]=0;exec_log_busy=false;core.transport=&pll_transport;
        executor_bus.read=pll_readback;executor_bus.write=pll_logged_write;
        CHECK(gfinit_exec_run(&core,&execution)==0);compare_trace();
        CHECK(actual_count==counts[k]);
        u32 writes=0,reads=0;
        for(u32 i=0;i<decoded.plan.node_count;i++) {
            reads+=pll_reads[i];
            if(decoded.nodes[i].rule_id==RECOVER_NONE)continue;
            const struct recover_rule *rule=&profile.rules[decoded.nodes[i].rule_id];
            CHECK(rule->verify_mask==0 && profile.targets[rule->key].read_safe);
            /* One old read, with no extra verification read after the write. */
            CHECK(pll_reads[i]==1);writes++;
            u32 old=rule->key==power?0:0xa5a55a5a;
            for(u32 j=0;j+1<writes;j++)
                if(expected[j].key==rule->key && rule->key!=power)old=expected[j].value;
            CHECK(pll_old[i]==old);
        }
        /* Captures (gate/control, plus MEM power) still precede the old reads. */
        CHECK(writes==counts[k] && reads==counts[k]+(k==1?3U:k==2?4U:2U));
        CHECK(values[key_for(&profile,GF_PLL_GATE,0,0,0)]==0xa5a55a5a);
        if(k==1) {
            CHECK(actual[0].key==power && actual[0].value==0x80000000U);
            CHECK(actual[11].key==power && actual[11].value==0 && values[power]==0);
        }
        core.transport=0;
    }
    output("PLL readback: CORE/MEM/VIDEO 26 old reads and gf_bus old fields, no verify reads; ordered writes/restores unchanged\n");
}
/* Expand a single production instruction, preserving all its writes and captures. */
static void copy_restore_plan(u32 pc,u32 main,u32 head)
{
    setup();fixture();profile.layouts=gf_fixture_profile.layouts;profile.layout_count=gf_fixture_profile.layout_count;
    decoded=(struct gfinit_decode){0};
    decoded.plan=(struct recover_plan){.version=1,.rom_size=sizeof(rom),.nodes=decoded.nodes,.args=decoded.args,
        .entries=decoded.entries,.entry_count=main+1,.ctx_count=1,.captures=decoded.captures};
    for(u32 i=0;i<main;i++) {
        decoded.entries[i]=(struct recover_plan_entry){REC_RUN,i,i};
        decoded.nodes[i]=(struct recover_plan_node){.phase=REC_RUN,.main_id=i,.pc=0xe832,.opcode=0x71,
            .substep=GF_INIT_C,.rule_id=RECOVER_NONE,.expr_id=RECOVER_NONE,.next={i+1,i+1}};
    }
    decoded.plan.node_count=main;decoded.entries[main]=(struct recover_plan_entry){REC_RUN,main,main};
    if(head) {
        CHECK(gfinit_decode_instruction(rom,sizeof(rom),0xe69a,&decoded.insns[0])==0);
        for(u32 i=0;i<GF_ARRAY(effect_spans);i++)if(effect_spans[i].pc==0xe69a)
            decoded.insns[0].effect_first=effect_spans[i].first;
        decoded.schedule[0]=(struct gfinit_scheduled){.main_id=main};decoded.schedule_count=1;
        CHECK(expand_step(&decoded,rom,&profile,0,0)==0);
        u32 last=decoded.plan.node_count-1;decoded.nodes[last].next[0]=decoded.nodes[last].next[1]=last+1;
    }
    CHECK(gfinit_decode_instruction(rom,sizeof(rom),pc,&decoded.insns[0])==0);
    for(u32 i=0;i<GF_ARRAY(effect_spans);i++)if(effect_spans[i].pc==pc)
        decoded.insns[0].effect_first=effect_spans[i].first;
    decoded.schedule[0]=(struct gfinit_scheduled){.main_id=main};decoded.schedule_count=1;
    CHECK(expand_step(&decoded,rom,&profile,0,head*GF_STATE_HEAD1)==0);
    if(pc==0xdf10 || pc==0xe67f || pc==0xe6c0) {
        u32 slot=pc==0xdf10?0:1+head;
        decoded.captures[0]=decoded.captures[slot];decoded.captures[0].slot=profile.count;
        decoded.plan.capture_count=1;
        for(u32 i=0;i<decoded.plan.arg_count;i++)if(decoded.args[i].kind==REC_ARG_SAVED)
            decoded.args[i].value=profile.count;
    }
    u32 last=decoded.plan.node_count-1;decoded.nodes[last].next[0]=decoded.nodes[last].next[1]=RECOVER_NONE;
    CHECK(decoded.plan.unresolved_count==0 && contract_bind()==0);exec_arm();
    for(u32 i=0;i<main;i++){execution.main_id=i;CHECK(gfinit_exec_run(&core,&execution)==0);}
    execution.main_id=main;
    for(u32 i=0;i<profile.target_count;i++)reference_values[i]=values[i]=0;
    ref_head=head;ref_skip=0;
}
static u32 observed_rule;
static int restore_logged_write(void *v,const struct recover_bus_access *a,u32 value)
{
    if(decoded.nodes[core.cursor].rule_id==observed_rule)return pll_logged_write(v,a,value);
    exec_log_n=0;exec_log[0]=0;
    return exec_bus_write(v,a,value);
}
static void copy_restore_tests(void)
{
    const u32 pcs[]={0xdf10,0xe67f,0xe6c0,0xe7fb,0xdfaf};
    const u32 mains[]={0,2,2,3,0},heads[]={0,0,1,0,0},ids[]={573,575,576,577,574};
    for(u32 k=0;k<5;k++) {
        copy_restore_plan(pcs[k],mains[k],heads[k]);
        /* Execute only this instruction in the independent reference. */
        put(pcs[k]+decoded.insns[0].length,0x71,1);
        if(heads[k]){ref_head=0;ref_index(RECOVER_CRTC,RECOVER_CR44,0,3);}
        reference(pcs[k],0);
        observed_rule=ids[k];mismatch_key=RECOVER_NONE;mismatch_after=0;
        for(u32 i=0;i<RECOVER_NODE_MAX;i++)pll_reads[i]=pll_old[i]=0;
        exec_log_n=0;exec_log[0]=0;exec_log_busy=false;core.transport=&pll_transport;
        executor_bus.read=pll_readback;executor_bus.write=restore_logged_write;
        int rc=gfinit_exec_run(&core,&execution);
        if(rc){output("copy pc/reason/cursor ");number(pcs[k]);output("/");number(core.reason);output("/");number(core.cursor);output("\n");}
        CHECK(rc==0);compare_trace();
        for(u32 i=0;i<decoded.plan.node_count;i++)if(decoded.nodes[i].rule_id==observed_rule) {
            CHECK(pll_reads[i]==(k==4?3U:k==3?2U:1U));
            CHECK(pll_old[i]==(k==1?6U:k==2?2U:0U));
            CHECK(profile.rules[observed_rule].verify_mask==(k==4?255U:0U));
        }
        core.transport=0;
    }
    copy_restore_plan(0xdfaf,0,0);
    mismatch_key=profile.rules[574].key;mismatch_after=2;mismatch_seen=0;
    executor_bus.read=pll_readback;
    CHECK(gfinit_exec_run(&core,&execution)<0 && core.reason==REC_VERIFY);
    CHECK(core.first_key==mismatch_key && mismatch_seen==2);
    u32 nonzero=0;
    for(u32 i=0;i<profile.rule_count;i++)if(profile.rules[i].main_id==RECOVER_NONE && profile.rules[i].verify_mask)nonzero++;
    CHECK(nonzero==31);
    output("RESET/CR57/COPY: old reads/logs retained, ordered writes/restores unchanged; CR3C mismatch stops; NONE nonzero masks 31\n");
}
static void pll_execution_tests(void)
{
    const u32 pcs[]={0xe728,0xe754,0xe623},counts[]={6,15,5};
    for(pll_double=0;pll_double<2;pll_double++) for(u32 k=0;k<3;k++) {
        pll_plan(pcs[k]);
        reference_pll(decoded.insns[0].target,k==0?200000:k==1?400000:36000);
        CHECK(gfinit_exec_run(&core,&execution)==0);compare_trace();
        CHECK(actual_count==counts[k] && !core.capture_valid);
        CHECK(values[key_for(&profile,GF_PLL_GATE,0,0,0)]==0xa5a55a5a);
        if(k==1)CHECK(values[key_for(&profile,GF_PLL_MEMORY_POWER,0,0,0)]==0xa5a55a5a);
        for(u32 j=0;j<counts[k];j++) {
            pll_plan(pcs[k]);fail_at=j;write_attempt=0;executor_bus.write=pll_fail_write;
            CHECK(gfinit_exec_run(&core,&execution)<0 && write_attempt==j+1 && actual_count==j);
        }
        pll_plan(pcs[k]);
        /* The production rule chain refuses a missing predecessor. */
        release_plan(&core);core.state=REC_IDLE;
        u32 first=0;while(decoded.nodes[first].rule_id==RECOVER_NONE)first++;
        decoded.nodes[first-1].next[0]=decoded.nodes[first-1].next[1]=first+1;
        CHECK(contract_bind()<0);
    }
    pll_readback_tests();copy_restore_tests();
    output("PLL production rules: core 6 / memory 15 / video 5 writes, source captures, failure at every write, ordered dependencies\n");
}
void _start(void)
{
    gf_fixture_init();
    pll_parser_tests();pll_solver_tests();pll_execution_tests();
    output(failures?"GF S4 FAIL\n":"GF S4 PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(failures?1:0));__builtin_unreachable();
}
