/* Real command, stores and shell parser, with RAM in place of the VRAM loan. */
#include "hwb_fake.h"
#include "kdebug/pegc_command.c"
#undef PATH_MAX_LEN
#include "userland/shell/cmd_pegc.c"
static u32 client_words[PEGC_FRAME_BYTES/4], vram_words[PEGC_FRAME_BYTES/4];
static u8 heap[PEGC_FRAME_BYTES] __attribute__((aligned(MEM_PAGE_SIZE)));
static char output[16384];
static u32 outpos, clocks, loans, live, calls;
static int unavailable, busy, no_memory, fault;
static KernelAPI api;
KernelAPI *g_api = &api;
static int has(const char *s, const char *p) {
    for (; *s; ++s) { u32 i=0; while(p[i] && s[i]==p[i]) ++i; if(!p[i])return 1; }return 0;
}
void serial_puts_polled(const char *s) {
    while (*s) { CHECK(outpos+1 < sizeof(output)); output[outpos++]=*s++; }
    output[outpos]=0;
}
int serial_gate_active(void) { return busy; }
u32 cpu_loops_per_tick(void) { return 1000000; }
int pegc_diagnostic_view(struct pegc_diagnostic_view *v) {
    ++loans;
    v->client=(u8 *)client_words; v->vram=(u8 *)vram_words;
    return unavailable ? OS32_ERR_NOSYS : 0;
}
u32 pgalloc_alloc_phys(u32 owner, int pages) {
    CHECK(owner==LEDGER_OWNER_KERNEL && pages==75 && !live);
    if(no_memory)return 0;
    live=1;return (u32)(uptr)heap;
}
int pgalloc_free_n_owner(u32 owner,u32 pfn,int pages) {
    CHECK(owner==LEDGER_OWNER_KERNEL && pfn==(u32)(uptr)heap/MEM_PAGE_SIZE && pages==75 && live);
    live=0;return 1;
}
void *kmemcpy(void *d,const void *s,u32 n) {
    for(u32 i=0;i<n;i++)((u8 *)d)[i]=((const u8 *)s)[i];
    return d;
}
int sys_time_now(u32 *lo,u32 *hi) {
    *lo=(u32)-10 + clocks/2*100 + (clocks%2 ? clocks/2%8+10 : 0);*hi=0;
    ++clocks;
    if(clocks%2==0 && clocks<=64 && fault==1) vram_words[7]^=1;
    return fault==2 && clocks==2 ? OS32_ERR_IO : 0;
}
int strcmp(const char *a,const char *b) {
    while(*a && *a==*b) { ++a; ++b; }
    return (u8)*a-(u8)*b;
}
static void print(u8 a,const char *f,...) { (void)a;(void)f; }
static i32 call(u32 op,void *p,u32 n) { ++calls;CHECK(op==PEGC_OP_COMMAND);return pegc_command_call(p,n); }
static void reset(void) {
    outpos=clocks=loans=calls=0;output[0]=0;busy=unavailable=no_memory=fault=0;
    for(u32 i=0;i<PEGC_FRAME_BYTES/4;i++) {client_words[i]=0x12345678U+i;vram_words[i]=i^0xF00DBABEU;}
}
static void restored(void) {
    CHECK(!live);
    for(u32 i=0;i<PEGC_FRAME_BYTES/4;i++) {
        CHECK(client_words[i]==0x12345678U+i);CHECK(vram_words[i]==(i^0xF00DBABEU));
    }
}
static struct pegc_request request(u32 test) {
    struct pegc_request q={.version=PEGC_COMMAND_VERSION,.size=sizeof(q),.test=test};return q;
}
void _start(void) {
    HWB_TEST("pegc-command"); reset();
    CHECK(gap_loops(1000000,0)==0 && gap_loops(1000000,50)==5);
    CHECK(gap_loops(1000000,600)==60);
    CHECK(gap_loops((u32)-1,(u32)-1)==(u32)-1);
    struct pegc_request q=request(PEGC_TEST_PRESENT);
    CHECK(pegc_command_call(&q,sizeof(q))==0 && q.rc==0 && q.result_count==9);
    CHECK(clocks==128 && loans==1);
    for(u32 m=0;m<4;m++) {
        char line[80] = "PEGC phase=read method=";
        u32 n=23, j=0;
        while(names[m][j]) line[n++]=names[m][j++];
        line[n]=0;
        CHECK(has(output,line));
        struct pegc_diagnostic_view v={(u8 *)client_words,(u8 *)vram_words};
        for(u32 i=0;i<PEGC_FRAME_BYTES;i++) heap[i]=0;
        read_frame(&v,heap,m);
        for(u32 i=0;i<PEGC_FRAME_BYTES;i++) CHECK(heap[i]==((u8 *)vram_words)[i]);
    }
    CHECK(has(output,"count=8 min_us=10 max_us=17 avg_us=13 rc=0"));
    CHECK(has(output,"count=8 min_us=10 max_us=17 avg_us=13 mismatches=0 rc=0"));restored();
    reset();q=request(PEGC_TEST_PRESENT);fault=1;
    CHECK(pegc_command_call(&q,sizeof(q))==OS32_ERR_IO && q.result_count==9);
    CHECK(has(output,"mismatches=8 rc=-1") && clocks==128);restored();
    reset();q=request(PEGC_TEST_PRESENT);fault=2;
    CHECK(pegc_command_call(&q,sizeof(q))==OS32_ERR_IO && q.result_count==9);restored();
    reset();q=request(PEGC_TEST_GAP_SWEEP);q.start_ns=0;q.end_ns=125;q.step_ns=50;
    CHECK(!pegc_command_call(&q,sizeof(q)) && q.result_count==4 && clocks==48);
    CHECK(has(output,"gap_ns=100 loops=10") && !has(output,"gap_ns=150"));restored();
    reset();q=request(PEGC_TEST_GAP_SWEEP);q.start_ns=(u32)-2;q.end_ns=(u32)-1;q.step_ns=2;
    /* Exercise overflow-safe endpoint without spending billions of nops. */
    no_memory=1;CHECK(pegc_command_call(&q,sizeof(q))==OS32_ERR_NOSPC);restored();
    reset();q=request(PEGC_TEST_GAP_SWEEP);
    CHECK(pegc_command_call(&q,sizeof(q))==OS32_ERR_INVAL && !loans);
    q=request(PEGC_TEST_PRESENT);q.start_ns=1;
    CHECK(pegc_command_call(&q,sizeof(q))==OS32_ERR_INVAL && !loans);
    q=request(PEGC_TEST_PRESENT);busy=1;
    CHECK(pegc_command_call(&q,sizeof(q))==OS32_ERR_BUSY && !loans && !outpos);
    busy=0;unavailable=1;q=request(PEGC_TEST_PRESENT);
    CHECK(pegc_command_call(&q,sizeof(q))==OS32_ERR_NOSYS && !outpos);
    CHECK(pegc_command_call(0,sizeof(q))==OS32_ERR_INVAL);
    reset();api.version=78;api.kdebug_call=call;api.kprintf=print;
    char *present[]={"pegc","present"};
    CHECK(shell_cmd_pegc(2,present)==SH_STATUS_OK && calls==1);restored();
    char *sweep[]={"pegc","gap","sweep","0","100","50"};
    CHECK(shell_cmd_pegc(6,sweep)==SH_STATUS_OK && calls==2);restored();
    sweep[5]="0";CHECK(shell_cmd_pegc(6,sweep)==SH_STATUS_USAGE && calls==2);
    sweep[5]="4294967296";CHECK(shell_cmd_pegc(6,sweep)==SH_STATUS_USAGE && calls==2);
    sweep[5]="-1";CHECK(shell_cmd_pegc(6,sweep)==SH_STATUS_USAGE);
    api.version=77;CHECK(shell_cmd_pegc(2,present)==SH_STATUS_ERROR && calls==2);
    hwb_test_write("GREEN PEGC transfers, timing, mismatch, cleanup, sweep and shell\n");hwb_test_exit(0);
}
