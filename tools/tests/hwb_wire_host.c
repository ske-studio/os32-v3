/* Same TU compiled by host gcc and i386-elf-gcc; no libc startup or assert. */
#ifdef HWB_SHELL_HEADERS
#include "os32_kapi_shared.h"
#endif
#include "hwb_fake.h"
#include "drivers/pci/pci_hwb.h"
#include "exec/exec_hwb.h"
#include "kdebug/recover_wire.h"
#include "kdebug/gfinit_wire.h"
#include "kdebug/gf_foundation_wire.h"
#include "kdebug/gf_command_wire.h"

/* Compile the private import/dispatch signatures without requiring bodies. */
STATIC_ASSERT(__builtin_types_compatible_p(__typeof__(&pci_hwb_read),
    u32 (*)(u32, u32, u32, u32 *)), pci_hwb_read_signature);
STATIC_ASSERT(__builtin_types_compatible_p(__typeof__(&pci_hwb_write),
    u32 (*)(u32, u32, u32, u32)), pci_hwb_write_signature);
STATIC_ASSERT(__builtin_types_compatible_p(__typeof__(&exec_hwb_abort_take),
    u32 (*)(void)), exec_hwb_abort_signature);
STATIC_ASSERT(__builtin_types_compatible_p(__typeof__(((struct hwb_instruction *)0)->space),
    enum hw_space), hwb_instruction_space_type);
#ifdef __KERNEL_BUILD__
STATIC_ASSERT(__builtin_types_compatible_p(__typeof__(&hwb_access),
    i32 (*)(void *, u32)), hwb_access_signature);
