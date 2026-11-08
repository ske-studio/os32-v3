/* Real shell adapters/parser/exec/logger. Only KernelAPI and bus are fake. */
/* Full traces for tri_burst1..4: depth/width/length comparisons plus VGA state
 * save/restore. Keep headroom for every event/line and all 4096 LFB bytes. */
#ifndef HWB_SHELL_SKIP_SCRIPTS
#define HWB_SHELL_SKIP_SCRIPTS 0
#endif
#define HWBF_LAZY_TRACE_ZERO
#define HWBF_EVENTS 4194304U
#define HWBF_LINES 1048576U
#define HWBF_CELLS 8192U
#include "hwb_fake.h"
#include "userland/shell/shell.h"
#include "pci_dev_contract.h"
#include "hwb_shell_scripts.h"
/* userland/tests/hw/np21w_tvram.hwb・np21w_poll.hwb が触る TVRAM の番地 (文字の領域の末尾、PM 決定 P11)。 */
#define TVRAM_SPARE 0xA1FFEUL

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_log.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/cmd_hw.c"

static struct hwb_fake bus;
static KernelAPI api;
KernelAPI *g_api = &api;
/* Two simultaneously live loads, each source and index, as in §5. */
static u8 heap[4][HWB_FILE_BYTES];
static u32 allocated[4], live, alloc_calls, fail_alloc;
/* tri_burst5 has a separate indexed-bus oracle for every output record.
 * Run its shell lifecycle without retaining millions of interpreter lines. */
static u32 quiet_script;
static u32 open_file, file_offset, closes, read_fail, raw_nosys;
static char large_file[HWB_FILE_BYTES + 1U];
#define LOG_TEST_BYTES (HWB_LOG_BATCH_BYTES * 8U)
static char log_file[LOG_TEST_BYTES], expected_log[LOG_TEST_BYTES];
static u32 log_bytes, log_opens, log_closes, log_calls, log_short, log_fail, log_zero;
static u32 log_open_fail, diagnostics, terminals;
static int terminal_log_rc;

