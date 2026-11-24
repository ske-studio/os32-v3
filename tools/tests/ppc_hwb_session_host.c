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

static void *landing[5];
void exec_longjmp(k_jmpbuf buf)
{
    (void)buf;
    __builtin_longjmp(landing, 1);
}

/* A resident CUI stop is cooperative. Model the shell exiting after the
 * request, including an exit before the LAN stop has been consumed. */
static void shell_exit(void)
{
    if (!__builtin_setjmp(landing)) kapi_sys_exit(0);
    CHECK(g_longjmp_reason == EXEC_LJ_EXIT && g_last_kind == EXEC_KIND_EXITED);
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
    HWB_TEST("fault-exit");
    kctx_exc_depth = 1;
    if (!__builtin_setjmp(landing)) exec_fault_recover();
    kctx_exc_depth = 0;
    CHECK(g_longjmp_reason == EXEC_LJ_PENDING);
    exec_pending_finish();
    CHECK(g_last_kind == EXEC_KIND_FAULT);
    CHECK(!resident_cui_hw_active());
    appslot_shell_commit();
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, 0, 0) == 0);

    HWB_TEST("ctrl-stop-exit");
    ring3_abort_request();
    take_check(1);
    shell_exit();
    CHECK(!resident_cui_hw_active());
    appslot_shell_commit();
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, 0, 0) == 0);

    HWB_TEST("terminal-exit");
    pm_terminal_feed(bytes+1, 1);
    shell_exit();
    CHECK(!resident_cui_hw_active());
    take_check(0);
    appslot_shell_commit();
    CHECK(kdebug_call(PLATFORM_HWB_ENTER, 0, 0) == 0);
    CHECK(forwarded == 0 && kdebug_call(HW_OP, 0, 0) == HW_OP && forwarded == 1);
    hwb_test_write("PASS PPC session dispatch, teardown and one-shot terminal stop\n");
    hwb_test_exit(0);
}

/* Other shell resources have no allocations in this fixture. */
#define EMPTY_OWNER(name) void name(int id) { (void)id; }
EMPTY_OWNER(install_reclaim_owner)
EMPTY_OWNER(io_kapi_owner_exit)
EMPTY_OWNER(kapi_strings_cleanup_owned)
EMPTY_OWNER(db_cleanup_owned)
EMPTY_OWNER(fd_redirect_reset_owned)
EMPTY_OWNER(font_lease_close_owned)
EMPTY_OWNER(vfs_close_owned)
EMPTY_OWNER(pipe_free_owned)
EMPTY_OWNER(shm_free_owned)
EMPTY_OWNER(snd2_reclaim)
EMPTY_OWNER(snd2_chip_reclaim)
EMPTY_OWNER(in2_reclaim)
EMPTY_OWNER(ime_owner_exit)
EMPTY_OWNER(kbd_inject_owner_exit)
EMPTY_OWNER(launch_owner_exit)
EMPTY_OWNER(host_owner_exit)
EMPTY_OWNER(con_sink_owner_exit)
EMPTY_OWNER(con_sink_push_exit)
void shell_service_retire(void) {}
u32 io_syscall_suspend(int id) { (void)id; return 0; }
void gui_owner_exit(int id, int kind) { (void)id; (void)kind; }
void av_focus_exec_leave(int id, int parent) { (void)id; (void)parent; }
int gfx_return_emergency(u32 id, int kind) { (void)id; (void)kind; return 0; }
int con_sink_reader_get(void) { return 0; }
i32 launch_child(i32 id) { (void)id; return 0; }
void kbd_inject_discard(void) {}
void input_discard_stop(void) {}
void __cdecl console_printf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

/* Only the master address space exists for a resident shell. */
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 pd) { CHECK(pd == paging_kernel_pd_phys()); }
void serial_puts_polled(const char *s) { (void)s; }
u32 ledger_owner_pages(u32 owner) { (void)owner; CHECK(0); return 0; }
int lease_revoke_all(struct addrspace *as) { (void)as; CHECK(0); return 0; }
int gfx2_owner_release(struct addrspace *as) { (void)as; CHECK(0); return 0; }
void shlib_addrspace_detach(struct addrspace *as) { (void)as; CHECK(0); }
u32 paging_addrspace_free_user_range(struct addrspace *as, u32 b, u32 e) { (void)as; (void)b; (void)e; CHECK(0); return 0; }
void paging_addrspace_poison(struct addrspace *as) { (void)as; CHECK(0); }
void paging_addrspace_destroy(struct addrspace *as) { (void)as; CHECK(0); }
int ledger_reclaim_owner(u32 owner, u32 *pages) { (void)owner; (void)pages; CHECK(0); return 0; }
int ledger_owner_retire(u32 owner) { (void)owner; CHECK(0); return 0; }
void kfree(void *p) { (void)p; CHECK(0); }
void exec_heap_restore_state(u32 b, u32 n, u32 used) { (void)b; (void)n; (void)used; CHECK(0); }

volatile u32 ring3_wm_shell_gen, font_guard_active;
void caller_access_invalidate(void) {}
void font_guard_fault(u32 a, u32 b, u32 c) { (void)a; (void)b; (void)c; hwb_test_exit(1); }
int legacy_closing(void) { return 0; }
void legacy_abort_begin(void) {}
void legacy_end(void) {}
void legacy_cleanup(void) {}
