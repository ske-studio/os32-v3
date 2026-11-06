/* Real C0 loader, all bus/clock/wait fault positions, libc-free. */
#include "gf_startlog_host.h"
#define _start unused_c0_start
#include "gf4_c0_host.c"
#undef _start
#define INJECT (-1701)
static u32 number(const char *key,u32 base)
{
    const char *p=gf2_host_strstr(startlog,key);
    assert(p); p+=gf2_host_strlen(key);
    u32 n=0;
    while ((*p>='0' && *p<='9') || (base==16 && *p>='a' && *p<='f')) {
        n=n*base+(*p<='9' ? *p-'0' : *p-'a'+10); p++;
    }
    return n;
}
static void one_line(int rc)
{
    const char *load=gf2_host_strstr(startlog,"[gf4] mode_load fail at=");
    assert(load && (rc==GF4_ETIME || load==startlog));
    unsigned lines=0;
    for (unsigned i=0;i<startlog_len;i++) lines+=startlog[i]=='\n';
    assert(lines==(rc==GF4_ETIME ? 2U : 1U) && startlog[startlog_len-1]=='\n');
    assert((int)number(" rc=-",10)==-rc);
}
static int invoke(void)
{ return gf4_mode_load(&host.ctx,T_HEAD_B,&state.work[T_HEAD_B],T_DEADLINE); }
static void ready(bool fp)
{
    setup();
    assert(!gf4_change_begin(&host.ctx,T_DEADLINE));
    if (fp) gf3_host_set(&host.base,0,0x682848U,0x10000011U);
    gf4_host_trace_clear(&host);
    startlog_clear();
}
static void sweep(void)
{
    for (u32 fp=0;fp<2;fp++) {
        ready(fp); assert(!invoke()); assert(startlog_equal(""));
        assert(host.local_now==(fp ? 70000U : 20000U));
        u32 counts[D_OP_COUNT];
        for (u32 op=0;op<D_OP_COUNT;op++) counts[op]=host.calls[op];
        const enum gf4_host_op ops[]={D_READ,D_WRITE,D_CLOCK,D_SLEEP,D_LUT};
        for (u32 op=0;op<sizeof(ops)/sizeof(ops[0]);op++) {
            for (u32 nth=1;nth<=counts[ops[op]];nth++) {
                ready(fp); gf4_host_fail_at(&host,ops[op],nth,INJECT);
                assert(invoke()==INJECT); one_line(INJECT);
                assert(number(" l.rc=-",10)==-INJECT);
                if (ops[op]==D_READ || ops[op]==D_WRITE) {
                    const struct gf4_host_event *e=0;
                    for (u32 i=0;i<host.nevents;i++)
                        if (events[i].op==ops[op] && events[i].nth==nth) e=&events[i];
                    assert(e && e->rc==INJECT);
                    assert(number(" reg=",16)==e->a[1]);
                    assert(number(" val=",16)==(ops[op]==D_WRITE ? e->a[3] : 0));
                }
                u32 row=number(" row=",10),sec=number(" sec=",10);
                if (row!=GF_LOAD_NO_INDEX) {
                    assert(row<gf_mode_row_count);
                    /* Frozen section boundaries from shared load contract. */
                    assert(sec==(row<2 ? GF_LOAD_Q : row<15 ? GF_LOAD_P :
                        row<26 ? GF_LOAD_SD : row<46 ? GF_LOAD_L1 :
                        row<120 ? GF_LOAD_L2 : row<190 ? GF_LOAD_L3 : GF_LOAD_COMMIT));
                }
            }
        }
    }
}
static void cleanup_fault(struct gf4_host *h,const struct gf4_host_event *e)
{
    if (e->op==D_LUT && e->rc) {
        gf4_host_fail_at(h,D_WRITE,0,-1702); /* gate + owner both fail */
    }
}
static void expire_wait(struct gf4_host *h,const struct gf4_host_event *e)
{
    if (e->op==D_SLEEP && !e->rc) h->local_now=T_DEADLINE;
}
static void focused(void)
{
    ready(false); gf4_host_fail_at(&host,D_CLOCK,1,-1);
    assert(invoke()==-1);
    assert(startlog_equal("[gf4] mode_load fail at=clock sec=4294967295 row=4294967295 reg=00000000 val=00000000 rc=-1 l.rc=-1 gate_rc=0 owner_rc=0\n"));
    ready(false); host.observe=cleanup_fault; gf4_host_fail_at(&host,D_LUT,1,INJECT);
    assert(invoke()==INJECT); one_line(INJECT);
    assert(gf2_host_strstr(startlog,"at=lut_load sec=5 row=120"));
    assert(number(" l.rc=-",10)==-INJECT);
    assert(number(" reg=",16)==0x6833c6U);
    assert(gf2_host_strstr(startlog,"gate_rc=-1702 owner_rc=-1702"));
    ready(false); host.local_now=T_DEADLINE-5000U;
    assert(invoke()==GF4_ETIME); one_line(GF4_ETIME);
    assert(gf2_host_strstr(startlog,"at=wait_budget sec=1 row=5 reg=00000000 val=00002710"));
    assert(gf2_host_strstr(startlog,"GF4 ERROR stage=mode_wait_budget rc=-409 head=1 remain_us=5000 reg=00000000 val=00002710\n"));
    ready(false); host.observe=expire_wait;
    assert(invoke()==GF4_ETIME); one_line(GF4_ETIME);
    assert(gf2_host_strstr(startlog,"at=wait_deadline sec=1 row=5"));
    ready(false); host.local_now=T_DEADLINE;
    assert(invoke()==GF4_ETIME); one_line(GF4_ETIME);
    assert(gf2_host_strstr(startlog,"at=deadline"));
    ready(false); host.ctx.deadline=T_DEADLINE-1;
    assert(invoke()==GF4_ESTATE); one_line(GF4_ESTATE);
    ready(false);
    assert(gf4_mode_load(0,0,0,0)==GF4_EINVAL); one_line(GF4_EINVAL);
}
/* Section callbacks can fail too: preserve their rc, including the initial Q. */
static u32 fail_section;
static int section(void *p,u32 sec) { (void)p; return sec==fail_section ? INJECT : 0; }
static int row(void *p,const struct gfinit_mode *m,u32 i) { (void)p;(void)m;(void)i;return 0; }
static int wait(void *p,u32 ms,bool fp) { (void)p;(void)ms;(void)fp;return 0; }
void _start(void)
{
    sweep(); focused();
    const struct gf_load_io io={section,row,wait};
    struct gfinit_mode m={0};
    for (fail_section=GF_LOAD_Q;fail_section<=GF_LOAD_COMMIT;fail_section++)
        assert(gf_load_state(&m,0,&io)==INJECT);
    gf2_host_exit(0);
}
