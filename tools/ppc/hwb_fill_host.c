/* HWOP_COMMON_DESIGN §3.5/§3.7: reject the entire physical fill before
 * the first write. Use the shell parser, executor and PowerMac wire sender. */
#include "tools/tests/hwb_fake.h"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_log.c"
#include "userland/shell/hwb_exec.c"

static struct hwb_fake bus;
static struct hwb_state shell;
static unsigned checks;

static int range_call(u32 op, void *buf, u32 size)
{
    struct hw_range_req *r = buf;
    CHECK(op == HW_OP && size == sizeof(*r));
    CHECK(r->req.action == HW_PHYS_CHECK && r->req.version == HW_VERSION);
    CHECK(r->req.space == HW_PHYS && r->req.width == 2 && r->req.addr == 0x1000);
    CHECK(!r->req.bdf && !r->req.value && !r->req.status && !r->bar && !r->offset);
    ++checks;
    r->req.status = r->bytes <= 4 ? HW_OK : HW_WINDOW;
    return 0;
}

void _start(void)
{
    static struct fill_api api = {range_call};
    g_api = &api;
    hwbf_init(&bus);
    hwb_init(&shell, &bus.backend);
    const char fits[] = "mmiofill16 phys 0x1000 2 0x1234";
    const char over[] = "mmiofill16 phys 0x1000 3 0x1234";
    HWB_TEST("fill-last-element");
    enum hwb_rc rc = hwb_command(&shell, over, sizeof(over)-1);
    CHECK(!bus.bus_accesses && rc == HWB_WINDOW && checks == 1);
    CHECK(hwb_command(&shell, fits, sizeof(fits)-1) == HWB_OK);
    CHECK(bus.bus_accesses == 2 && checks == 2);
    hwb_test_exit(0);
}
