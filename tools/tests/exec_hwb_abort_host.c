/* Policy transitions are exercised separately by test_gfx_policy_triggers. */
void gfx_policy_reeval(void) {}
/* HW_BASIC_DESIGN §3/F, N4/P3: actual exec TU and actual AppSlot table.
 * Only console mode, resource owner and CPU interrupt/MMU edges are faked. */
#include "hwb_fake.h"
static u32 host_cr3;
#include "io.h"
static unsigned int observed_save(void);
static void observed_restore(unsigned int flags);
#define irq_save observed_save
#define irq_restore observed_restore
#include "exec/exec.c"
#undef irq_save
#undef irq_restore
#include "exec/appslot.c"
#include "exec/ring3_str.c"
volatile u32 kctx_irq_depth, kctx_exc_depth;

volatile u32 tick_count;
void fd_redirect_clear_state(FdRedirectState *state)
{
    hwbf_zero(state, sizeof(*state));
}

static int gui_mode, owner;
static u32 display_returns;
void gf_display_owner_exit(int id)
{
    CHECK(id == APP_ID_SHELL && _irq_enabled());
    display_returns++;
}
static u32 saves, restores;
static int inject_after_restore;
static unsigned int observed_save(void)
{
    saves++;
    return irq_save();
}
static void observed_restore(unsigned int flags)
{
    CHECK(!_irq_enabled());
    restores++;
    irq_restore(flags);
    /* Model a newly delivered IRQ immediately after the atomic read/clear. */
    if (inject_after_restore && _irq_enabled()) {
        inject_after_restore = 0;
        ring3_abort_request();
    }
}
int con_sink_is_enabled(void) { return gui_mode; }
void res_owner_set(int id) { owner = id; }
int res_owner_get(void) { return owner; }

static void same_slots(const AppSlot *before)
{
    const u8 *a = (const u8 *)before, *b = (const u8 *)g_slot;
    for (u32 i = 0; i < sizeof(g_slot); i++) CHECK(a[i] == b[i]);
}
static void take_check(u32 expected)
{
    unsigned int before = host_arch_if;
    u32 nsave = saves, nrestore = restores;
    CHECK(exec_hwb_abort_take() == expected);
    CHECK(host_arch_if == before);
    CHECK(saves == nsave + 1 && restores == nrestore + 1);
}
static void test(void)
{
    AppSlot before[APP_SLOT_COUNT];
    appslot_init();
    HWB_TEST("cui-shell");
    CHECK(appslot_cur() == APP_ID_SHELL);
    for (u32 i = 0; i < APP_SLOT_COUNT; i++) {
        /* Nonzero guards also catch accidental clearing of existing requests. */
        g_slot[i].stop_wm_req = 1;
        g_slot[i].abort_req = 1;
    }
    for (u32 i = 0; i < APP_SLOT_COUNT; i++) before[i] = g_slot[i];
    take_check(0);
    ring3_abort_request(); ring3_abort_request();
    same_slots(before);
    HWB_TEST("take-clear");
    take_check(1); take_check(0);
    same_slots(before);
    HWB_TEST("if-disabled");
    _disable(); ring3_abort_request(); take_check(1); take_check(0);
    _enable();
    HWB_TEST("new-irq");
    ring3_abort_request(); inject_after_restore = 1;
    take_check(1); take_check(1); take_check(0);
    same_slots(before);

    /* Independent expectations from §3/F and existing GUI STOP contract. */
    HWB_TEST("display-return");
    g_cui_display_abort_pending = 0; display_returns = 0;
    exec_cui_display_abort(); CHECK(!display_returns);
    ring3_abort_request(); CHECK(!display_returns);
    kctx_irq_depth = 1;
    (void)ring3_wait_pending(); CHECK(!display_returns);
    kctx_irq_depth = 0;
    (void)ring3_wait_pending(); CHECK(display_returns == 1);
    exec_cui_display_abort(); CHECK(display_returns == 1);
    take_check(1); take_check(0); /* independent HW stop request survives */
    same_slots(before);

    HWB_TEST("legacy-routes");
    for (int mode = 0; mode <= 1; mode++) {
        for (int kind = 0; kind < 3; kind++) {
            appslot_init(); gui_mode = mode;
            g_cur = APP_ID_MIN;
            AppSlot *a = appslot_at(g_cur);
            a->state = APP_STATE_RUNNING; a->cpl3 = 1; a->gui = mode;
            tick_count = 0; a->last_kernel_tick = 0;
            if (kind == 1) a->in_op_wait = 1;
            if (kind == 2) tick_count = APP_RUNAWAY_TICKS;
            ring3_abort_request();
            CHECK(a->abort_req == (!mode || kind != 0));
            CHECK(a->stop_wm_req == (mode && kind == 0));
            take_check(0);
        }
    }
    HWB_TEST("gui-shell");
    appslot_init(); gui_mode = 1;
    for (u32 i = 0; i < APP_SLOT_COUNT; i++) before[i] = g_slot[i];
    ring3_abort_request(); take_check(0); same_slots(before);
    hwb_test_write("PASS exec HW abort: CUI shell, take/IF, GUI/app controls\n");
}
void _start(void) { test(); hwb_test_exit(0); }
