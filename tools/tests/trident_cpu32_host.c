/* CPU drawing must remain observable when optional GE setup is unavailable. */
#include "kdebug/trident_command_test.c"
static u32 memory[TRI_WIDTH * 2];
static int read_reports, ge_reports;
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s)
{
    const char *a = s, *b = "cpu32_read";
    while (*a && *a == *b) { ++a; ++b; }
    if (!*a && !*b) ++read_reports;
    a = s; b = "ge";
    while (*a && *a == *b) { ++a; ++b; }
    if (!*a && !*b) ++ge_reports;
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
                if (memory[i]!=(0x5aa59669u ^ (i*0x01020408u))) return 1;
            if (memory[TRI_WIDTH*cpp/4]) return 1;
        }
    }
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory");
    __builtin_unreachable();
}