#endif
static struct hwb_fake fake;
static struct hw_req request(u32 action, u32 space, u32 width, u32 bdf, u32 addr, u32 val)
{
    return (struct hw_req){HW_VERSION,action,space,width,bdf,addr,val,HW_OK};
}
static void wire(void)
{
    HWB_TEST("wire");
    CHECK(sizeof(u32) == 4 && sizeof(struct hw_req) == 32);
    CHECK(__builtin_offsetof(struct hw_req, status) == 28);
    CHECK(HW_OP == 18 && HW_VERSION == 1 && HW_PCI_CONFIG_BYTES == 256);
    CHECK(HW_READ == 1 && HW_WRITE == 2 && HW_POINT == 3);
    CHECK(HW_PCI == 1 && HW_IO == 2 && HW_PHYS == 3);
    CHECK(HW_OK == 0 && HW_ARG == 1 && HW_NOSYS == 2 && HW_WINDOW == 3 && HW_IO_ERROR == 4);
    /* All occupied op numbers, not enum values/operands from those headers. */
    const u32 used[] = {KDEBUG_OP_PEEK, KDEBUG_OP_PEEK_LAN_PROBE,
        KDEBUG_OP_RECOVER_STATUS, KDEBUG_OP_RECOVER_NOW, GF_OP_INSPECT,
        GF_OP_SIZE_BARS, GF_OP_PREPARE, GF_OP_RUN_ONE, GF_OP_VERIFY, GF_OP_ABORT,
        KDEBUG_OP_RECOVER_VIRTUAL, KDEBUG_OP_RECOVER_ROUTE_TEST, GF_OP_MODESET,
        GF_OP_C_ONLY, KDEBUG_OP_RECOVER_RESTORE, GF_OP_FOUNDATION, GF_OP_COMMAND};
    for (u32 i = 0; i < sizeof(used) / sizeof(used[0]); i++) {
        CHECK(used[i] == i + 1); CHECK(HW_OP != used[i]);
    }
    HWB_TEST("bdf");
    /* §3 table independently specifies 8 bus bits, 5 device bits, 3 fn bits. */
    CHECK(HW_BDF(0, 0, 0) == 0);
    CHECK(HW_BDF(255, 31, 7) == 65535);
    CHECK(HW_BDF(1, 1, 1) == 265);
    for (u32 bus = 0; bus <= 255; bus++)
        for (u32 dev = 0; dev <= 31; dev++)
            for (u32 fn = 0; fn <= 7; fn++) {
                u32 bdf = HW_BDF(bus, dev, fn);
                CHECK(HW_BDF_BUS(bdf) == bus && HW_BDF_DEV(bdf) == dev && HW_BDF_FN(bdf) == fn);
            }
    HWB_TEST("limits");
    CHECK(HWB_VARIABLES == 32 && HWB_NAME_CHARS == 16 && HWB_LINE_BYTES == 256);
    CHECK(HWB_FILE_BYTES == 65536 && HWB_LABELS == 64 && HWB_CALL_DEPTH == 8);
    CHECK(HWB_US_MIN == 1 && HWB_US_MAX == 10000000 && HWB_ITERATIONS == 1000000);
    const int exits[] = {HWB_EXIT_OK,HWB_EXIT_ARG,HWB_EXIT_NOSYS,HWB_EXIT_WINDOW,
                         HWB_EXIT_IO,HWB_EXIT_TIMEOUT,HWB_EXIT_ABORT};
    const int expected[] = {0,2,3,4,5,124,130}; /* §7 exit table */
    for (u32 i = 0; i < sizeof(exits)/sizeof(exits[0]); i++) CHECK(exits[i] == expected[i]);
    CHECK(HWB_OK == 0 && HWB_ARG == 1 && HWB_NOSYS == 2 && HWB_WINDOW == 3 &&
          HWB_IO == 4 && HWB_TIMEOUT == 5 && HWB_ABORT == 6);
}
static void bus(void)
{
    HWB_TEST("bus"); hwbf_init(&fake);
    const struct hwb_backend *b = &fake.backend;
    /* PCI command/status DWORD: Command 04h, W1C Status 06h (PCI §6.2.3). */
    struct hwbf_pci *p = hwbf_add_pci(&fake, 0x40, 0x1023, 0x9660);
    p->config[6] = 0x09; p->config[7] = 0xF9;
    struct hw_req r = request(HW_WRITE, HW_PCI, 2, 0x40, 4, 7);
    CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_OK && r.value == 7);
    CHECK(p->config[4] == 7 && p->config[5] == 0 && p->config[6] == 9 && p->config[7] == 0xF9);
    r = request(HW_WRITE, HW_PCI, 1, 0x40, 5, 0x12);
    CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_OK);
    r = request(HW_READ, HW_PCI, 4, 0x40, 4, 0);
    CHECK(b->access(b->ctx, &r) == 0 && r.value == 0xF9091207UL);
    r = request(HW_WRITE, HW_PCI, 1, 0x40, 7, 0x80);
    CHECK(b->access(b->ctx, &r) == 0 && p->config[7] == 0x79);
    r = request(HW_WRITE, HW_PCI, 2, 0x40, 6, 0x7900);
    CHECK(b->access(b->ctx, &r) == 0 && p->config[6] == 9 && p->config[7] == 0);
    r = request(HW_READ, HW_PCI, 2, 0x40, 0, 0);
    CHECK(b->access(b->ctx, &r) == 0 && r.value == 0x1023);
    r = request(HW_READ, HW_PCI, 1, 0x40, 3, 0);
    CHECK(b->access(b->ctx, &r) == 0 && r.value == 0x96);
    for (u32 width = 1; width <= 4; width *= 2) {
        r = request(HW_READ, HW_PCI, width, 0xFFFF, 0, 0);
        CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_OK);
        CHECK(r.value == (width == 1 ? 0xFFUL : width == 2 ? 0xFFFFUL : 0xFFFFFFFFUL));
    }
    fake.mechanism = 0;
    r = request(HW_READ, HW_PCI, 4, 0x40, 0, 0);
    CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_NOSYS && r.value == 0);
    fake.mechanism = 1;
    r = request(HW_WRITE, HW_PCI, 2, 0x40, 255, 1);
    CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_ARG && p->config[255] == 0);
    /* Arbitrary port and physical cells, independent byte lanes. */
    for (u32 space = HW_IO; space <= HW_PHYS; space++) {
        u32 addr = space == HW_IO ? 0x1234 : 0x204003C4UL; /* arbitrary test addresses */
        r = request(HW_WRITE, space, 4, 0, addr, 0x12345678);
        CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_OK);
        r = request(HW_WRITE, space, 1, 0, addr + 1, 0xAB);
        CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_OK);
        r = request(HW_READ, space, 4, 0, addr, 0);
        CHECK(b->access(b->ctx, &r) == 0 && r.value == 0x1234AB78);
        hwbf_add_register(&fake, space, 0, addr, 4, 1);
        r = request(HW_READ, space, 1, 0, addr + 1, 0);
        CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_IO_ERROR && r.value == 0);
        r = request(HW_WRITE, space, 4, 0, addr, 0x87654321);
        CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_OK);
    }
    HWB_TEST("trace");
    CHECK(fake.events[0].kind == HWBF_ACCESS && fake.events[0].req.width == 2 &&
          fake.events[0].req.addr == 4 && fake.events[0].req.value == 7);
    CHECK(fake.events[1].req.action == HW_WRITE && fake.events[1].req.width == 1 && fake.events[1].req.addr == 5);
    CHECK(fake.events[2].req.action == HW_READ && fake.events[2].req.width == 4);
    CHECK(fake.events[2].result.value == 0xF9091207UL && fake.events[2].result.status == HW_OK);
    CHECK(fake.accesses == fake.event_count);
    fake.access_fail_at = fake.accesses + 1;
    r = request(HW_WRITE, HW_PCI, 1, 0x40, 4, 0xFF);
    CHECK(b->access(b->ctx, &r) == OS32_ERR_IO && p->config[4] == 7);
    CHECK(fake.events[fake.event_count - 1].rc == OS32_ERR_IO);
    fake.status_fail_at = fake.accesses + 1;
    CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_IO_ERROR && p->config[4] == 7);
}
static void invalid_requests(void)
{
    HWB_TEST("wire-errors"); hwbf_init(&fake);
    const struct hwb_backend *b = &fake.backend;
    /* §3 malformed envelope/unused inputs: call fails, no output writes. */
    struct hw_req malformed[] = {
        {HW_VERSION + 1, HW_WRITE, HW_IO, 1, 0, 0, 1, 0},
        {HW_VERSION, HW_WRITE, HW_PHYS, 1, 0, 0, 1, HW_ARG},
        {HW_VERSION, HW_READ, HW_PCI, 1, 0, 0, 1, 0},
        {HW_VERSION, 0, HW_IO, 1, 0, 0, 1, 0},
        {HW_VERSION, HW_WRITE, 0, 1, 0, 0, 1, 0},
        {HW_VERSION, HW_WRITE, HW_IO, 1, 1, 0, 1, 0},
        {HW_VERSION, HW_READ, HW_PHYS, 1, 1, 0, 0, 0},
    };
    for (u32 i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
        struct hw_req r = malformed[i];
        CHECK(b->access(b->ctx, &r) == OS32_ERR_INVAL);
        const u8 *before = (const u8 *)&malformed[i], *after = (const u8 *)&r;
        for (u32 n = 0; n < sizeof(r); n++) CHECK(before[n] == after[n]);
        CHECK(fake.events[i].rc == OS32_ERR_INVAL);
        CHECK(fake.bus_accesses == 0 && fake.cell_count == 0);
    }
    HWB_TEST("access-errors");
    /* §3 valid envelope, invalid access width/range/alignment/value. */
    struct hw_req invalid[] = {
        {HW_VERSION, HW_READ, HW_PCI, 3, 0, 0, 0, 0},
        {HW_VERSION, HW_READ, HW_PCI, 1, HW_BDF_MASK + 1, 0, 0, 0},
        {HW_VERSION, HW_WRITE, HW_PCI, 2, 0, HW_PCI_CONFIG_BYTES - 1, 1, 0},
        {HW_VERSION, HW_READ, HW_IO, 2, 0, U16_MAX, 0, 0},
        {HW_VERSION, HW_WRITE, HW_PHYS, 4, 0, U32_MAX, 1, 0},
        {HW_VERSION, HW_READ, HW_PHYS, 2, 0, 1, 0, 0},
        {HW_VERSION, HW_WRITE, HW_IO, 1, 0, 0, U8_MAX + 1U, 0},
    };
    for (u32 i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        struct hw_req r = invalid[i];
        CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_ARG);
        CHECK(r.value == invalid[i].value);
        CHECK(fake.events[fake.event_count - 1].rc == 0);
        CHECK(fake.bus_accesses == 0 && fake.cell_count == 0);
    }
    /* Same bus is usable after rejection: a valid write reaches it once. */
    struct hw_req r = request(HW_WRITE, HW_IO, 1, 0, 0, 1);
    CHECK(b->access(b->ctx, &r) == 0 && r.status == HW_OK);
    CHECK(fake.bus_accesses == 1 && fake.cell_count == 1 && fake.cells[0].value == 1);
}
static void boundaries(void)
{
    HWB_TEST("clock"); hwbf_init(&fake);
    const struct hwb_backend *b = &fake.backend;
    u32 lo = 0, hi = 0;
    fake.now_lo = 0xFFFFFFFEUL; fake.now_hi = 9; fake.tick_us = 3;
    CHECK(b->time_now(b->ctx, &lo, &hi) == 0 && lo == 0xFFFFFFFEUL && hi == 9);
    CHECK(b->time_now(b->ctx, &lo, &hi) == 0 && lo == 1 && hi == 10);
    fake.tick_us = 0;
    CHECK(b->time_now(b->ctx, &lo, &hi) == 0 && lo == 4 && hi == 10);
    CHECK(b->time_now(b->ctx, &lo, &hi) == 0 && lo == 4 && hi == 10);
    fake.reverse_clock = 1; fake.tick_us = 7;
    CHECK(b->time_now(b->ctx, &lo, &hi) == 0 && lo == 4 && hi == 10);
    CHECK(b->time_now(b->ctx, &lo, &hi) == 0 && lo == 0xFFFFFFFDUL && hi == 9);
    fake.clock_fail_at = fake.times + 1;
    CHECK(b->time_now(b->ctx, &lo, &hi) == OS32_ERR_IO && lo == 0xFFFFFFFDUL && hi == 9);
    HWB_TEST("point"); fake.point_at = 2; fake.pending_abort = 1;
    u32 flag;
    CHECK(b->point(b->ctx, &flag) == 0 && flag == 1 && fake.pending_abort == 0);
    CHECK(b->point(b->ctx, &flag) == 0 && flag == 1);
    CHECK(b->point(b->ctx, &flag) == 0 && flag == 0);
    fake.point_error = OS32_ERR_NOSYS; flag = 9;
    CHECK(b->point(b->ctx, &flag) == OS32_ERR_NOSYS && flag == 9);
    b->yield(b->ctx); CHECK(fake.yields == 1);
    HWB_TEST("lines"); b->line(b->ctx, "first"); b->line(b->ctx, "second");
    CHECK(fake.line_count == 2 && hwbf_equal(fake.lines[0], "first") && hwbf_equal(fake.lines[1], "second"));
    CHECK(fake.events[fake.event_count - 3].kind == HWBF_YIELD);
    CHECK(fake.events[fake.event_count - 2].kind == HWBF_LINE && fake.events[fake.event_count - 2].a == 0);
    HWB_TEST("files");
    hwbf_add_file(&fake, "/a", "end", 3); hwbf_add_file(&fake, "/b", "log", 3);
    char buf[4] = {0}; u32 bytes = 99;
    CHECK(b->read_file(b->ctx, "/a", buf, 4, &bytes) == 0 && bytes == 3 && hwbf_equal(buf, "end"));
    CHECK(b->read_file(b->ctx, "/b", buf, 4, &bytes) == 0 && hwbf_equal(buf, "log"));
    bytes = 99;
    CHECK(b->read_file(b->ctx, "/a", buf, 2, &bytes) == OS32_ERR_INVAL && bytes == 99 && hwbf_equal(buf, "log"));
    CHECK(b->read_file(b->ctx, "/missing", buf, 4, &bytes) == OS32_ERR_NOTFOUND && bytes == 99);
    fake.file_error = OS32_ERR_IO;
    CHECK(b->read_file(b->ctx, "/a", buf, 4, &bytes) == OS32_ERR_IO && bytes == 99);
    HWB_TEST("find");
    hwbf_add_pci(&fake, 1, 0x1023, 0x9660); hwbf_add_pci(&fake, 9, 0x1023, 0x9660);
    hwbf_add_pci(&fake, 8, 0x8086, 0x1229);
    u32 bdfs[2] = {99,99};
    CHECK(b->find_id(b->ctx, 0x1023, 0x9660, 0, bdfs, 2) == 2 && bdfs[0] == 1 && bdfs[1] == 9);
    bdfs[0] = 99;
    CHECK(b->find_id(b->ctx, 0x1023, 0x9660, 1, bdfs, 1) == 2 && bdfs[0] == 9);
    CHECK(b->find_id(b->ctx, 0, 0, 0, bdfs, 1) == 0 && bdfs[0] == 9);
    fake.find_error = OS32_ERR_IO;
    CHECK(b->find_id(b->ctx, 0, 0, 0, bdfs, 1) == OS32_ERR_IO && bdfs[0] == 9);
    hwbf_clear_trace(&fake);
    CHECK(fake.event_count == 0 && fake.line_count == 0 && fake.pci_count == 3 && fake.file_count == 2);
}
void _start(void)
{
    wire(); bus(); invalid_requests(); boundaries();
    hwb_test_write("PASS wire/bdf/limits, wire-errors/access-errors, fake bus/W1C/lane/trace/clock/point/files/ID\n");
    hwb_test_exit(0);
}
