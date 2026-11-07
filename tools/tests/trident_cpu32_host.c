/* CPU drawing must remain observable when optional GE setup is unavailable. */
#include "kdebug/trident_command_test.c"
static u32 memory[TRI_WIDTH * 2];
static int read_reports, ge_reports, phases, fills, copies, syncs, failed;
static int fail_fill, fail_sync;
static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return !*a && !*b;
}
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s)
{
    if (same(s, "cpu32_no_ge_read")) {
        ++read_reports;
        if (fills || copies || syncs) failed=1;
    }
    if (same(s, "ge")) ++ge_reports;
    if (same(s, "cpu32_fill_read")) phases |= 1;
    if (same(s, "cpu32_double_sync_read")) phases |= 2;
    if (same(s, "cpu32_scalar_read")) phases |= 4;
    if (same(s, "cpu32_read")) phases |= 8;
    if (same(s, "ge_copy_read")) phases |= 16;

}
static int fill(struct trident_device *d, const struct tri_surface *s,
                int x, int y, int w, int h, u32 color, u8 rop)
{
    (void)d;
    if (x || y || w!=TRI_WIDTH || h!=2 || color!=0x35 || rop!=3) failed=1;
    ++fills;
    if (fail_fill) return TRI_E_ACCESS;
    u32 count=TRI_WIDTH*s->cpp/4;
    /* Before each fresh GE fill the preceding CPU case must have survived. */
    u32 salt=fills*0x01020408u;
    for (u32 i=0;i<count;++i)
        if (memory[i]!=(0x5aa59669u ^ (i*0x01020408u) ^ salt)) failed=1;
    u32 pixel=s->cpp==1 ? 0x35353535u : s->cpp==2 ? 0x00350035u : 0x35u;
    for (u32 i=0;i<count*2;++i) memory[i]=pixel;
    return 0;
}
static int sync_ge(struct trident_device *d)
{
    (void)d;
    ++syncs;
    if ((syncs==1 && fills!=2) || (syncs==2 && copies!=1)) failed=1;
    return fail_sync ? TRI_E_ACCESS : 0;
}
static int direction(struct trident_device *d, int x, int y)
{
    (void)d;
    if (x!=1 || y!=1) failed=1;
    return 0;
}
static int copy(struct trident_device *d, const struct tri_surface *src,
                const struct tri_surface *dst, int sx, int sy, int dx, int dy,
                int w, int h, u8 rop, int transparent, u32 key)
{
    (void)d;
    if (src!=dst || sx || sy || dx || dy!=1 || w!=TRI_WIDTH || h!=1 || rop!=3)
        failed=1;
    ++copies;
    if (transparent!=(copies==1) || key!=(copies==1 ? 0x35u : 0u)) failed=1;
    u32 count=TRI_WIDTH*src->cpp/4;
    if (!transparent)
        for (u32 i=0;i<count;++i) memory[count+i]=memory[i];
    return 0;
}
static int test(void)
{
    struct trident_resources r = {.ready=true, .client=(u8 *)memory,
        .client_bytes=sizeof(memory), .generation=1};
    struct trident_device d = {.res=&r, .active=true, .screen_ready=true};
    struct TridentDiagnosticOps o = {.context=&d};
    d.mode.valid=true; d.mode.generation=1;
    for (u32 cpp=1; cpp<=4; cpp*=2) {
        d.mode.client=(struct tri_surface){0, TRI_WIDTH*cpp*2, TRI_WIDTH*cpp,
                                           TRI_WIDTH, 2, cpp, TRI_FMT_INDEX8};
        d.mode.display=d.mode.client;
        for (u32 scenario=0; scenario<2; ++scenario) {
            d.mode.no_accel=scenario==0; d.accel.initialized=false;
            for (u32 i=0;i<TRI_WIDTH*2;++i) memory[i]=0;
            read_reports=ge_reports=0;
            if (cpu32(&o)!=TRI_E_NOSYS || read_reports!=1 || ge_reports!=1) return 1;
            for (u32 i=0;i<TRI_WIDTH*cpp/4;++i)
                if (memory[i]!=(0x5aa59669u ^ (i*0x01020408u) ^ 0x01020408u)) return 1;
            if (memory[TRI_WIDTH*cpp/4]) return 1;
        }
    }
    o.fill=fill; o.copy=copy; o.copy_direction=direction; o.sync=sync_ge;
    d.mode.no_accel=false; d.accel.initialized=true;
    d.mode.features=TRI_CAP_FILL|TRI_CAP_COPY;
    for (u32 cpp=1;cpp<=4;cpp*=2) {
        d.mode.client=(struct tri_surface){0, TRI_WIDTH*cpp*2, TRI_WIDTH*cpp,
                                           TRI_WIDTH, 2, cpp, TRI_FMT_INDEX8};
        d.mode.display=d.mode.client;
        fills=copies=syncs=phases=read_reports=ge_reports=failed=0;
        if (cpu32(&o) || failed || fills!=4 || copies!=2 || syncs!=2 ||
            phases!=31 || read_reports!=1 || ge_reports) return 1;
        /* Verify volatile readback detects stale VRAM, independently of writes. */
        memory[0]^=1;
        if (read_row(&o, "stale", 0, TRI_WIDTH*cpp/4)!=TRI_E_ACCESS) return 1;
    }
    fills=copies=syncs=phases=read_reports=failed=0;
    fail_fill=1;
    if (cpu32(&o)!=TRI_E_ACCESS || copies || syncs || phases) return 1;
    fail_fill=0; fail_sync=1;
    fills=copies=syncs=phases=read_reports=failed=0;
    if (cpu32(&o)!=TRI_E_ACCESS || fills!=2 || copies || syncs!=1 || phases!=1)
        return 1;
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory");
    __builtin_unreachable();
}
