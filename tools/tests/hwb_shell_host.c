/* Real shell adapters/parser/exec/logger. Only KernelAPI and bus are fake. */
/* gf_regs.hwb reads CR00-CR9F in a loop: each command logs begin/end. */
#define HWBF_LINES 4096U
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
#include "userland/shell/hwb_log.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/cmd_hw.c"

static struct hwb_fake bus;
static KernelAPI api;
KernelAPI *g_api = &api;
/* Two simultaneously live loads, each source and index, as in §5. */
static u8 heap[4][HWB_FILE_BYTES];
static u32 allocated[4], live, alloc_calls, fail_alloc;
static u32 open_file, file_offset, closes, read_fail, raw_nosys;
static char large_file[HWB_FILE_BYTES + 1U];
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
    CHECK(op == HW_OP && bytes == sizeof(*r));
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
    if (!hwbf_equal(fmt, "%s\n")) return; /* unavailable diagnostic */
    __builtin_va_list args; __builtin_va_start(args, fmt);
    const char *line = __builtin_va_arg(args, const char *);
    __builtin_va_end(args);
    hwbf_line(&bus, line);
    hwb_test_write(line); hwb_test_write("\n");
}
static int file_open(const char *path, int flags)
{
    CHECK(flags == KAPI_O_RDONLY);
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
static void file_close(int fd) { CHECK(fd == 1); closes++; }
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
    CHECK(!live);
    hwbf_init(&bus); hw_initialized = 0; hw_program = 0; hw_source = 0;
    alloc_calls = fail_alloc = closes = read_fail = raw_nosys = 0;
    bus.tick_us = 1000000U; /* coarse yielding clock; §3 wait may extend */
    api = (KernelAPI){0}; api.version = HWB_KAPI_MIN;
    api.kdebug_call = fake_kdebug; api.sys_time_now = fake_time;
    api.sys_yield = fake_yield; api.kprintf = output;
    api.mem_alloc = allocate; api.mem_free = release;
    api.sys_open = file_open; api.sys_read = file_read; api.sys_close = file_close;
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
            if (hwbf_equal(script->name, "gf_regs.hwb")) {
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
            int expected = 0;
            if (hwbf_equal(script->name, "np21w_nopci.hwb")) { bus.mechanism = 0; expected = 3; }
            if (hwbf_equal(script->name, "np21w_poll.hwb")) expected = 124;
            if (hwbf_equal(script->name, "np21w_loop.hwb")) { bus.point_at = 8; expected = 130; }
            const char *arg = mode == 1 ? "mode=1" : mode == 2 ? "mode=2" : 0;
            CHECK(command("run", script->name, arg) == expected && !live);
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
void _start(void)
{
    adapters(); lifetime(); arrays(); exit_codes(); unresolved(); scripts();
    hwb_test_write("SUMMARY PASS hwb shell\n"); hwb_test_exit(0);
}
