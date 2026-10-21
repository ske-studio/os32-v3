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
    output("PLL parser: truncation at every byte, 4 entries, reg-4, duplicate/missing/version/ranges/ref\n");
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
    profile.layouts=recover_gf_template.layouts;profile.layout_count=recover_gf_template.layout_count;
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
    output("PLL production rules: core 6 / memory 15 / video 5 writes, source captures, failure at every write, ordered dependencies\n");
}
void _start(void)
{
    pll_parser_tests();pll_solver_tests();pll_execution_tests();
    output(failures?"GF S4 FAIL\n":"GF S4 PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(failures?1:0));__builtin_unreachable();
}
