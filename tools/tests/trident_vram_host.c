/* libc-free VRAM acceptance; shared fake decodes VGA by MISC bit0. SPR expectations:
 * MIT trident_driver-1.4.0.c:2919-2975; memory cases: S3_DESIGN §§4,7.
 * Production regs/Init/Mode/Enable are linked, not reimplemented here.
 * Init gate oracle is reused from trident_pc98init_host.c (MIT notices there).
 */
#include "trident_device_fake.h"
#include "lib/serial_diag.h"
#include "vram_init_expected.h"
#define N(a) (sizeof(a) / sizeof((a)[0]))
static int equal(u32 a, u32 b)
{
    __asm__ volatile ("" : "+r"(a), "+r"(b) : : "memory");
    return a == b;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static struct trident_fake f;
static struct trident_device d;
static u8 storage[TRI_BAR0_DECODE_BYTES];
static u32 scratch[TRI_VRAM_SCRATCH_BYTES / sizeof(u32)];
static struct trident_fake_event trace[512];
static char logbuf[512];
static u32 loglen, lines, lower_writes, maximum_write;
static int observe_write(void *opaque, enum trident_space space, u32 off, u32 v)
{
    if (space == TRI_SPACE_LFB) {
        if (off == 4) ++lower_writes;
        if (off > maximum_write) maximum_write = off;
    }
    return trident_fake_io.write32(opaque, space, off, v);
}
static struct trident_io observed_io;
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s)
{
    while (*s) {
        if (*s == '\n') ++lines;
        if (loglen < sizeof(logbuf) - 1) logbuf[loglen++] = *s;
        ++s;
    }
    logbuf[loglen] = 0;
}
static bool contains(const char *s)
{
    for (u32 i = 0; i < loglen; ++i) {
        u32 j = 0;
        while (s[j] && i + j < loglen && logbuf[i+j] == s[j]) ++j;
        if (!s[j]) return true;
    }
    return false;
}
static u8 original(u32 off) { return (u8)((off * 13u) ^ (off >> 8) ^ 0xB7u); }
static int intact(void)
{
    for (u32 i = 0; i < f.real_bytes; ++i) CHECK(equal(storage[i], original(i)));
    return 0;
}
static int setup(bool initialize)
{
    d = (struct trident_device){0};
    CHECK(!trident_fake_reset(&f, storage, sizeof(storage), trace, N(trace)));
    trident_fake_bind(&f, &d);
    d.scratch = scratch; d.scratch_bytes = sizeof(scratch);
    d.saved.vga_base = TRI_CR_COLOR;
    d.cur_vga_base = TRI_CR_COLOR;
    f.regs[TRI_MISC_WRITE] = 1;
    d.pinned = d.armed = true;
    d.last_rc = -77;
    if (initialize) {
        /* Independent exact Init trace for reset's zero-register branch.
         * Only the oracle can open the common fake's write gate. */
        f.regs[TRI_CR_COLOR + 6] = 0x5A;
        CHECK(!trident_pc98_init(&d));
        CHECK(equal(f.trace_count, N(init_expected)));
        CHECK(!trident_fake_accept_init(&f, 0, init_expected, N(init_expected)));
        d.saved.vga_base = TRI_CR_MONO; /* consumers must use current base */
        d.init_done = true; /* lifecycle's documented commit point */
        CHECK(!trident_capacity_plan(&d));
        CHECK(!trident_mode_640(&d));
        CHECK(!trident_route_enable(&d));
    }
    for (u32 i = 0; i < sizeof(storage); ++i) storage[i] = original(i);
    f.operations = 0;
    observed_io = trident_fake_io;
    observed_io.write32 = observe_write;
    d.io = &observed_io;
    lower_writes = maximum_write = 0;
    trident_fake_trace_reset(&f);
    loglen = lines = 0;
    return 0;
}
static int spr_table(void)
{
    /* Literal KiB table independently transcribed from D:2927-2975. */
    static const u32 kib[16] = {
        1024,512,6144,1024,8192,1024,10240,2048,
        12288,1024,14336,1024,16384,1024,20480,4096
    };
    CHECK(!setup(false)); d.init_done = true;
    for (u32 mono = 0; mono < 2; ++mono) {
        d.cur_vga_base = mono ? TRI_CR_MONO : TRI_CR_COLOR;
        f.regs[TRI_MISC_WRITE] = !mono;
        u8 *cr = mono ? f.cr_mono : f.cr_color;
        for (u32 spr = 0; spr < 256; ++spr) {
            cr[0x1F] = spr; cr[0xC1] = 0;
            trident_fake_trace_reset(&f);
            CHECK(!trident_capacity_plan(&d));
            CHECK(equal(d.declared_bytes, kib[spr & 15] * 1024));
            CHECK(equal(d.spr, spr));
            CHECK(equal(d.declared_unknown, (spr & 15) == 0 || (spr & 15) == 5 ||
                         (spr & 15) == 9 || (spr & 15) == 11 || (spr & 15) == 13));
            CHECK(equal(f.trace_count, (spr & 15) == 14 ? 4 : 2));
            for (u32 j = 0; j < f.trace_count; ++j) {
                CHECK(equal(trace[j].width, 1));
                CHECK(equal(trace[j].space, TRI_SPACE_REGS));
                CHECK(equal(trace[j].offset, d.cur_vga_base + (j & 1)));
            }
        }
        cr[0x1F] = 0xFE;
        for (u32 ext = 0; ext < 256; ++ext) {
            cr[0xC1] = ext;
            u32 want = (ext & 0x10) ? ((ext & 1) ? 32768 : 28672) :
                                                   ((ext & 1) ? 24576 : 20480);
            CHECK(!trident_capacity_plan(&d));
            CHECK(equal(d.declared_bytes, want * 1024));
        }
        for (u32 op = 1; op <= 4; ++op) {
            f.operations = 0; trident_fake_fail(&f, op, -123);
            CHECK(equal(trident_capacity_plan(&d), (u32)-123));
            CHECK(equal(f.operations, op));
        }
        trident_fake_fail(&f, 0, 0);
    }
    return 0;
}
static int memory_cases(void)
{
    static const struct {
        u32 bytes, hole, mapped;
        enum trident_upper memory_upper, want_upper;
        u32 verified;
        int rc;
    } cases[] = {
        {2097152,U32_MAX,4194304,TRI_UPPER_INDEPENDENT,TRI_UPPER_INDEPENDENT,2097152,0},
        {2097152,U32_MAX,4194304,TRI_UPPER_MIRROR,TRI_UPPER_MIRROR,2097152,0},
        {4194304,U32_MAX,4194304,TRI_UPPER_INDEPENDENT,TRI_UPPER_INDEPENDENT,4194304,0},
        {2097152,2097152,4194304,TRI_UPPER_INDEPENDENT,TRI_UPPER_UNKNOWN,2097152,0},
        {2097152,307200,4194304,TRI_UPPER_INDEPENDENT,TRI_UPPER_UNKNOWN,307200,0},
        {2097152,307196,4194304,TRI_UPPER_INDEPENDENT,TRI_UPPER_UNKNOWN,307196,TRI_E_ACCESS},
        {262144,U32_MAX,4194304,TRI_UPPER_MIRROR,TRI_UPPER_MIRROR,262144,TRI_E_CAP_TOO_SMALL},
        {1048576,U32_MAX,4194304,TRI_UPPER_MIRROR,TRI_UPPER_MIRROR,1048576,0},
        {2097152,U32_MAX,307200,TRI_UPPER_INDEPENDENT,TRI_UPPER_UNKNOWN,307200,0},
    };
    for (u32 i = 0; i < N(cases); ++i) {
        CHECK(!setup(true));
        CHECK(!trident_fake_memory(&f, cases[i].bytes, cases[i].memory_upper));
        f.hole_start = cases[i].hole;
        f.resources.mapped_bytes = cases[i].mapped;
        CHECK(equal(trident_vram_verify(&d), cases[i].rc));
        CHECK(equal(d.verified_bytes, cases[i].verified));
        CHECK(equal(d.upper, cases[i].want_upper));
        CHECK(equal(d.declared_bytes, 4194304));
        CHECK(equal(d.last_rc, (u32)-77) && !d.active && !d.first_init_done);
        CHECK(equal(lines, 1) && contains("stage=vram") && contains("declared=4194304"));
        CHECK(!intact());
        if (cases[i].memory_upper == TRI_UPPER_MIRROR)
            CHECK(equal(lower_writes, 6)); /* full lower-range recheck */
        CHECK(maximum_write < cases[i].mapped);
    }
    /* A final tag undo failure after >=300KiB is recorded, not fatal. */
    CHECK(!setup(true));
    f.resources.mapped_bytes = TRI_DISPLAY_BYTES;
    CHECK(!trident_vram_verify(&d));
    u32 final_read = f.operations;
    CHECK(!setup(true));
    f.resources.mapped_bytes = TRI_DISPLAY_BYTES;
    trident_fake_fail(&f, final_read, -123);
    CHECK(!trident_vram_verify(&d));
    CHECK(equal(d.verified_bytes, TRI_DISPLAY_BYTES));
    CHECK(contains("rc=-7") && contains("verified=307200") && !intact());
    /* Decode and mapping independently constrain the traversal. */
    CHECK(!setup(true)); f.resources.decode_bytes = TRI_DISPLAY_BYTES;
    CHECK(!trident_vram_verify(&d));
    CHECK(equal(d.verified_bytes, TRI_DISPLAY_BYTES));
    CHECK(maximum_write < TRI_DISPLAY_BYTES && !intact());
    /* A small SPR declaration never caps the measured decode window. */
    CHECK(!setup(true));
    f.cr_color[0x1F] = 1;
    CHECK(!trident_capacity_plan(&d));
    CHECK(equal(d.declared_bytes, 524288));
    CHECK(!trident_vram_verify(&d));
    CHECK(equal(d.verified_bytes, 2097152) && !intact());
    /* Blocked BAR0 despite lifecycle flags: writes must not prove capacity. */
    CHECK(!setup(false)); d.init_done = true;
    CHECK(equal(trident_vram_verify(&d), (u32)TRI_E_CAP_TOO_SMALL));
    CHECK(!d.verified_bytes && !intact());
    return 0;
}
static int small_trace(void)
{
    CHECK(!setup(true));
    f.resources.mapped_bytes = 16; f.trace_lfb = true;
    CHECK(equal(trident_vram_verify(&d), (u32)TRI_E_CAP_TOO_SMALL));
    CHECK(equal(d.verified_bytes, 16));
    CHECK(equal(f.trace_count, 28)); /* save4 + (write4/read4)*2 + undo4*2 */
    for (u32 i = 0; i < 4; ++i) {
        CHECK(equal(trace[i].offset, i*4) && !trace[i].write);
        CHECK(equal(trace[4+i].value, (i*4) ^ 0x69C35AA5u));
        CHECK(equal(trace[12+i].value, ~((i*4) ^ 0x69C35AA5u)));
        CHECK(equal(trace[20+i*2].offset, (3-i)*4) && trace[20+i*2].write);
    }
    for (u32 i = 0; i < f.trace_count; ++i)
        CHECK(equal(trace[i].space, TRI_SPACE_LFB) && equal(trace[i].width, 4));
    CHECK(!intact());
    /* Every save, pattern write/read and reverse restoration failure. */
    for (u32 op = 1; op <= 28; ++op) {
        CHECK(!setup(true)); f.resources.mapped_bytes = 16;
        trident_fake_fail(&f, op, -123);
        int rc = trident_vram_verify(&d);
        CHECK(equal(rc, op >= 21 ? (u32)TRI_E_RESTORE : (u32)-123));
        /* A failed undo write leaves that word altered; other writes undo. */
        if (op < 21 || !(op & 1)) CHECK(!intact());
    }
    return 0;
}
static int invalid_inputs(void)
{
    CHECK(equal(trident_capacity_plan(0), (u32)TRI_E_INVAL));
    CHECK(equal(trident_vram_verify(0), (u32)TRI_E_INVAL));
    CHECK(!setup(false));
    CHECK(equal(trident_capacity_plan(&d), (u32)TRI_E_STATE));
    CHECK(equal(trident_vram_verify(&d), (u32)TRI_E_STATE));
    CHECK(!f.operations);
    d.init_done = true;
    d.scratch_bytes = TRI_VRAM_SCRATCH_BYTES - 1;
    CHECK(equal(trident_vram_verify(&d), (u32)TRI_E_INVAL));
    CHECK(!f.operations);
    return 0;
}
static int production_binding(void)
{
    CHECK(!setup(false));
    f.resources.mapped_bytes = f.resources.decode_bytes = TRI_DISPLAY_BYTES;
    /* Ordinary mapped RAM exercises actual volatile MMIO callbacks through
     * production regs_bind. No port callback is invoked by capacity/verify. */
    CHECK(!trident_regs_bind(&d));
    d.init_done = true;
    f.regs[TRI_CR_COLOR + 1] = 0xFF;
    CHECK(!trident_capacity_plan(&d));
    CHECK(!trident_vram_verify(&d));
    CHECK(equal(d.verified_bytes, TRI_DISPLAY_BYTES) && !intact());
    CHECK(equal(f.operations, 0)); /* no fake I/O callbacks on this path */
    return 0;
}
static void emit(const char *s, u32 n)
{
    u32 result = 4;
    __asm__ volatile ("int $0x80" : "+a"(result) : "b"(1), "c"(s), "d"(n) : "memory");
}
void _start(void)
{
    int rc = invalid_inputs();
    if (!rc) rc = spr_table();
    if (!rc) rc = memory_cases();
    if (!rc) rc = small_trace();
    if (!rc) rc = production_binding();
    if (rc) {
        emit("RED vram line ", 14);
        char buf[12]; u32 n = 0, v = rc;
        do { buf[n++] = '0' + v % 10; v /= 10; } while (v);
        while (n) emit(&buf[--n], 1);
        emit("\n", 1);
    } else emit("GREEN vram\n", 11);
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
