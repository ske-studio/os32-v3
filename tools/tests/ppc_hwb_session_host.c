/* PowerMac HW_OP §5 5a and R3-P1/P2. Exercise the private dispatch, exec
 * ownership and terminal delivery together, with the existing host AppSlot. */
#include "platform/powermac/platform_hwb_session.h"
#define _start pc98_abort_control
#include "exec_hwb_abort_host.c"
#undef _start
#define PM_KDEBUG
#include "kernel/module/kdebug_call.c"
#define PM_TERMINAL_TEST
#include "platform/powermac/terminal_input.c"

ShellService g_shell_service;
static unsigned forwarded;
static i32 forward(u32 op, void *buf, u32 size)
{
    (void)buf;
    (void)size;
    ++forwarded;
    return op;
}

void _start(void)
{
    u8 bytes[] = {'a', 3, 'z'};
    appslot_init();
    kdebug.ops.dbg_call = forward;
    HWB_TEST("private-session");
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, bytes, 1) == OS32_ERR_INVAL);
    CHECK(!resident_cui_hw_active());
    g_cur = APP_ID_MIN;
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, 0, 0) != 0);
    g_cur = APP_ID_SHELL;
    gui_mode = 1;
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, 0, 0) != 0);
    gui_mode = 0;
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, 0, 0) == 0);
    CHECK(resident_cui_hw_active());
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, 0, 0) != 0);
    pm_terminal_open();
    CHECK(pm_terminal_trygetkey() == RESIDENT_CLI_KEY_CANCEL);
    take_check(0);
    pm_terminal_feed(bytes, sizeof(bytes));
    CHECK(pm_terminal_trygetkey() == 'a' && pm_terminal_trygetkey() == 'z');
    HWB_TEST("take-clear");
    take_check(1);
    take_check(0);
    HWB_TEST("private-session");
    CHECK(kdebug_call(PLATFORM_HWB_LEAVE, 0, 0) == 0 && !resident_cui_hw_active());
    pm_terminal_feed(bytes+1, 1);
    CHECK(pm_terminal_trygetkey() == 3);
    take_check(0);
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, 0, 0) == 0);
    pm_terminal_feed(bytes+1, 1);
    exec_input_owner_exit(APP_ID_SHELL);
    CHECK(!resident_cui_hw_active());
    take_check(0);
    CHECK(forwarded == 0 && kdebug_call(HW_OP, 0, 0) == HW_OP && forwarded == 1);
    hwb_test_write("PASS PPC session dispatch, teardown and one-shot terminal stop\n");
    hwb_test_exit(0);
}