static void *allocate(u32 bytes)
{
    if (++alloc_calls == fail_alloc) return 0;
    CHECK(bytes <= HWB_FILE_BYTES);
    for (u32 i = 0; i < 4; i++) if (!allocated[i]) {
        allocated[i] = 1; live++; return heap[i];
    }
    CHECK(0); return 0;
}
static void release(void *p)
{
    for (u32 i = 0; i < 4; i++) if (p == heap[i]) {
        CHECK(allocated[i]); allocated[i] = 0; live--; return;
    }
    CHECK(0);
}
static i32 fake_kdebug(u32 op, void *data, u32 bytes)
{
    struct hw_req *r = data;
    CHECK(op == HW_OP && bytes ==
          (HW_IS_BLOCK(r->action) ? HW_BLOCK_REQ_BYTES : HW_REQ_BYTES));
    if (raw_nosys) return OS32_ERR_NOSYS;
    if (r->action == HW_POINT) {
        CHECK(r->version == HW_VERSION && !r->space && !r->width &&
              !r->bdf && !r->addr && !r->value && !r->status);
        return hwbf_point(&bus, &r->value);
    }
    return hwbf_access(&bus, r);
}
static int fake_time(u32 *lo, u32 *hi) { return hwbf_time(&bus, lo, hi); }
static i32 fake_yield(void) { hwbf_yield(&bus); return 0; }
static void output(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (hwbf_equal(fmt, "hw: log rc=%d\n")) { diagnostics++; return; }
    if (hwbf_equal(fmt, "%s log_rc=%d\n")) {
        __builtin_va_list args; __builtin_va_start(args, fmt);
        const char *line = __builtin_va_arg(args, const char *);
        terminal_log_rc = __builtin_va_arg(args, int);
        __builtin_va_end(args);
        terminals++; hwbf_line(&bus, line);
        return;
    }
    if (!hwbf_equal(fmt, "%s\n")) return; /* unavailable diagnostic */
    __builtin_va_list args; __builtin_va_start(args, fmt);
    const char *line = __builtin_va_arg(args, const char *);
    __builtin_va_end(args);
    if (quiet_script) return;
    hwbf_line(&bus, line);
    hwb_test_write(line); hwb_test_write("\n");
}
static int file_open(const char *path, int flags)
{
    if (flags != KAPI_O_RDONLY) {
        CHECK(hwbf_equal(path, "output"));
        CHECK(flags == (KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC));
        log_opens++;
        if (log_open_fail) return OS32_ERR_IO;
        log_bytes = 0; return 2;
    }
    for (u32 i = 0; i < bus.file_count; i++) if (hwbf_equal(path, bus.files[i].name)) {
        open_file = i; file_offset = 0; return 1;
    }
    return OS32_ERR_NOTFOUND;
}
static int file_read(int fd, void *out, u32 bytes)
{
    CHECK(fd == 1 && bytes > 0);
    if (read_fail) return OS32_ERR_IO;
    const struct hwbf_file *file = &bus.files[open_file];
    u32 n = file->bytes - file_offset;
    if (n > (u32)bytes) n = bytes;
    if (n > 17U) n = 17U; /* exercise short reads, independent transport chunk */
    for (u32 i = 0; i < n; i++) ((char *)out)[i] = file->data[file_offset++];
    return n;
}
static int file_write(int fd, const void *data, u32 bytes)
{
    CHECK(fd == 2 && bytes && bytes <= HWB_LOG_BATCH_BYTES);
    log_calls++;
    if (log_fail && log_calls == log_fail) return OS32_ERR_IO;
    if (log_zero) return 0;
    if (log_short && bytes > log_short) bytes = log_short;
    CHECK(log_bytes + bytes < LOG_TEST_BYTES);
    for (u32 i = 0; i < bytes; i++) log_file[log_bytes++] = ((const char *)data)[i];
    log_file[log_bytes] = 0;
    return (int)bytes;
}
static void file_close(int fd)
{
    if (fd == 2) { log_closes++; return; }
    CHECK(fd == 1); closes++;
}
static int pci_count_fake(void) { return (int)bus.pci_count; }
static int pci_get_fake(u32 index, void *out)
{
    CHECK(index < bus.pci_count);
    struct pci_dev *d = out; hwbf_zero(d, sizeof(*d));
    const struct hwbf_pci *p = &bus.pci[index];
    d->bus = HW_BDF_BUS(p->bdf); d->dev = HW_BDF_DEV(p->bdf); d->fn = HW_BDF_FN(p->bdf);
    d->vendor = p->vendor; d->device = p->device;
    return 0;
}
static void setup(void)
{
    CHECK(!live); quiet_script = 0;
    hwbf_init(&bus); hw_initialized = 0; hw_program = 0; hw_source = 0;
    alloc_calls = fail_alloc = closes = read_fail = raw_nosys = 0;
    CHECK(!hw_log_active);
    log_bytes = log_opens = log_closes = log_calls = log_short = log_fail = log_zero = 0;
    log_open_fail = diagnostics = terminals = 0; terminal_log_rc = 0;
    bus.tick_us = 1000000U; /* coarse yielding clock; §3 wait may extend */
    api = (KernelAPI){0}; api.version = HWB_KAPI_MIN;
    api.kdebug_call = fake_kdebug; api.sys_time_now = fake_time;
    api.sys_yield = fake_yield; api.kprintf = output;
    api.mem_alloc = allocate; api.mem_free = release;
    api.sys_open = file_open; api.sys_read = file_read; api.sys_close = file_close;
    api.sys_write = file_write;
    api.pci_count = pci_count_fake; api.pci_get = pci_get_fake;
}
static int command(const char *verb, const char *a, const char *b)
{
    char *args[] = {"hw", (char *)verb, (char *)a, (char *)b};
    return shell_cmd_hw(b ? 4 : a ? 3 : 2, args);
}
static void add(const char *path, const char *text)
{
    u32 n = 0; while (text[n]) n++;
    hwbf_add_file(&bus, path, text, n);
}
static u32 scalar(const char *name)
{
    for (u32 i = 0; i < hw_session.var_count; i++)
        if (hwbf_equal(hw_session.vars[i].name, name)) return hw_session.vars[i].value;
    CHECK(0); return 0;
}
static void adapters(void)
{
    HWB_TEST("adapters"); setup();
    CHECK(hw_backend.access && hw_backend.point && hw_backend.time_now && hw_backend.yield &&
          hw_backend.line && hw_backend.read_file && hw_backend.find_id);
    struct hw_req req = {HW_VERSION, HW_WRITE, HW_IO, 1, 0, 0x715, 0x42, 0};
    CHECK(!hw_backend.access(0, &req)); CHECK(*hwbf_byte(&bus, HW_IO, 0x715) == 0x42);
    u32 flag = 2, lo = 0, hi = 0;
    bus.pending_abort = 1; CHECK(!hw_backend.point(0, &flag) && flag == 1);
    CHECK(!hw_backend.point(0, &flag) && flag == 0);
    CHECK(!hw_backend.time_now(0, &lo, &hi)); hw_backend.yield(0); CHECK(bus.yields == 1);
    hwbf_add_pci(&bus, HW_BDF(7,13,2), 0x1234, 0x5678);
    hwbf_add_pci(&bus, HW_BDF(8,14,3), 0x1234, 0x5678);
    u32 bdf = 0;
    CHECK(hw_backend.find_id(0, 0x1234, 0x5678, 1, &bdf, 1) == 2);
    CHECK(bdf == HW_BDF(8,14,3));
    add("id", "device d = id 1234:5678\niowrite8 port 0x715 1\nend\n");
    CHECK(command("run", "id", 0) == HWB_EXIT_ARG && !live);
    CHECK(bus.accesses == 1); /* only the adapter's preceding direct write */
}
static void lifetime(void)
{
    HWB_TEST("load-lifetime"); setup();
    add("mode", "let saved = mode\niowrite8 port 0x715 saved\nend\n");
    CHECK(command("run", "mode", 0) == 0 && scalar("saved") == 0 && !live);
    CHECK(command("run", "mode", "mode=0x2A") == 0 && scalar("saved") == 42 && !live);
    CHECK(command("run", "mode", "mode=4294967296") == 2 && !live);
    CHECK(command("run", "mode", "mode=") == 2 && !live);
    CHECK(command("run", "mode", "1bad=2") == 2 && !live);
    CHECK(command("load", "mode", "mode=7") == 0 && live == 2);
    u32 pc = hw_session.pc, accesses = bus.accesses;
    CHECK(command("load", "absent", 0) == 5 && live == 2 && hw_session.pc == pc);
    read_fail = 1; CHECK(command("load", "mode", 0) == 5 && live == 2); read_fail = 0;
    fail_alloc = alloc_calls + 1; CHECK(command("load", "mode", 0) == 5 && live == 2); fail_alloc = 0;
    CHECK(command("step", 0, 0) == 0 && scalar("saved") == 7 && bus.accesses == accesses);
    CHECK(command("continue", 0, 0) == 0 && !live);
    CHECK(command("load", "mode", 0) == 0 && live == 2);
    CHECK(command("load", "mode", "mode=8") == 0 && live == 2);
    accesses = bus.accesses;
    CHECK(command("abort", 0, 0) == 130 && !live && bus.accesses == accesses);
    char *let[] = {"hw", "let", "x", "=", "19"};
    CHECK(shell_cmd_hw(5, let) == 0 && scalar("x") == 19);
    CHECK(command("print", "a b", "x") == 0);
    CHECK(command("log", "hello world", 0) == 0);
    CHECK(command("reset", 0, 0) == 0 && !hw_session.var_count);
    for (u32 i = 0; i < sizeof(large_file); i++) large_file[i] = '\n';
    hwbf_add_file(&bus, "large", large_file, HWB_FILE_BYTES);
    CHECK(command("run", "large", 0) == 0 && !live);
    bus.files[bus.file_count - 1].bytes++;
    CHECK(command("load", "large", 0) == 5 && !live);
    CHECK(closes > 0);
}
static void exit_codes(void)
{
    HWB_TEST("exit-codes"); setup();
    api.version = HWB_KAPI_MIN - 1; CHECK(command("let", "x=1", 0) == 3);
    api.version = HWB_KAPI_MIN; api.kdebug_call = 0; CHECK(command("let", "x=1", 0) == 3);
    api.kdebug_call = fake_kdebug; raw_nosys = 1;
    add("read", "ioread8 port 0x60 -> x\niowrite8 port 0x715 1\nend\n");
    CHECK(command("run", "read", 0) == 3 && !bus.accesses && !live); raw_nosys = 0;
    const u32 failures[] = {HW_NOSYS, HW_WINDOW, HW_IO_ERROR};
    const int statuses[] = {3,4,5};
    for (u32 i = 0; i < 3; i++) {
        hwbf_clear_trace(&bus); bus.status_fail_at = 1; bus.failure_status = failures[i];
        CHECK(command("run", "read", 0) == statuses[i] && bus.accesses == 1 && !live);
    }
    bus.status_fail_at = 0;
    add("poll", "poll 8 phys 0xA0000 1 1 1000 100 -> x\nend\n");
    CHECK(command("run", "poll", 0) == 124 && !live);
}
static void unresolved(void)
{
    HWB_TEST("unresolved-address"); setup();
    /* P9: missing scalar prevents all data accesses, including later writes. */
    char *args[] = {"hw", "mmioread8", "phys", "missing", "->", "x"};
    CHECK(shell_cmd_hw(6, args) == HWB_EXIT_ARG && !bus.accesses && !live);
    add("missing-write", "mmiowrite8 phys missing 1\niowrite8 port 0x715 1\nend\n");
    CHECK(command("run", "missing-write", 0) == HWB_EXIT_ARG && !bus.accesses && !live);
    add("missing-poll", "poll 8 phys missing 1 1 1000 100 -> x\niowrite8 port 0x715 1\nend\n");
    CHECK(command("run", "missing-poll", 0) == HWB_EXIT_ARG && !bus.accesses && !live);
}
static void scripts(void)
{
    HWB_TEST("production-scripts");
    for (u32 i = 0; i < sizeof(scripts_data) / sizeof(scripts_data[0]); i++) {
        const struct script_data *script = &scripts_data[i];
        u32 modes = hwbf_equal(script->name, "trident.hwb") ? 3 : 1;
        for (u32 mode = 0; mode < modes; mode++) {
            setup();
            hwbf_add_file(&bus, script->name, script->source, script->bytes);
            /* §8 example BAR1, independent from shell address resolver. */
            struct hwbf_pci *p = hwbf_add_pci(&bus, HW_BDF(0,8,0), 0x1023, 0x9660);
            p->config[0x17] = 0x20; p->config[0x16] = 0x40; /* BAR1 0x20400000 */
            if (hwbf_equal(script->name, "tri_w32.hwb") || (hwbf_equal(script->name, "tri_burst.hwb") || hwbf_equal(script->name, "tri_burst2.hwb") || hwbf_equal(script->name, "tri_burst3.hwb") || hwbf_equal(script->name, "tri_burst4.hwb") || hwbf_equal(script->name, "tri_burst5.hwb"))) {
                p->config[0x13] = 0x20; /* BAR0 0x20000000 */
                for (u32 n = 0; n < ((hwbf_equal(script->name, "tri_burst.hwb") || hwbf_equal(script->name, "tri_burst2.hwb") || hwbf_equal(script->name, "tri_burst3.hwb") || hwbf_equal(script->name, "tri_burst4.hwb") || hwbf_equal(script->name, "tri_burst5.hwb")) ? 4096U : 32U); n++)
                    *hwbf_byte(&bus, HW_PHYS, 0x20000000U + n) = 0x80U + n;
            }
            if (hwbf_equal(script->name, "gf_regs.hwb") || hwbf_equal(script->name, "gf_palette.hwb")) {
                /* Ra266 GeForce 0:13.0, BAR0 0x24000000 (RA266 §1-1). */
                struct hwbf_pci *g = hwbf_add_pci(&bus, HW_BDF(0,13,0), 0x10de, 0x0221);
                g->config[0x13] = 0x24;
            }
            if (hwbf_equal(script->name, "hpt370_probe.hwb")) {
                /* Ra266 HighPoint 0:12.0 (RA266 §1-1): no drive attached, status reads 0. */
                struct hwbf_pci *h = hwbf_add_pci(&bus, HW_BDF(0,12,0), 0x1103, 0x0004);
                h->config[0x10] = 0x21; h->config[0x11] = 0x60; /* cmd0 0x6020 */
                h->config[0x14] = 0x29; h->config[0x15] = 0x60; /* ctl0 0x6028 */
                h->config[0x18] = 0x31; h->config[0x19] = 0x60; /* cmd1 0x6030 */
                h->config[0x1C] = 0x2D; h->config[0x1D] = 0x60; /* ctl1 0x602c */
                h->config[0x20] = 0x01; h->config[0x21] = 0xC0; /* bm 0xc000 */
            }
            quiet_script = hwbf_equal(script->name, "tri_burst5.hwb");
            int expected = 0;
            if (hwbf_equal(script->name, "np21w_nopci.hwb")) { bus.mechanism = 0; expected = 3; }
            if (hwbf_equal(script->name, "np21w_poll.hwb")) expected = 124;
            if (hwbf_equal(script->name, "np21w_loop.hwb")) { bus.point_at = 8; expected = 130; }
            const char *arg = mode == 1 ? "mode=1" : mode == 2 ? "mode=2" : 0;
            CHECK(command("run", script->name, arg) == expected && !live);
            if (hwbf_equal(script->name, "tri_w32.hwb")) {
                CHECK(scalar("same") == 1);
                for (u32 n = 0; n < 32; n++)
                    CHECK(*hwbf_byte(&bus, HW_PHYS, 0x20000000U + n) == 0x80U + n);
            }
            if ((hwbf_equal(script->name, "tri_burst.hwb") || hwbf_equal(script->name, "tri_burst2.hwb") || hwbf_equal(script->name, "tri_burst3.hwb") || hwbf_equal(script->name, "tri_burst4.hwb") || hwbf_equal(script->name, "tri_burst5.hwb"))) {
                CHECK(scalar("bad") == 0);
                for (u32 n = 0; n < 4096; n++)
                    CHECK(*hwbf_byte(&bus, HW_PHYS, 0x20000000U+n) == (u8)(0x80U+n));
                CHECK(bus.line_count < HWBF_LINES && bus.event_count < HWBF_EVENTS);
            }
            if (hwbf_equal(script->name, "np21w_gdc.hwb")) CHECK(*hwbf_byte(&bus, HW_IO, 0x68) == 0x0F);
            if (hwbf_equal(script->name, "np21w_tvram.hwb")) CHECK(*hwbf_byte(&bus, HW_PHYS, TVRAM_SPARE) == 0);
            if (hwbf_equal(script->name, "trident.hwb")) {
                CHECK(scalar("mode") == mode);
                if (mode) CHECK(*hwbf_byte(&bus, HW_IO, 0xFAC) == (mode == 1 ? 2 : 0));
            }
        }
    }
}
static void arrays(void)
{
    HWB_TEST("shell-arrays"); setup();
    CHECK(command("dim", "save", "1024") == 0);
    CHECK(command("let", "save[3]=0xA5", 0) == 0);
    CHECK(command("print", "saved", "save[3]") == 0);
    CHECK(command("ioread8", "port 9 -> save[4]", 0) == 0);
    CHECK(command("let", "save[1024]=1", 0) == 2);
    CHECK(command("dim", "save", "1") == 2);
    CHECK(command("reset", 0, 0) == 0 && !hw_session.array_used);
    CHECK(command("dim", "save", "4") == 0 && !hw_session.array_values[3]);
    add("arrays", "dim a 4\nlet a[1] = 27\niowrite8 port 9 a[1]\nend");
    CHECK(command("load", "arrays", 0) == 0 && live == 2);
    CHECK(command("step", 0, 0) == 0 && hw_session.array_used == 4);
    CHECK(command("let", "a[0]=19", 0) == 0);
    CHECK(command("continue", 0, 0) == 0 && !live && hw_session.array_values[0] == 19);
    CHECK(*hwbf_byte(&bus, HW_IO, 9) == 27);
}
static int logged(const char *verb, const char *path, const char *arg)
{
    char *args[] = {"hw", (char *)verb, "-o", "output", (char *)path, (char *)arg};
    return shell_cmd_hw(arg ? 6 : 5, args);
}
static void logfile(void)
{
    HWB_TEST("log-file"); setup();
    /* Long enough to cross several batches, without a total-log ceiling. */
    const char *script = "let i = 0\nlabel loop\nlet i = i + 1\nif i != 30 goto loop\nend\n";
    add("batch", script);
    CHECK(command("run", "batch", 0) == 0);
    u32 expected = 0;
    for (u32 i = 0; i < bus.line_count; i++) {
        for (const char *p = bus.lines[i]; *p; p++) {
            CHECK(expected < LOG_TEST_BYTES); expected_log[expected++] = *p;
        }
        expected_log[expected++] = '\n';
    }
    CHECK(expected > 2U * HWB_LOG_BATCH_BYTES);
    u32 first_run = hw_session.run;
    hwbf_clear_trace(&bus);
    CHECK(logged("run", "batch", 0) == 0 && !live);
    CHECK(log_bytes == expected && bus.line_count == 1 && terminals == 1);
    CHECK(log_opens == 1 && log_closes == 1 && log_calls ==
          (expected + HWB_LOG_BATCH_BYTES - 1U) / HWB_LOG_BATCH_BYTES);
    CHECK(!diagnostics && !terminal_log_rc);
    CHECK(hw_session.run == first_run + 1U && hw_session.run < 10U);
    /* Only run IDs differ; every other byte, including final newline, agrees. */
    for (u32 i = 0; i < expected; i++) {
        if (i >= 7 && expected_log[i - 1] == '=' && expected_log[i - 2] == 'n')
            CHECK(log_file[i] == expected_log[i] + 1);
        else CHECK(log_file[i] == expected_log[i]);
    }
    /* Feed real file records through the host collector too. */
    hwb_test_write("FILE_BEGIN\n"); hwb_test_write(log_file); hwb_test_write("FILE_END\n");
    setup(); add("small", "let x = mode\nend\n"); log_short = 7;
    CHECK(logged("load", "small", "mode=42") == 0 && live == 2 && !log_closes);
    CHECK(command("step", 0, 0) == 0 && scalar("x") == 42 && log_bytes && !bus.line_count);
    CHECK(command("continue", 0, 0) == 0 && !live && log_closes == 1 && terminals == 1);
    CHECK(log_calls > 2 && !diagnostics);
    CHECK(logged("run", "small", 0) == 0 && log_opens == 2 && log_closes == 2);
    setup(); add("small", "end\n"); log_open_fail = 1;
    CHECK(logged("run", "small", 0) == 5 && !live && !bus.accesses);
    CHECK(diagnostics == 1 && terminals == 1 && terminal_log_rc == OS32_ERR_IO && !log_closes);
    setup(); add("small", "end\n"); log_fail = 1;
    CHECK(logged("run", "small", 0) == 5 && !live && log_closes == 1);
    CHECK(diagnostics == 1 && terminals == 1 && terminal_log_rc == OS32_ERR_IO);
    setup(); add("batch", script); log_fail = 2;
    CHECK(logged("run", "batch", 0) == 5 && !live && log_bytes == HWB_LOG_BATCH_BYTES);
    CHECK(diagnostics == 1 && terminals == 1 && log_closes == 1 && bus.line_count == 1);
    setup(); add("small", "end\n"); log_zero = 1;
    CHECK(logged("run", "small", 0) == 5 && diagnostics == 1 && log_closes == 1);
    setup(); add("bad", "ioread8 port missing -> x\nend\n");
    CHECK(logged("run", "bad", 0) == 2 && !terminal_log_rc && terminals == 1 && log_bytes);
    setup(); add("small", "end\n");
    CHECK(logged("load", "small", 0) == 0);
    CHECK(command("abort", 0, 0) == 130 && log_closes == 1 && terminals == 1);
    setup(); add("small", "end\n");
    CHECK(logged("load", "small", 0) == 0);
    CHECK(command("run", "absent", 0) == 5 && hw_log_active && live == 2);
    CHECK(command("run", "small", 0) == 0 && !hw_log_active && !live && log_closes == 1);
    setup(); add("small", "let x = 1\nend\n");
    CHECK(logged("load", "small", 0) == 0); log_fail = 1;
    CHECK(command("step", 0, 0) == 5 && hw_log_active);
    CHECK(command("run", "small", 0) == 0 && !hw_log_active && !live && log_closes == 1);
    setup(); add("small", "end\n");
    char *missing[] = {"hw", "run", "-o", "output"};
    CHECK(shell_cmd_hw(3, missing) == 2 && !log_opens);
    CHECK(shell_cmd_hw(4, missing) == 2 && !log_opens);
    CHECK(logged("run", "small", "invalid=") == 2 && !log_opens);
    CHECK(logged("run", "absent", 0) == 5 && !log_opens);
}
void _start(void)
{
    logfile(); adapters(); lifetime(); arrays(); exit_codes(); unresolved();
    if (!HWB_SHELL_SKIP_SCRIPTS) scripts();
    hwb_test_write("SUMMARY PASS hwb shell\n"); hwb_test_exit(0);
}
