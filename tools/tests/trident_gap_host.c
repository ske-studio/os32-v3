/* Production conversion, MMIO span, next-frame updates and wire sweep. */
#include "kdebug/trident_command_test.c"
void *kmalloc(u32 size) { (void)size; return 0; }
void kfree(void *p) { (void)p; }
static u32 lfb[16];
static u8 client[64];
static struct trident_resources r;
static struct trident_device d;
static struct TridentDiagnosticOps ops;
static u32 observed, expected_loops, calls[3], clock_us, lines, output_bad;
static int watching, failed, sweeping, read_failure;
void gap_observe(void)
{
    if (!watching) return;
    u32 index = observed / expected_loops;
    if (index >= 2 || lfb[index] != 0x04030201u + index * 0x04040404u ||
        (index == 0 && lfb[1])) failed = 1;
    ++observed;
}
int tri_sync(struct trident_device *device) { return device ? 0 : TRI_E_INVAL; }
int sys_time_now(u32 *lo, u32 *hi) { *lo=clock_us; *hi=0; clock_us+=10; return 0; }
int serial_gate_active(void) { return 0; }
static int same(const char *a, const char *b)
{ while (*a && *a==*b) { ++a; ++b; } return !*a && !*b; }
void serial_puts_polled(const char *s)
{
    if (same(s,"TRI phase=gap gap_ns=")) ++lines;
    if (same(s," mismatches=")) output_bad=1;
    else if (output_bad) {
        if (!same(s, lines==1 && !read_failure ? "64" : "0")) failed=1;
        output_bad=0;
    }
}
const struct TridentDiagnosticOps *trident_diagnostic_ops(void) { return &ops; }
static int readback(void *opaque, enum trident_space space, u32 off, u8 *v)
{
    (void)opaque;
    if (space!=TRI_SPACE_LFB || off>=8) return TRI_E_ACCESS;
    if (read_failure) return TRI_E_ACCESS;
    *v=((u8 *)lfb)[off];
    if (sweeping && d.write_gap_ns==0) *v^=1;
    return 0;
}
static int draw(struct trident_device *device, int x, int y, int w, int h)
{
    if (sweeping) {
        if (device->present_method!=TRI_PRESENT_DWORD_GAP || device->write_gap_ns>100 ||
            device->write_gap_ns%50) failed=1;
        else {
            u32 at=device->write_gap_ns/50;
            u8 expected=(calls[at]%2u) ? 1u : 254u;
            if (!read_failure && device->res->client[0]!=expected) failed=1;
            ++calls[at];
        }
    }
    return trident_present(device,x,y,w,h);
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int test(void)
{
    const struct {u32 lpt, ns, loops;} vectors[] = {
        {1000000,300,30},{1000000,0,0},{640000,50,3},
        {1,9999999,0},{1,10000000,1},{0xffffffffu,1,429},
        {0xffffffffu,10000000,0xffffffffu},
        {0xffffffffu,0xffffffffu,0xffffffffu},{0,0xffffffffu,0}
    };
    for (u32 i=0;i<sizeof(vectors)/sizeof(vectors[0]);++i) {
        d.write_gap_lpt=vectors[i].lpt;
        tri_write_gap_set(&d,vectors[i].ns);
        CHECK(d.write_gap_ns==vectors[i].ns && d.write_gap_loops==vectors[i].loops);
    }
    for(u32 i=0;i<sizeof(client);++i) client[i]=(u8)(i+1);
    r=(struct trident_resources){.ready=true,.lfb=(u8 *)lfb,.client=client,
        .mapped_bytes=sizeof(lfb),.decode_bytes=sizeof(lfb),.client_bytes=sizeof(client),
        .ram_fallback=true,.generation=1};
    d=(struct trident_device){.res=&r,.declared_bytes=sizeof(lfb),.write_gap_lpt=1000000,
        .present_method=TRI_PRESENT_DWORD_GAP,.active=true,.screen_ready=true};
    d.mode.client=(struct tri_surface){.width=8,.height=1,.cpp=1,.pitch=8,.bytes=8};
    d.mode.display=d.mode.client; d.mode.valid=true; d.mode.generation=1;
    CHECK(!trident_regs_bind(&d));
    struct trident_io io=*d.io;
    io.read8=readback; d.io=&io;
    ops=(struct TridentDiagnosticOps){.version=TRIDENT_DIAGNOSTIC_OPS_VERSION,
        .size=TRIDENT_DIAGNOSTIC_OPS_SIZE,.context=&d,.present=draw,.set_gap=tri_write_gap_set};
    const u32 gaps[]={300,0,50};
    for (u32 i=0;i<3;++i) {
        struct tri_request q={.version=TRI_COMMAND_VERSION,.size=sizeof(q),
            .test=TRI_TEST_GAP_SET,.arg=gaps[i]};
        CHECK(!trident_command_call(&q,sizeof(q)));
        CHECK(d.write_gap_ns==gaps[i] && d.write_gap_loops==gaps[i]/10);
        lfb[0]=lfb[1]=0; observed=0; expected_loops=gaps[i]/10; watching=1;
        CHECK(!trident_present(&d,0,0,8,1)); watching=0;
        CHECK(!failed && observed==expected_loops*2 && lfb[0]==0x04030201u && lfb[1]==0x08070605u);
    }
    struct tri_request q={.version=TRI_COMMAND_VERSION,.size=sizeof(q),
        .test=TRI_TEST_GAP_SWEEP,.arg=0,.end_ns=100,.step_ns=50};
    sweeping=1; lines=0; d.present_method=TRI_PRESENT_DWORD_SYNC;
    CHECK(trident_command_call(&q,sizeof(q))==TRI_E_ACCESS);
    CHECK(!failed && calls[0]==8 && calls[1]==8 && calls[2]==8 && lines==3);
    CHECK(d.write_gap_ns==50 && d.write_gap_loops==5 && d.present_method==TRI_PRESENT_DWORD_SYNC);
    CHECK(q.rc==TRI_E_ACCESS && q.result_count==4 && client[0]==1);
    q.rc=q.result_count=0; q.end_ns=0; read_failure=1; lines=0;
    CHECK(trident_command_call(&q,sizeof(q))==TRI_E_ACCESS);
    CHECK(!failed && client[0]==1 && d.write_gap_ns==50 &&
          d.present_method==TRI_PRESENT_DWORD_SYNC);
    q.rc=q.result_count=0; q.step_ns=0;
    CHECK(trident_command_call(&q,sizeof(q))==TRI_E_INVAL && !q.result_count);
    q.rc=0; q.step_ns=1; q.arg=101;
    CHECK(trident_command_call(&q,sizeof(q))==TRI_E_INVAL && !q.result_count);
    return 0;
}
void _start(void)
{
    int rc=test();
    if(rc) { char s[4]={'0'+rc/100%10,'0'+rc/10%10,'0'+rc%10,'\n'};
        __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(4):"memory"); }
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc?1:0):"memory");
    __builtin_unreachable();
}
