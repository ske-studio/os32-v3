/* HW_BASIC_DESIGN §§2,3,7: real parser/logger/executor, only backend is fake. */
#include "hwb_fake.h"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_log.c"
#include "userland/shell/hwb_exec.c"

static struct hwb_fake f;
static struct hwb_state s;
static struct hwb_program programs[2];
static char sources[2][HWB_FILE_BYTES];
static u32 which;
/* Independent fixture addresses: intentionally absent from any product table. */
#define TEST_PHYS 0x34567000UL
#define TEST_PORT 0x715U
#define TEST_BDF HW_BDF(7,13,2)
#define TRIDENT_BASE 0x20400000UL
#define TRIDENT_FAC 0xFACU
static u32 length(const char *p) { u32 n = 0; while (p[n]) n++; return n; }
static void setup(void) { hwbf_init(&f); hwb_init(&s, &f.backend); which = 0; }
static enum hwb_rc load(const char *text, const struct hwb_initial *args, u32 count)
{
    f.file_count = 0; hwbf_add_file(&f, "script", text, length(text));
    which = s.program == &programs[0] ? 1U : 0U;
    return hwb_load(&s, &programs[which], sources[which], sizeof(sources[which]), "script", args, count);
}
static enum hwb_rc run(const char *text)
{
    enum hwb_rc rc = load(text, 0, 0); return rc ? rc : hwb_continue(&s);
}
static u32 scalar(const char *name)
{
    for (u32 n = 0; n < s.var_count; n++) if (hwbf_equal(name, s.vars[n].name)) return s.vars[n].value;
    CHECK(0); return 0;
}
static u32 count_access(u32 space, u32 action)
{
    u32 count = 0;
    for (u32 n = 0; n < f.event_count; n++) if (f.events[n].kind == HWBF_ACCESS &&
        f.events[n].req.space == space && f.events[n].req.action == action) count++;
    return count;
}
static int contains(const char *p, const char *q)
{
    for (; *p; p++) { u32 n = 0; while (q[n] && p[n] == q[n]) n++; if (!q[n]) return 1; }
    return 0;
}
static void fresh_addresses(void)
{
    HWB_TEST("fresh-addresses"); setup();
    const char *a = "device d = bdf 7:13.2\npciwrite8 d 0x41 0xA5\niowrite8 port 0x715 0x6B\nmmiowrite32 phys 0x34567000 0x12345678\nend";
    const char *b = "device d = bdf 11:29.6\npciwrite16 d 0x42 0x789A\niowrite16 port 0x927 0xBCDE\nmmiowrite8 phys 0x45678003 0xEF";
    CHECK(run(a) == HWB_OK && s.writes == 3);
    CHECK(*hwbf_byte(&f, HW_PHYS, TEST_PHYS) == 0x78);
    CHECK(*hwbf_byte(&f, HW_IO, TEST_PORT) == 0x6B);
    CHECK(f.events[0].kind == HWBF_FILE);
    int found = 0;
    for (u32 n = 0; n < f.event_count; n++) if (f.events[n].kind == HWBF_ACCESS &&
        f.events[n].req.bdf == TEST_BDF && f.events[n].req.value == 0xA5) found = 1;
    CHECK(found);
    hwbf_clear_trace(&f);
    CHECK(run(b) == HWB_OK && s.writes == 3);
    CHECK(*hwbf_byte(&f, HW_IO, 0x927) == 0xDE && *hwbf_byte(&f, HW_IO, 0x928) == 0xBC);
    CHECK(*hwbf_byte(&f, HW_PHYS, 0x45678003) == 0xEF);
    CHECK(contains(f.lines[0], "seq=1 "));
    CHECK(contains(f.lines[f.line_count-1], "writes=3"));
}
static void widths(void)
{
    HWB_TEST("widths-rmw"); setup();
    struct hwbf_pci *p = hwbf_add_pci(&f, TEST_BDF, 0x1111, 0x2222);
    p->config[4] = 0xA0; p->config[5] = 0xA1; p->config[6] = 0xFF; p->config[7] = 0xFF;
    CHECK(run("device d = bdf 7:13.2\npciread8 d 4 -> t\nlet t = t | 3\npciwrite8 d 4 t\npciwrite16 d 4 0x1234\npciread16 d 4 -> x") == HWB_OK);
    CHECK(p->config[4] == 0x34 && p->config[5] == 0x12 && p->config[6] == 0xFF && p->config[7] == 0xFF);
    CHECK(scalar("x") == 0x1234 && count_access(HW_PCI, HW_READ) == 2);
    u32 actions[4] = {HW_READ,HW_WRITE,HW_WRITE,HW_READ}, widths[4] = {1,1,2,2}, k = 0;
    for (u32 n = 0; n < f.event_count; n++) if (f.events[n].kind == HWBF_ACCESS) {
        CHECK(k < 4 && f.events[n].req.action == actions[k] && f.events[n].req.width == widths[k]);
        k++;
        CHECK(n && f.events[n-1].kind == HWBF_LINE && contains(f.lines[f.events[n-1].a], "phase=begin"));
    }
    CHECK(k == 4);
}
static void write_only(void)
{
    HWB_TEST("write-only"); setup();
    hwbf_add_register(&f, HW_PHYS, 0, TEST_PHYS, 4, 1);
    CHECK(run("mmiowrite32 phys 0x34567000 0xAABBCCDD") == HWB_OK);
    CHECK(count_access(HW_PHYS, HW_READ) == 0 && s.writes == 1);
}
static void bad_args(void)
{
    HWB_TEST("boundary"); setup();
    CHECK(run("iowrite32 port 65534 0\niowrite8 port 1 1") == HWB_ARG);
    CHECK(f.accesses == 0 && s.writes == 0 && s.stopped);
    u32 pc = s.pc, points = f.points;
    CHECK(hwb_step(&s) == HWB_ARG && hwb_continue(&s) == HWB_ARG && f.points == points && s.pc == pc);
    const char *bad[] = {"mmiowrite32 phys 0xFFFFFFFE 0", "mmioread16 phys 1 -> x",
        "device d = bdf 0:0.0\npciread32 d 254 -> x", "iowrite8 port 4 256",
        "wait us 0", "wait us 10000001", "let x = 0xFFFFFFFF + 1", "let x = missing",
        "poll 8 port 1 0xFF 1 5 0 -> x", "poll 8 port 1 1 2 5 1 -> x",
        "poll 8 port 1 256 0 5 1 -> x", "return", "call loop\nend\nlabel loop\ncall loop\nreturn"};
    for (u32 n = 0; n < sizeof(bad)/sizeof(bad[0]); n++) {
        hwbf_clear_trace(&f); CHECK(run(bad[n]) == HWB_ARG); CHECK(s.writes == 0);
    }
    CHECK(s.call_depth == HWB_CALL_DEPTH);
}
static void failures(void)
{
    HWB_TEST("failures"); setup(); f.access_fail_at = 2;
    CHECK(run("iowrite8 port 1 1\niowrite8 port 2 2\niowrite8 port 3 3") == HWB_IO);
    CHECK(f.accesses == 2 && s.writes == 1 && *hwbf_byte(&f, HW_IO, 3) == 0);
    setup(); f.status_fail_at = 1; f.failure_status = HW_WINDOW;
    CHECK(run("mmioread8 phys 0 -> x\niowrite8 port 3 3") == HWB_WINDOW && s.writes == 0);
    setup(); f.mechanism = 0;
    CHECK(run("device d = bdf 0:0.0\npciread32 d 0 -> x\niowrite8 port 3 3") == HWB_NOSYS);
    CHECK(hwb_exit_status(s.rc) == 3 && s.writes == 0);
    setup(); f.clock_fail_at = 1;
    CHECK(run("wait us 1\niowrite8 port 3 3") == HWB_IO && s.writes == 0);
    setup(); f.now_lo = 100; f.tick_us = 1; f.reverse_clock = 1;
    CHECK(run("wait us 2\niowrite8 port 3 3") == HWB_IO && s.writes == 0);
}
/* Million-iteration tests keep counters, discarding only old observation events. */
static void bounded_yield(void *ctx) { hwbf_yield(ctx); ((struct hwb_fake *)ctx)->event_count = 0; }
static void clocks(void)
{
    HWB_TEST("clock-wrap"); setup(); f.now_lo = U32_MAX - 2U; f.tick_us = 2;
    CHECK(run("wait us 6\niowrite8 port 3 3") == HWB_OK && s.writes == 1);
    CHECK(f.now_hi == 1 && f.yields == 2);
}
static int jumping_time(void *ctx, u32 *lo, u32 *hi)
{
    struct hwb_fake *fake = ctx; *lo = 0; *hi = fake->times++; return 0;
}
static void deadlines(void)
{
    HWB_TEST("clock-high-difference"); setup(); f.backend.time_now = jumping_time; f.backend.yield = bounded_yield;
    CHECK(run("wait us 10") == HWB_OK && f.times == 2 && f.yields == 0);
    HWB_TEST("frozen-clock"); setup(); f.backend.yield = bounded_yield;
    CHECK(run("wait us 1\niowrite8 port 3 3") == HWB_TIMEOUT && s.writes == 0);
    CHECK(f.yields == HWB_ITERATIONS);
    HWB_TEST("poll-timeout"); setup(); f.tick_us = 2;
    CHECK(run("poll 8 port 1 255 1 10 2 -> x\niowrite8 port 3 3") == HWB_TIMEOUT);
    CHECK(s.writes == 0 && count_access(HW_IO, HW_READ) == 4 && scalar("x") == 0);
    setup(); *hwbf_byte(&f, HW_IO, 1) = 1; f.tick_us = 1;
    CHECK(run("poll 8 port 1 255 1 10 2 -> x") == HWB_OK);
    CHECK(count_access(HW_IO, HW_READ) == 1 && scalar("x") == 1);
    setup(); f.backend.yield = bounded_yield;
    CHECK(run("poll 8 port 1 255 1 10 2 -> x") == HWB_TIMEOUT);
    CHECK(f.accesses == 1 && f.yields == HWB_ITERATIONS);
}
static void aborts(void)
{
    HWB_TEST("instruction-point"); setup(); f.point_at = 2;
    CHECK(run("iowrite8 port 1 1\niowrite8 port 2 2") == HWB_ABORT && s.writes == 0);
    CHECK(hwb_exit_status(s.rc) == 130);
    f.point_at = 0; f.pending_abort = 1; hwbf_clear_trace(&f);
    CHECK(run("iowrite8 port 1 1") == HWB_OK && s.writes == 1);
    setup(); f.point_at = 5;
    CHECK(run("label loop\ngoto loop\niowrite8 port 1 1") == HWB_ABORT && s.writes == 0);
    setup(); f.point_at = 4;
    CHECK(run("wait us 10\niowrite8 port 1 1") == HWB_ABORT && s.writes == 0);
    setup(); f.point_at = 4;
    CHECK(run("poll 8 port 1 255 1 10 2 -> x\niowrite8 port 1 1") == HWB_ABORT && f.accesses == 0);
    setup(); CHECK(load("let x = 4\niowrite8 port 1 x", 0, 0) == HWB_OK);
    CHECK(hwb_step(&s) == HWB_OK && scalar("x") == 4 && !s.stopped);
    hwb_abort(&s); u32 lines = f.line_count; hwb_abort(&s);
    CHECK(hwb_continue(&s) == HWB_ABORT && s.writes == 0 && lines == f.line_count);
}
static void sessions(void)
{
    HWB_TEST("sessions"); setup();
    CHECK(load("let x = mode\niowrite8 port 1 x", 0, 0) == HWB_OK);
    CHECK(scalar("mode") == 0 && s.seq == 0 && !s.writes);
    CHECK(hwb_step(&s) == HWB_OK && scalar("x") == 0);
    const struct hwb_program *old = s.program; u32 pc = s.pc;
    CHECK(load("bad", 0, 0) == HWB_ARG && s.program == old && s.pc == pc && scalar("x") == 0);
    CHECK(hwb_command(&s, "let x = 7", 9) == HWB_OK && s.pc == pc && s.program == old);
    CHECK(hwb_continue(&s) == HWB_OK && *hwbf_byte(&f, HW_IO, 1) == 7);
    hwb_reset(&s); CHECK(!s.var_count && s.program == old);
    struct hwb_initial args[] = {{"mode",2},{"custom",3}};
    CHECK(load("let x = mode", args, 2) == HWB_OK && scalar("mode") == 2);
    CHECK(hwb_continue(&s) == HWB_OK && scalar("x") == 2);
}
static void bars(void)
{
    HWB_TEST("bars"); setup();
    struct hwbf_pci *p = hwbf_add_pci(&f, TEST_BDF, 0x1111, 0x2222);
    /* BAR0 memory, BAR1 I/O, BAR2/3 64-bit memory, BAR4 memory zero base. */
    p->config[0x12] = 0x50; p->config[0x14] = 0x01; p->config[0x15] = 0x07;
    p->config[0x18] = 4; p->config[0x1B] = 0x20;
    CHECK(run("device d = bdf 7:13.2\nwindow v = bar d 0\nmmiowrite8 v + 3 4\npciwrite32 d 0x10 0x600000\nmmiowrite8 v + 3 5\niowrite8 bar d 1 + 2 6\nmmiowrite32 bar d 2 + 0 7\nmmiowrite8 bar d 4 + 0 8") == HWB_OK);
    CHECK(*hwbf_byte(&f, HW_PHYS, 0x500003) == 4 && *hwbf_byte(&f, HW_PHYS, 0x600003) == 5);
    CHECK(*hwbf_byte(&f, HW_IO, 0x702) == 6 && *hwbf_byte(&f, HW_PHYS, 0) == 8);
    const char *bad[] = {"mmioread8 bar d 1 + 0 -> x", "ioread8 bar d 0 + 0 -> x",
        "mmioread8 bar d 3 + 0 -> x", "mmioread8 bar d 6 + 0 -> x"};
    for (u32 n = 0; n < sizeof(bad)/sizeof(bad[0]); n++) {
        CHECK(hwb_command(&s, bad[n], length(bad[n])) == HWB_ARG);
    }
    p->config[0x1C] = 1;
    CHECK(hwb_command(&s, "mmioread8 bar d 2 + 0 -> x", length("mmioread8 bar d 2 + 0 -> x")) == HWB_ARG);
    p->config[0x0E] = 1;
    CHECK(hwb_command(&s, "mmioread8 bar d 2 + 0 -> x", length("mmioread8 bar d 2 + 0 -> x")) == HWB_ARG);
    /* PCI header type 2 exposes exactly BAR0 (CardBus socket base). */
    p->config[0x0E] = 2;
    CHECK(hwb_command(&s, "mmioread8 bar d 0 + 0 -> x", length("mmioread8 bar d 0 + 0 -> x")) == HWB_OK);
    CHECK(hwb_command(&s, "mmioread8 bar d 1 + 0 -> x", length("mmioread8 bar d 1 + 0 -> x")) == HWB_ARG);
}
/* A mismatch must not replace the window or offset used by the next read. */
static void poll_aliases(void)
{
    HWB_TEST("poll-alias"); setup(); f.tick_us = 1;
    hwbf_add_pci(&f, TEST_BDF, 0x1111, 0x2222);
    CHECK(run("device d = bdf 7:13.2\nwindow v = bar d 0\npoll 8 v + 0 255 1 10 1 -> v\niowrite8 port 3 3") == HWB_TIMEOUT);
    CHECK(count_access(HW_PHYS, HW_READ) > 1 && scalar("v") == 0 && s.writes == 0);
    setup(); f.tick_us = 1;
    CHECK(run("let x = 1\npoll 8 port x 255 1 10 1 -> x") == HWB_TIMEOUT);
    CHECK(count_access(HW_IO, HW_READ) > 1 && scalar("x") == 0);
    for (u32 n = 0; n < f.event_count; n++) if (f.events[n].kind == HWBF_ACCESS)
        CHECK(f.events[n].req.addr == 1);
}
static u32 record_run(const char *p)
{
    u32 result = 0;
    while (*p && *p != '=') p++;
    CHECK(*p == '='); p++;
    while (*p >= '0' && *p <= '9') result = result * 10U + (*p++ - '0');
    return result;
}
static void suspended_records(void)
{
    HWB_TEST("suspended-records"); setup();
    CHECK(load("iowrite8 port 1 1\niowrite8 port 2 x", 0, 0) == HWB_OK);
    CHECK(hwb_step(&s) == HWB_OK);
    u32 loaded_run = s.run, pc = s.pc;
    CHECK(hwb_command(&s, "let x = 7", length("let x = 7")) == HWB_OK);
    CHECK(s.run == loaded_run && s.seq == 1 && s.lines == 1 && s.writes == 1 && s.pc == pc);
    u32 interactive_run = record_run(f.lines[2]);
    CHECK(interactive_run != loaded_run);
    CHECK(hwb_command(&s, "let x = x + 1", length("let x = x + 1")) == HWB_OK);
    CHECK(record_run(f.lines[5]) != interactive_run && record_run(f.lines[5]) != loaded_run);
    CHECK(hwb_continue(&s) == HWB_OK && s.lines == 2 && s.writes == 2 && s.seq == 2);
    CHECK(*hwbf_byte(&f, HW_IO, 2) == 8);
    CHECK(record_run(f.lines[8]) == loaded_run && contains(f.lines[8], "seq=2 "));
    CHECK(record_run(f.lines[10]) == loaded_run && contains(f.lines[10], "op=end rc=OK lines=2 writes=2"));
    /* Replacing an unfinished load closes its run exactly once. */
    setup(); CHECK(load("let x = 1\nend", 0, 0) == HWB_OK);
    CHECK(hwb_step(&s) == HWB_OK); loaded_run = s.run;
    CHECK(load("end", 0, 0) == HWB_OK);
    CHECK(record_run(f.lines[2]) == loaded_run && contains(f.lines[2], "op=end rc=ABORT"));
    CHECK(hwb_continue(&s) == HWB_OK);
}
static void begin_records(void)
{
    HWB_TEST("bar-log-address"); setup();
    struct hwbf_pci *pci = hwbf_add_pci(&f, TEST_BDF, 0x1111, 0x2222);
    /* Design §4 example: BAR base 0x20400000 plus VGA offset 0x3C4. */
    pci->config[0x13] = 0x20; pci->config[0x12] = 0x40;
    CHECK(run("device d = bdf 7:13.2\nmmiowrite8 bar d 0 + 0x3C4 9\npoll 8 bar d 0 + 0x3C4 255 9 10 1 -> x") == HWB_OK);
    CHECK(contains(f.lines[2], "phase=begin op=mmiowrite8 target=d addr=0x204003C4"));
    CHECK(contains(f.lines[3], "phase=end op=mmiowrite8 addr=0x204003C4"));
    CHECK(contains(f.lines[4], "phase=begin op=poll target=d addr=0x204003C4"));
    CHECK(contains(f.lines[5], "phase=end op=poll addr=0x204003C4 val=0x09 reads=1"));
    u32 begin = U32_MAX;
    for (u32 n = 0; n < f.event_count; n++) {
        if (f.events[n].kind == HWBF_LINE && contains(f.lines[f.events[n].a], "phase=begin op=mmiowrite8")) begin = n;
        if (f.events[n].kind == HWBF_ACCESS && f.events[n].req.space == HW_PHYS &&
            f.events[n].req.action == HW_WRITE) CHECK(begin < n);
    }
    CHECK(begin != U32_MAX);
    /* Fail each config read, then the resolved MMIO read: no following write. */
    for (u32 fail = 1; fail <= 3; fail++) {
        setup(); pci = hwbf_add_pci(&f, TEST_BDF, 0x1111, 0x2222);
        pci->config[0x13] = 0x20; pci->config[0x12] = 0x40;
        f.access_fail_at = fail;
        CHECK(run("device d = bdf 7:13.2\nmmioread8 bar d 0 + 0x3C4 -> x\niowrite8 port 3 3") == HWB_IO);
        CHECK(contains(f.lines[2], "seq=2 line=2 phase=begin op=mmioread8 target=d"));
        CHECK(contains(f.lines[3], "seq=2 line=2 phase=end op=mmioread8"));
        CHECK(contains(f.lines[3], "rc=IO"));
        CHECK(contains(f.lines[2], fail < 3 ? "addr=UNRESOLVED" : "addr=0x204003C4"));
        CHECK(contains(f.lines[3], fail < 3 ? "addr=UNRESOLVED" : "addr=0x204003C4"));
        CHECK(f.accesses == fail && s.writes == 0 && count_access(HW_IO, HW_WRITE) == 0);
    }
    HWB_TEST("wait-begin-duration"); setup(); f.clock_fail_at = 1;
    CHECK(run("wait us 10") == HWB_IO);
    CHECK(contains(f.lines[0], "phase=begin op=wait requested_us=10"));
}
static void ids(void)
{
    HWB_TEST("ids"); setup();
    hwbf_add_pci(&f, TEST_BDF, 0xABCD, 0x1234);
    CHECK(run("device d = id ABCD:1234\npciread16 d 0 -> v") == HWB_OK && scalar("v") == 0xABCD);
    hwbf_add_pci(&f, HW_BDF(1,1,1), 0xABCD, 0x1234);
    CHECK(run("device d = id ABCD:1234\niowrite8 port 1 1") == HWB_ARG && s.writes == 0);
    u32 candidates = 0;
    for (u32 n = 0; n < f.line_count; n++) if (contains(f.lines[n], "candidate bdf=")) {
        CHECK(contains(f.lines[n], "0x07:0x0D.2") || contains(f.lines[n], "0x01:0x01.1"));
        candidates++;
    }
    CHECK(candidates == 2);
    CHECK(run("device d = id 9999:8888") == HWB_ARG);
}
/* Exact design §8 text; the separate trident stage owns Protect expansion. */
static const char trident[] =
    "# /test/hw/trident.hwb \u2014 vgaHWProtect\u672a\u5c55\u958b\u306e\u8abf\u67fb\u7528\u8ee2\u8a18\n"
    " device d = bdf 0:8.0\n"
    " window v = bar d 1\n"
    " if mode == 1 goto enable\n"
    " if mode == 2 goto disable\n"
    " mmiowrite8 v + 0x3C4 0x09\n"
    " mmioread8 v + 0x3C5 -> chip\n"
    " print \"SR09\" chip\n"
    " end\n"
    " label enable\n"
    " iowrite8 port 0x68 0x0E\n"
    " iowrite8 port 0x6A 0x07\n"
    " iowrite8 port 0x6A 0x8F\n"
    " iowrite8 port 0x6A 0x06\n"
    " # \u5143\u30bd\u30fc\u30b9704: vgaHWProtect(TRUE) \u5c55\u958b\u4f4d\u7f6e\n"
    " mmiowrite8 v + 0x3D4 0x23\n"
    " mmioread8 v + 0x3D5 -> t\n"
    " mmiowrite16 v + 0x3D4 0x23 | ((t & 0xDF) << 8)\n"
    " mmiowrite8 v + 0x3D4 0x29\n"
    " mmioread8 v + 0x3D5 -> t\n"
    " mmiowrite16 v + 0x3D4 0x29 | ((t | 4) << 8)\n"
    " let mask = 0xFF\n"
    " let bits = 6\n"
    " call dac\n"
    " let bits = 8\n"
    " call dac\n"
    " mmiowrite8 v + 0x3CE 0x23\n"
    " mmioread8 v + 0x3CF -> t\n"
    " mmiowrite16 v + 0x3CE 0x23 | ((t & 0xFC) << 8)\n"
    " let bits = 1\n"
    " call dac\n"
    " mmiowrite8 v + 0x3C4 1\n"
    " mmioread8 v + 0x3C5 -> t\n"
    " mmiowrite16 v + 0x3C4 1 | ((t & 0xEF) << 8)\n"
    " # \u5143\u30bd\u30fc\u30b9727: vgaHWProtect(FALSE) \u5c55\u958b\u4f4d\u7f6e\n"
    " iowrite8 port 0xFAC 2\n"
    " end\n"
    " label disable\n"
    " iowrite8 port 0xFAC 0\n"
    " # \u5143\u30bd\u30fc\u30b9740: vgaHWProtect(TRUE) \u5c55\u958b\u4f4d\u7f6e\n"
    " mmiowrite8 v + 0x3C4 1\n"
    " mmioread8 v + 0x3C5 -> t\n"
    " mmiowrite16 v + 0x3C4 1 | ((t | 0x10) << 8)\n"
    " let bits = 0\n"
    " let mask = 0xFE\n"
    " call dac\n"
    " mmiowrite8 v + 0x3CE 0x23\n"
    " mmioread8 v + 0x3CF -> t\n"
    " mmiowrite16 v + 0x3CE 0x23 | (((t & 0xFC) | 1) << 8)\n"
    " let mask = 0xFD\n"
    " call dac\n"
    " let mask = 0xCF\n"
    " call dac\n"
    " let mask = 0xF7\n"
    " call dac\n"
    " let mask = 0xFB\n"
    " call dac\n"
    " mmiowrite8 v + 0x3D4 0x29\n"
    " mmioread8 v + 0x3D5 -> t\n"
    " mmiowrite16 v + 0x3D4 0x29 | ((t & 0xFB) << 8)\n"
    " mmiowrite8 v + 0x3D4 0x23\n"
    " mmioread8 v + 0x3D5 -> t\n"
    " mmiowrite16 v + 0x3D4 0x23 | ((t | 0x20) << 8)\n"
    " # \u5143\u30bd\u30fc\u30b9769: vgaHWProtect(FALSE) \u5c55\u958b\u4f4d\u7f6e\n"
    " iowrite8 port 0x6A 0x07\n"
    " iowrite8 port 0x6A 0x8E\n"
    " iowrite8 port 0x6A 0x06\n"
    " iowrite8 port 0x68 0x0F\n"
    " end\n"
    " label dac\n"
    " mmiowrite8 v + 0x83C8 4\n"
    " mmioread8 v + 0x83C6 -> t\n"
    " mmiowrite8 v + 0x83C8 4\n"
    " mmiowrite8 v + 0x83C6 (t & mask) | bits\n"
    " return\n"
