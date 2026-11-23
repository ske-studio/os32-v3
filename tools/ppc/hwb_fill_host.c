/* HWOP_COMMON_DESIGN §3.5/§3.7: reject the entire physical fill before
 * the first write. Use the shell parser, executor and PowerMac wire sender. */
#include "tools/tests/hwb_fake.h"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_log.c"
#include "userland/shell/hwb_exec.c"

static struct hwb_fake bus;
static struct hwb_state shell;
int fill_dispatch(u32 op, void *buf, u32 size);
void setup_fill_window(void);
unsigned fill_stores(void);
int fill_contents_ok(void);

static int fill_access(void *ctx, struct hw_req *req)
{
    (void)ctx;
    return fill_dispatch(HW_OP, req, sizeof(*req));
}

void test_fill(void)
{
    static struct fill_api api = {fill_dispatch};
    g_api = &api;
    hwbf_init(&bus);
    setup_fill_window();
    bus.backend.access = fill_access;
    hwb_init(&shell, &bus.backend);
    const char fits[] = "mmiofill16 phys 0x1000 2 0x1234";
    const char over[] = "mmiofill16 phys 0x1000 3 0x1234";
    HWB_TEST("fill-last-element");
    enum hwb_rc rc = hwb_command(&shell, over, sizeof(over)-1);
    CHECK(fill_stores() == 0 && rc == HWB_WINDOW);
    CHECK(hwb_command(&shell, fits, sizeof(fits)-1) == HWB_OK);
    CHECK(fill_stores() == 2 && fill_contents_ok());
}
