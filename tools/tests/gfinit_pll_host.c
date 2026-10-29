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
static void pll_sequence_tests(void)
{
    fixture();
    struct gfinit_pll_limits l;
    struct gfinit_pll_sequence q;
    const struct gfinit_pll_solution solution={.n1=26,.m1=7,.n2=1,.m2=1,.p=2,.actual=25071};
    const u32 core[]={0x4000,0x4000,0xc040,0x4004,0x4000,0xc040};
    const u32 memory[]={0x4600,0x4020,0x4020,0x4020,0x4038,0xc040,0x4024,0x403c,
                        0x4020,0x4020,0x4038,0x4600,0xc040,0x4020,0x4038};
    const u32 video[]={0xc040,0x680580,0x680578,0x680508,0xc040};
    const u32 regs[]={0x4004,0x4024,0x680508}, counts[]={6,15,5};
    const u32 *offsets[]={core,memory,video};
    for(u32 i=0;i<3;i++) {
        CHECK(!gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,regs[i],&l));
        CHECK(!gfinit_pll_sequence(regs[i],&l,&solution,&q) && q.count==counts[i]);
        for(u32 j=0;j<q.count;j++)CHECK(q.writes[j].reg==offsets[i][j]);
        u32 restore=i==1?12:counts[i]-1;
        CHECK(q.writes[restore].source==0xc040 && q.writes[restore].keep==~0U && !q.writes[restore].value);
        if(i==1)CHECK(q.writes[4].value==q.writes[3].value &&
                      q.writes[10].value==q.writes[9].value &&
                      q.writes[14].source==0x4020 && q.writes[14].value==q.writes[13].value &&
                      q.writes[14].keep==q.writes[13].keep);
    }
    output("PLL core/memory/video literal order, mirror and gate restoration PASS\n");
}
void _start(void)
{
    gf_fixture_init();pll_parser_tests();pll_solver_tests();pll_sequence_tests();
    output(failures?"GF PLL FAIL\n":"GF PLL PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(failures?1:0));__builtin_unreachable();
}