;
static void trident_modes(void)
{
    HWB_TEST("trident-fac-order");
    for (u32 mode = 0; mode < 3; mode++) {
        setup(); struct hwbf_pci *p = hwbf_add_pci(&f, HW_BDF(0,8,0), 0x1023, 0x9660);
        /* Design §8 fake BAR1=0x20400000; little-endian config bytes. */
        p->config[0x16] = 0x40; p->config[0x17] = 0x20;
        struct hwb_initial arg = {"mode",mode};
        CHECK(load(trident, &arg, 1) == HWB_OK && hwb_continue(&s) == HWB_OK);
        u32 first = U32_MAX, last = 0, fac = U32_MAX, fac_count = 0, writes = 0;
        for (u32 n = 0; n < f.event_count; n++) {
            struct hwbf_event *e = &f.events[n];
            if (e->kind != HWBF_ACCESS || e->req.action != HW_WRITE || e->req.space == HW_PCI) continue;
            if (first == U32_MAX) first = n;
            last = n; writes++;
            if (e->req.space == HW_IO && e->req.addr == TRIDENT_FAC) {
                fac = n; fac_count++; CHECK(e->req.width == 1 && e->req.value == (mode == 1 ? 2U : 0U));
            }
        }
        /* Independent counts from the §8 statement list, not executor calculations. */
        CHECK(writes == (mode == 0 ? 1U : mode == 1 ? 22U : 28U));
        CHECK(mode == 0 ? fac_count == 0 : fac_count == 1);
        if (mode == 1) CHECK(fac == last);
        if (mode == 2) CHECK(fac == first);
    }
}
static enum hwb_rc command(const char *text)
{
    return hwb_command(&s, text, length(text));
}
static void arrays(void)
{
    HWB_TEST("array-values"); setup();
    CHECK(run("dim save 1024\nlet save[0] = 0xFFFFFFFF\nlet save[1023] = save[0] - 1\n"
              "let x = save[1] | save[1023]\nlet save[save[1]+2] = 7") == HWB_OK);
    CHECK(scalar("x") == 0xFFFFFFFEU && s.array_values[2] == 7);
    CHECK(contains(f.lines[3], "op=let dest=save index=0 val=0xFFFFFFFF rc=OK"));
    CHECK(command("let save[3] = 9") == HWB_OK && s.array_values[3] == 9);
    CHECK(command("let x = save[3]") == HWB_OK && scalar("x") == 9);
    const struct hwb_program *old = s.program;
    CHECK(load("let x = 1", (struct hwb_initial[]){{"bad!",1}}, 1) == HWB_ARG);
    CHECK(s.program == old && s.array_values[3] == 9 && s.array_used == 1024);
    hwb_reset(&s); CHECK(s.array_used == 0 && s.var_count == 0);
    CHECK(command("dim save 4") == HWB_OK && s.array_values[3] == 0);

    HWB_TEST("array-write-range"); setup();
    CHECK(run("dim a 2\nlet a[1] = 17\nlet a[2] = 99\niowrite8 port 1 1") == HWB_ARG);
    CHECK(s.array_values[1] == 17 && s.array_values[2] == 0 && s.writes == 0);
    CHECK(command("ioread8 port 1 -> a[2]") == HWB_ARG && f.accesses == 0);
    CHECK(command("poll 8 port 1 255 1 10 1 -> a[2]") == HWB_ARG && f.accesses == 0);
    CHECK(command("let a[0-1] = 1") == HWB_ARG);
    CHECK(command("let a[0xFFFFFFFF] = 1") == HWB_ARG);
    CHECK(command("let x = 1") == HWB_OK);
    CHECK(command("let x[0] = 1") == HWB_ARG && scalar("x") == 1);

    HWB_TEST("array-read-range");
    CHECK(command("let x = a[2]") == HWB_ARG && scalar("x") == 1);
    CHECK(command("let x = a[0xFFFFFFFF]") == HWB_ARG && scalar("x") == 1);
    CHECK(command("let x = a") == HWB_ARG);
    CHECK(command("let x = x[0]") == HWB_ARG);

    HWB_TEST("array-redim");
    const char *bad[] = {"dim a 2", "dim x 2", "dim z 0", "let a = 1",
        "device a = bdf 0:0.0", "dim missing", "dim z 1 2",
        "let a[] = 1", "let a[0 = 1", "let a[0][1] = 1", "let x = a[0]1"};
    for (u32 n=0; n<sizeof(bad)/sizeof(*bad); n++) CHECK(command(bad[n]) == HWB_ARG);

    HWB_TEST("array-element-limit"); setup();
    CHECK(run("dim a 8192\ndim b 8192\nlet b[8191] = 77\nlet x = a[8191] + b[8191]") == HWB_OK);
    CHECK(s.array_used == 16384 && scalar("x") == 77);
    CHECK(command("dim c 1") == HWB_ARG && s.array_used == 16384);
    CHECK(run("dim a 16385") == HWB_ARG && s.array_used == 0);
    CHECK(run("dim a 16384\nlet a[16383] = 4") == HWB_OK && s.array_values[16383] == 4);

    HWB_TEST("array-name-limit"); setup();
    CHECK(run("dim a 1") == HWB_OK); /* mode + a */
    for (u32 n=0; n<62; n++) {
        char text[] = "let v00 = 1";
        text[5]='0'+n/10; text[6]='0'+n%10;
        hwbf_clear_trace(&f); CHECK(command(text) == HWB_OK);
    }
    CHECK(s.var_count == 64);
    hwbf_clear_trace(&f);
    CHECK(command("let extra = 1") == HWB_ARG && s.var_count == 64);
    CHECK(command("dim extra 1") == HWB_ARG && s.array_used == 1);
    CHECK(command("let v00 = 2") == HWB_OK && scalar("v00") == 2);
    CHECK(command("let a[0] = 3") == HWB_OK && s.array_values[0] == 3);

    HWB_TEST("array-read-destinations"); setup();
    hwbf_add_pci(&f, TEST_BDF, 0xABCD, 0x1234);
    *hwbf_byte(&f, HW_IO, 9) = 7;
    *hwbf_byte(&f, HW_PHYS, 16) = 8;
    CHECK(run("dim a 4\ndevice d = bdf 7:13.2\npciread16 d 0 -> a[0]\n"
              "ioread8 port 9 -> a[1]\nmmioread8 phys 16 -> a[2]\n"
              "poll 8 port 9 255 7 10 1 -> a[a[2]-5]") == HWB_OK);
    CHECK(s.array_values[0] == 0xABCD && s.array_values[1] == 7 &&
          s.array_values[2] == 8 && s.array_values[3] == 7);
    CHECK(contains(f.lines[5], "val=0xABCD dest=a index=0 rc=OK"));
}
void _start(void)
{
    HWB_TEST("record-pairs"); setup();
    CHECK(run("print \"value\" mode\nlog \"note\"") == HWB_OK);
    CHECK(f.line_count == 5 && contains(f.lines[0], "phase=begin") &&
          contains(f.lines[1], "phase=end") && contains(f.lines[2], "seq=2 ") &&
          contains(f.lines[3], "phase=end"));
    arrays(); fresh_addresses(); write_only(); widths(); bad_args(); failures(); clocks();
    deadlines(); aborts(); sessions(); bars(); poll_aliases(); suspended_records(); begin_records(); ids(); trident_modes();
    hwb_test_write("PASS hwb-exec\n"); hwb_test_exit(0);
}
