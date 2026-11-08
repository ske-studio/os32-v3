/* CPU drawing must remain observable when optional GE setup is unavailable. */
#include "kdebug/trident_command_test.c"
static u32 memory[TRI_WIDTH * 2];
static int read_reports, ge_reports, phases, fills, copies, syncs, failed;
static int fail_fill, fail_sync;
static int wrapper, group, groups_done, io_calls, io_fail, cr_reports;
static int fail_group, group_fault, restores, saves;
static u8 pci_value, pci_saved;
static bool expect_hex;
static bool expected_ge=true;
static bool benchmark;
static u32 bench_calls, bench_reads, bench_clock, bench_lines, bench_method;
static int bench_fault, bench_field;
static u32 bench_fields;
int sys_time_now(u32 *lo, u32 *hi)
{
    *lo = (u32)-51 + (bench_clock / 2u) * 100u +
          (bench_clock % 2u ? 10u + (bench_clock / 2u) % 8u : 0);
    ++bench_clock; *hi = 0;
    return bench_fault == 2 && bench_clock == 2 ? TRI_E_ACCESS : 0;
}
static int bench_present(struct trident_device *d, int x, int y, int w, int h)
{
    if (x || y || w != 3 || h != 2 || d->present_method !=
        (enum tri_present_method)bench_lines) failed=1;
    ++bench_calls;
    return bench_fault == 1 ? TRI_E_ACCESS : 0;
}
static int bench_read(void *opaque, enum trident_space space, u32 off, u8 *value)
{
    (void)opaque;
    if (space != TRI_SPACE_LFB || off != 16 + (bench_reads % 6 / 3)*7 + bench_reads%3)
        failed=1;
    if ((!bench_fault && bench_calls != (bench_reads / 6u + 1u) * 8u)) failed=1;
    *value = (u8)(bench_reads%6 + 1);
    if (bench_fault == 3 || (bench_fault == 5 && bench_lines == 2)) *value ^= 1;
    ++bench_reads;
    return bench_fault == 4 ? TRI_E_ACCESS : 0;
}
static void check_group(void)
{
    if (read_reports!=1 || (expected_ge ?
        (fills!=4 || copies!=2 || syncs!=2 || phases!=31) :
        (fills || copies || syncs || phases))) failed=1;
    ++groups_done;
}
static int pci_write(void *opaque, enum trident_space space, u32 off, u8 value)
{
    (void)opaque;
    ++io_calls;
    if (space!=TRI_SPACE_REGS) failed=1;
    if (off==TRI_CR_MONO) {
        if (value!=0x39) failed=1;
    } else if (off==TRI_CR_MONO+1) {
        if (group==1 && value!=(pci_saved & 0xf9)) failed=1;
        if (group==2 && value!=pci_saved) failed=1;
        pci_value=value; /* Also model a failed write that reached hardware. */
    } else failed=1;
    return io_calls==io_fail ? TRI_E_ACCESS : 0;
}
static int pci_read(void *opaque, enum trident_space space, u32 off, u8 *value)
{
    (void)opaque;
    ++io_calls; ++saves;
    if (space!=TRI_SPACE_REGS || off!=TRI_CR_MONO+1 || io_calls!=2)
        failed=1;
    if (io_calls==io_fail) return TRI_E_ACCESS;
    *value=pci_value;
    return 0;
}
static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return !*a && !*b;
}
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s)
{
    if (benchmark) {
        if (!bench_fault) {
            if (bench_field) {
                const char *expected[] = {"", "8", "10", "17", "13", "0"};
                if (!same(s, expected[bench_field])) failed=1;
                ++bench_fields; bench_field=0;
            } else if (same(s, " count=")) bench_field=1;
            else if (same(s, " min_us=")) bench_field=2;
            else if (same(s, " max_us=")) bench_field=3;
            else if (same(s, " avg_us=")) bench_field=4;
            else if (same(s, " mismatches=")) bench_field=5;
        }
        if (same(s, "TRI phase=present")) ++bench_lines;
        if (same(s, "byte")) bench_method |= 1;
        if (same(s, "dword_sync")) bench_method |= 2;
        if (same(s, "dword_wait")) bench_method |= 4;
        if (same(s, "dword_gap")) bench_method |= 8;
        return;
    }
    if (wrapper) {
        if (expect_hex) {
            const char *hex="0123456789abcdef";
            if (!io_fail && (s[0]!=hex[pci_value>>4] || s[1]!=hex[pci_value&15] || s[2]))
                failed=1;
            expect_hex=false;
        }
        if (same(s, " cr39=")) { expect_hex=true; ++cr_reports; }
        if (same(s, "cpu32_pci_original") || same(s, "cpu32_pci_no_burst")) {
            if (group && !group_fault && !io_fail) check_group();
            ++group;
            if ((group==1 && (io_calls!=2 || pci_value!=pci_saved)) ||
                (group==2 && !io_fail &&
                 (io_calls!=4 || pci_value!=(pci_saved&0xf9)))) failed=1;
            fills=copies=syncs=phases=read_reports=0;
            fail_fill=group==fail_group && group_fault==1;
            fail_sync=group==fail_group && group_fault==2;
        }
        if (same(s, "cpu32_pci_restore")) {
            if (!group_fault && !io_fail) check_group();
            ++restores;
            if (!io_fail && pci_value!=pci_saved) failed=1;
        }
    }
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
            if (cpu32_cases(&o, 7)!=TRI_E_NOSYS || read_reports!=1 || ge_reports!=1) return 1;
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
        if (cpu32_cases(&o, 7) || failed || fills!=4 || copies!=2 || syncs!=2 ||
            phases!=31 || read_reports!=1 || ge_reports) return 1;
        /* Verify volatile readback detects stale VRAM, independently of writes. */
        memory[0]^=1;
        if (read_row(&o, "stale", 0, TRI_WIDTH*cpp/4)!=TRI_E_ACCESS) return 1;
    }
    fills=copies=syncs=phases=read_reports=failed=0;
    fail_fill=1;
    if (cpu32_cases(&o, 7)!=TRI_E_ACCESS || copies || syncs || phases) return 1;
    fail_fill=0; fail_sync=1;
    fills=copies=syncs=phases=read_reports=failed=0;
    if (cpu32_cases(&o, 7)!=TRI_E_ACCESS || fills!=2 || copies || syncs!=1 || phases!=1)
        return 1;
    fail_fill=fail_sync=0;
    const struct trident_io io={.read8=pci_read, .write8=pci_write};
    d.io=&io; d.cur_vga_base=TRI_CR_MONO;
    wrapper=1;
    /* All starting bytes prove that only bits1/2 change, including bit0=0. */
    for (u32 cpp=1; cpp<=4; cpp*=2) {
        d.mode.client=(struct tri_surface){0, TRI_WIDTH*cpp*2, TRI_WIDTH*cpp,
                                           TRI_WIDTH, 2, cpp, TRI_FMT_INDEX8};
        d.mode.display=d.mode.client;
        for (u32 value=0; value<256; ++value) {
            pci_value=pci_saved=value;
            group=groups_done=io_calls=cr_reports=restores=saves=failed=0;
            fills=copies=syncs=phases=read_reports=0;
            if (cpu32(&o) || failed || io_calls!=6 || saves!=1 || restores!=1 ||
                pci_value!=value || groups_done!=2 || cr_reports!=11) return 1;
        }
    }
    /* Every register access can fail; a saved byte must still be restored.
     * Fail disable's data write after applying it to exercise ambiguous I/O. */
    for (int fault=1; fault<=6; ++fault) {
        pci_value=pci_saved=0xa7;
        group=groups_done=io_calls=cr_reports=restores=saves=failed=0;
        fills=copies=syncs=phases=read_reports=0;
        io_fail=fault;
        if (cpu32(&o)!=TRI_E_ACCESS || failed) return 1;
        if (fault<=2) {
            if (restores || group || io_calls!=fault) return 1;
        } else if (restores!=1 || (fault<5 && pci_value!=pci_saved)) return 1;
    }
    io_fail=0;
    /* GE failures in either group take the same unconditional restore exit. */
    for (fail_group=1; fail_group<=2; ++fail_group) {
        for (group_fault=1; group_fault<=2; ++group_fault) {
            pci_value=pci_saved=0x07;
            group=groups_done=io_calls=cr_reports=restores=saves=failed=0;
            fills=copies=syncs=phases=read_reports=0;
            if (cpu32(&o)!=TRI_E_ACCESS || failed || restores!=1 ||
                pci_value!=pci_saved) return 1;
        }
    }
    /* Optional GE absence must not hide a register/restore failure. */
    fail_group=group_fault=fail_fill=fail_sync=0;
    o.fill=0; expected_ge=false;
    for (int fault=0; fault<=6; ++fault) {
        pci_value=pci_saved=0xa7;
        group=groups_done=io_calls=cr_reports=restores=saves=failed=0;
        fills=copies=syncs=phases=read_reports=ge_reports=0;
        io_fail=fault;
        if (cpu32(&o)!=(fault ? TRI_E_ACCESS : TRI_E_NOSYS) || failed) return 1;
        if (!fault && (restores!=1 || groups_done!=2 || cr_reports!=5 ||
                       ge_reports!=2 || pci_value!=pci_saved)) return 1;
        if (fault>2 && (restores!=1 || (fault<5 && pci_value!=pci_saved))) return 1;
    }
    benchmark=true;
    u8 image[10]={1,2,3,0,0,6,0,0,0,0};
    image[5]=4; image[6]=5; image[7]=6;
    r.client=image; r.client_bytes=sizeof(image); r.ram_fallback=true;
    d.mode.client=(struct tri_surface){.width=3,.height=2,.cpp=1,.pitch=5,.bytes=10};
    d.mode.display=(struct tri_surface){.off=16,.pitch=7};
    const struct trident_io bench_io={.read8=bench_read};
    d.io=&bench_io; o.present=bench_present;
    for (bench_fault=0; bench_fault<=5; ++bench_fault) {
        bench_calls=bench_reads=bench_clock=bench_lines=bench_method=bench_fields=0;
        bench_field=0;
        failed=0;
        d.present_method=TRI_PRESENT_DWORD_SYNC;
        int rc=present_compare(&o);
        if (rc != (bench_fault ? TRI_E_ACCESS : 0) || bench_lines!=4 ||
            bench_method!=15 || d.present_method!=TRI_PRESENT_DWORD_SYNC) return 1;
        if (bench_fault == 5 && (failed || bench_calls!=32 || bench_reads!=24 || bench_clock!=64)) return 1;
        if (!bench_fault && (failed || bench_calls!=32 || bench_reads!=24 || bench_clock!=64 || bench_fields!=20))
            return 1;
    }
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory");
    __builtin_unreachable();
}
