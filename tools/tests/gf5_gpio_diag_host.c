/* B03 diagnostic boundary fixture. DCB values from the MIT reference:
 * nouveau c03c1a1 nvkm/subdev/gpio/base.c:80-109 resolves each get/set;
 * log[state] bit 1 is direction, bit 0 is output; get uses log[1] polarity.
 * These callbacks model only the module boundary, no hardware or ROM parser. */
#include "../../hwdev/gf/feature5.h"
#include "../../kdebug/gf5_test.h"
#include "../../kdebug/gf_command_wire.h"
#include "serial_diag_host.h"

static void __attribute__((noreturn)) quit(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(x) do { if (!(x)) quit(1); } while (0)
static struct gf5 ctx, other;
static struct gf5_test_ops ops;
static u32 finds, gets, sets, wiring;
static i32 find_rc, io_rc;
static bool swap_owner, absent, output;
static u8 expected_tag;
static i32 find(struct gf5 *g, u8 tag, u8 *line, u8 log[2])
{
    REQUIRE(g == &ctx && tag == expected_tag);
    ++finds;
    if (find_rc) return find_rc;
    *line = wiring ? 2 : 9;
    log[0] = wiring ? 1 : 2;
    log[1] = wiring ? 0 : 3;
    if (swap_owner) ops.ctx = &other;
    return 0;
}
static void check_wiring(struct gf5 *g, u8 line, const u8 log[2])
{
    REQUIRE(g == &ctx && finds == gets + sets + 1);
    REQUIRE(line == (wiring ? 2 : 9));
    REQUIRE(log[0] == (wiring ? 1 : 2) && log[1] == (wiring ? 0 : 3));
}
static i32 get(struct gf5 *g, u8 line, const u8 log[2], bool *value)
{
    check_wiring(g, line, log); ++gets; *value = true; return io_rc;
}
static i32 set(struct gf5 *g, u8 line, const u8 log[2], bool value)
{
    check_wiring(g, line, log); ++sets; output = value; return io_rc;
}
const struct gf5_test_ops *geforce_feature5_ops(void)
{
    return absent ? NULL : &ops;
}
static void arg(u32 index, u32 value)
{
    REQUIRE(!gf5_test(GF5T_ARG_LO | (index << GF5_WIRE_INDEX_SHIFT) |
                     ((value & GF5_WIRE_HALF_MASK) << GF5_WIRE_VALUE_SHIFT)));
    REQUIRE(!gf5_test(GF5T_ARG_HI | (index << GF5_WIRE_INDEX_SHIFT) |
                     ((value >> GF5_WIRE_VALUE_SHIFT) << GF5_WIRE_VALUE_SHIFT)));
}
static i32 request(u32 stage)
{
    host_serial_reset();
    return gf5_test(stage);
}
static void rejected(u32 stage, const char *rc)
{
    u32 before = finds + gets + sets;
    REQUIRE(request(stage) == 1);
    REQUIRE(host_serial_find(rc));
    REQUIRE(before == finds + gets + sets);
}
void _start(void)
{
    ctx.phase = GF5_RUN;
    ops.ctx = &ctx; ops.gpio_find = find; ops.gpio_get = get; ops.gpio_set = set;
    expected_tag = 9;
    arg(0, 9); arg(1, 1); arg(2, 0); arg(3, 0);
    /* Old SET and new SET can have exactly the same operand array. */
    rejected(GF5T_GPIO_SET, " rc=-501");
    rejected(GF5T_GPIO_GET, " rc=-501");
    arg(1, 0);
    rejected(GF5T_GPIO_GET, " rc=-501");
    arg(1, 1);
    REQUIRE(!request(GF5T_GPIO_SET | GF5_WIRE_GPIO_TAG) && output);
    REQUIRE(finds == 1 && sets == 1);
    REQUIRE(host_serial_find("tag=9 line=9 log0=2 log1=3 value=1"));
    wiring = 1;
    REQUIRE(!request(GF5T_GPIO_GET | GF5_WIRE_GPIO_TAG));
    REQUIRE(finds == 2 && gets == 1);
    REQUIRE(host_serial_find("tag=9 line=2 log0=1 log1=0 value=1"));
    arg(1, 0);
    REQUIRE(!request(GF5T_GPIO_SET | GF5_WIRE_GPIO_TAG) && !output);
    arg(0, 256);
    rejected(GF5T_GPIO_GET | GF5_WIRE_GPIO_TAG, " rc=-501");
    arg(0, 9); arg(1, 2);
    rejected(GF5T_GPIO_SET | GF5_WIRE_GPIO_TAG, " rc=-501");
    arg(1, 1);
    rejected(GF5T_GPIO_GET | GF5_WIRE_GPIO_TAG | (1U << 9), " rc=-501");
    rejected(GF5T_INFO | GF5_WIRE_GPIO_TAG, " rc=-501");
    find_rc = GF5_EBIOS;
    REQUIRE(request(GF5T_GPIO_SET | GF5_WIRE_GPIO_TAG) == 1);
    REQUIRE(host_serial_find(" rc=-503") && sets == 2 && gets == 1);
    find_rc = 0; finds = gets + sets;
    io_rc = GF5_EIO;
    REQUIRE(request(GF5T_GPIO_GET | GF5_WIRE_GPIO_TAG) == 1);
    REQUIRE(host_serial_find(" rc=-507"));
    io_rc = 0;
    swap_owner = true;
    REQUIRE(request(GF5T_GPIO_SET | GF5_WIRE_GPIO_TAG) == 1);
    REQUIRE(host_serial_find(" rc=-509") && sets == 2);
    swap_owner = false; ops.ctx = &ctx; finds = gets + sets;
    ops.gpio_find = NULL;
    REQUIRE(request(GF5T_GPIO_GET | GF5_WIRE_GPIO_TAG) == 77);
    REQUIRE(finds == gets + sets);
    ops.gpio_find = find;
    ctx.phase = GF5_SAVED;
    rejected(GF5T_GPIO_SET | GF5_WIRE_GPIO_TAG, " rc=-509");
    ctx.phase = GF5_RUN;
    host_serial_gate = 1;
    REQUIRE(!request(GF5T_GPIO_GET | GF5_WIRE_GPIO_TAG));
    REQUIRE(!host_serial_calls);
    quit(0);
}
