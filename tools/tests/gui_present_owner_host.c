/* The display copies one shadow pixel synchronously. Slot and WM dispatch are real. */
#include "appslot.h"
#include "gui.h"
#include "gfx.h"
#include "gfx_hal.h"
#include "fd_redirect.h"
#include "gfx2_bridge_fake.h"
#include "font_guard_inactive.h"

static u8 shadow, display;
u8 *bb_b = &shadow;
GfxCounters gfx_counters;
static void transfer(int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
    display = shadow;
}
static GfxBackend backend = {.present_rect = transfer};
GfxBackend *g_backend = &backend;
void gfx_diag_first_present(u32 before) { (void)before; }
void gfx_present_diag(void) {}
void gfx_shutdown(void) {}
static int owner;
int res_owner_get(void) { return owner; }
void res_owner_set(int id) { owner = id; }
void fd_redirect_clear_state(FdRedirectState *s) { (void)s; }
void fd_redirect_save(FdRedirectState *s) { (void)s; }
void fd_redirect_restore(const FdRedirectState *s) { (void)s; }
void input_set_gui_mode(int on) { (void)on; }
void ime_set_render(void *render) { (void)render; }
int serial_gate_active(void) { return 1; }
void serial_puts_polled(const char *s) { (void)s; }
int ring3_call_from_user(void) { return appslot_cur() != APP_ID_SHELL; }
i32 exec_trim_done(u32 epoch) { (void)epoch; return 0; }
static int ring3_wm_depth, ring3_wm_fault_count, fault_kill_count;
void ring3_wm_enter(void) { ++ring3_wm_depth; }
void ring3_wm_leave(void) { --ring3_wm_depth; }
static int kctx_irq_depth, kctx_exc_depth;
static void *landing[5];
static void blocked(void);
static void exec_pending_transfer(int kind) { (void)kind; __builtin_longjmp(landing, 1); }
static void exec_exit(int status, int kind) { (void)status; exec_pending_transfer(kind); }
#include "exec_fault.inc"

static void require(int ok, int line)
{
    if (!ok) {
        __asm__ volatile("int $0x80" :: "a"(1), "b"(line) : "memory");
        __builtin_unreachable();
    }
}
#define CHECK(test) require((test), __LINE__)

static int window_owner;
static i32 handler(u32 op, u32 arg, int child)
{
    if (op == GUI_OP_OWNER_EXIT) {
        if (child == window_owner) window_owner = 0;
        return 0;
    }
    CHECK(child == 2 && appslot_cur() == child && res_owner_get() == child);
    if (op == GUI_OP_WAIT) {
        CHECK(appslot_park_check() == 0);
        appslot_park_commit();
        __builtin_longjmp(landing, 1);
    }
    if (arg == 1) ring3_fault_kill();
    if (arg == 3) {
        appslot_switch_to(3);
        blocked();
        appslot_switch_to(child);
    }
    if (arg == 2) {
        /* Replace the resident shell while the old callback is suspended. */
        appslot_shell_commit();
        CHECK(gui_register(handler, 0) == 0);
        appslot_switch_to(child);
    }
    window_owner = child;
    gfx_present_rect(0, 0, 1, 1);
    return 0;
}

static void blocked(void)
{
    shadow = display + 1;
    gfx_present_rect(0, 0, 1, 1);
    CHECK(display != shadow);
}

void _start(void)
{
    appslot_init();
    CHECK(gui_register(handler, 0) == 0);
    appslot_start_commit(2, 1, 0);
    blocked();
    CHECK(gui_call(GUI_OP_COMMIT, 0) == 0 && display == shadow && window_owner == 2);
    blocked();
    if (!__builtin_setjmp(landing)) gui_call(GUI_OP_WAIT, 0);
    CHECK(appslot_cur() == 1);
    appslot_start_commit(3, 1, 0);
    blocked();
    appslot_resume_commit(2);
    blocked();
    CHECK(gui_call(GUI_OP_COMMIT, 3) == 0 && display == shadow);
    if (!__builtin_setjmp(landing)) gui_call(GUI_OP_COMMIT, 1);
    CHECK(fault_kill_count == 1);
    blocked();
    CHECK(gui_call(GUI_OP_COMMIT, 2) == 0 && display != shadow);
    blocked();
    gui_owner_exit(2, EXEC_KIND_EXITED);
    CHECK(window_owner == 0);
    gui_owner_exit(1, EXEC_KIND_EXITED);
    blocked();
    __asm__ volatile("int $0x80" :: "a"(1), "b"(0) : "memory");
    __builtin_unreachable();
}
