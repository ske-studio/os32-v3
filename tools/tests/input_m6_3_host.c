/* Real AppSlot, M5/M6 and exec adapters; hardware and policy are observable fakes. */
#include "hwb_fake.h"
#include "io.h"
#include "shell_service.h"
static void boundary_restore(unsigned int flags);
#define irq_restore boundary_restore
#include "appslot_copy.c"
#include "context.c"
#include "interrupt.c"
#undef irq_restore
volatile u32 kctx_irq_depth, kctx_exc_depth;
ShellService g_shell_service;
static int owner, returns, policy_calls, injection, injected, gui_mode;
void res_owner_set(int id) { owner = id; }
int res_owner_get(void) { return owner; }
int con_sink_is_enabled(void) { return gui_mode; }
static void init_attempt(void)
{
#include "m6_gfx_acquire.inc"
}
void fd_redirect_clear_state(FdRedirectState *s) { __builtin_memset(s, 0, sizeof(*s)); }
void gfx_policy_reeval(void) { CHECK(_irq_enabled()); ++policy_calls; }
void gf_display_owner_exit(int id)
{
    u32 gen;
    CHECK(_irq_enabled() && !kctx_irq_depth && !kctx_exc_depth);
    CHECK(appslot_gfx_resource_snapshot(id, &gen) == -1);
    ++returns;
}
static void boundary_restore(unsigned int flags)
{
    irq_restore(flags);
    if (injection && _irq_enabled()) {
        injection = 0; ++injected;
        /* IRQ after snapshot/drain copy, or after the atomic reservation. */
        kctx_irq_depth = 1;
        CHECK(input_display_restore_capture(owner) == 0);
        kctx_irq_depth = 0;
    }
}
static u32 g_hwb_abort_pending;
#include "exec_adapter.inc"
static u32 generation(int id)
{
    u32 gen = 0;
    CHECK(!appslot_gfx_resource_snapshot(id, &gen) && gen);
    return gen;
}
static void claim(int id)
{
    res_owner_set(id);
    AppSlot *a = appslot_get(id);
    a->cpl3 = 1; a->hdr_flags = OS32X_FLAG_GFX;
    CHECK(!appslot_gfx_claim(1));
}
static void test(void)
{
    u32 a, b, reason = 0;
    appslot_init(); input_interrupt_init();
    g_slot[2].state = g_slot[3].state = APP_STATE_RUNNING;
    HWB_TEST("1 same owner reacquire");
    claim(2); a = generation(2); claim(2); b = generation(2);
    CHECK(a != b && !appslot_gfx_owner_exit_if(2, a));
    CHECK(generation(2) == b && !returns);
    HWB_TEST("2 different owner and ID reuse");
    claim(3); CHECK(!appslot_gfx_owner_exit_if(2, b));
    appslot_gfx_owner_exit(3); claim(2);
    CHECK(generation(2) != b && !appslot_gfx_owner_exit_if(2, b));
    HWB_TEST("3 zero and wrap");
    CHECK(!appslot_gfx_owner_exit_if(2, 0));
    next_resource_gen = 0xffffffffUL; claim(2); CHECK(generation(2) == 1);
    appslot_gfx_owner_exit(2); CHECK(next_resource_gen == 1);
    CHECK(appslot_gfx_resource_snapshot(2, &a) == -1);
    HWB_TEST("4 exit/drain order, BUSY and duplicate");
    claim(2); CHECK(!input_display_restore_capture(2));
    CHECK(!input_display_restore_capture(2));
    appslot_gfx_owner_exit(2); int n = returns;
    CHECK(!input_display_restore_drain() && !input_display_restore_drain() && returns == n);
    claim(2); CHECK(!input_display_restore_capture(2));
    claim(3); CHECK(input_display_restore_capture(3) == -1);
    CHECK(!input_display_restore_drain() && generation(3));
    CHECK(!input_display_restore_capture(3)); n = returns;
    CHECK(!input_display_restore_drain() && returns == n + 1);
    CHECK(!input_display_restore_drain() && returns == n + 1);
    appslot_gfx_owner_exit(3); /* unconditional exit still reports/retries cleanup */
    HWB_TEST("5 unsafe and IRQ boundaries");
    claim(2); injection = 1;
    CHECK(!input_display_restore_capture(2) && injected == 1);
    n = returns;
    _disable(); CHECK(input_display_restore_drain() == -1 && !_irq_enabled());
    CHECK(appslot_gfx_owner_exit_if(2, generation(2)) == -1); _enable();
    kctx_irq_depth = 1; CHECK(input_display_restore_drain() == -1); kctx_irq_depth = 0;
    kctx_exc_depth = 1; CHECK(input_display_restore_drain() == -1); kctx_exc_depth = 0;
    CHECK(returns == n && display_restore_pending.resource_gen);
    injection = 1; CHECK(!input_display_restore_drain() && injected == 2);
    claim(2); a = generation(2); injection = 1;
    CHECK(appslot_gfx_owner_exit_if(2, a) == 1 && injected == 3);
    CHECK(!display_restore_pending.resource_gen);
    HWB_TEST("6 shell ownership interval and command end");
    res_owner_set(APP_ID_SHELL); appslot_gfx_resource_acquire(APP_ID_SHELL);
    a = generation(APP_ID_SHELL);
    KbdPolicyContext c; input_context_init(&c);
    CHECK(!input_context_command_begin(&c));
    CHECK(!input_display_restore_capture(APP_ID_SHELL));
    CHECK(display_restore_pending.resource_gen == a);
    CHECK(!input_context_command_end(&c));
    CHECK(!input_display_restore_drain());
    appslot_gfx_resource_acquire(APP_ID_SHELL); b = generation(APP_ID_SHELL);
    CHECK(b != a && !appslot_gfx_owner_exit_if(APP_ID_SHELL, a));
    HWB_TEST("7 exec exit while command active");
    CHECK(!input_context_first_shell(17));
    CHECK(!input_context_owner_begin(1, 17));
    KbdPolicySnapshot s; CHECK(!input_context_snapshot(&s));
    CHECK(!input_interrupt_capture(&s, 7));
    g_shell_service.generation = 17; g_hwb_abort_pending = 1;
    exec_input_owner_exit(1);
    CHECK(!exec_hwb_abort_take() && input_interrupt_take(&s, &reason) == -1);
    CHECK(!input_context_first_shell(18));
    HWB_TEST("8 unrelated exit and absent AS");
    s = (KbdPolicySnapshot){1,18,2,INPUT_CONTEXT_RUNNING,INPUT_CONTEXT_ISIG};
    CHECK(!input_interrupt_capture(&s, 9));
    CHECK(!input_display_restore_capture(1));
    g_hwb_abort_pending = 1; exec_input_owner_exit(2);
    CHECK(g_hwb_abort_pending && display_restore_pending.resource_gen == b);
    CHECK(!input_interrupt_take(&s, &reason) && reason == 9);
    struct addrspace as2 = {0}; as2.generation = 23;
    g_slot[2].as = &as2; g_slot[2].cpl3 = 1;
    CHECK(!input_interrupt_capture(&s, 10)); exec_input_owner_exit(2);
    CHECK(!input_interrupt_take(&s, &reason) && reason == 10);
    CHECK(g_hwb_abort_pending && display_restore_pending.resource_gen == b);
    CHECK(!input_interrupt_capture(&s, 11)); input_interrupt_exec_exit(1, 19);
    CHECK(!input_interrupt_take(&s, &reason) && reason == 11);
    CHECK(!input_display_restore_drain());
    HWB_TEST("9 legacy GUI WM and claim rejection");
    appslot_init(); res_owner_set(1); a = next_resource_gen;
    CHECK(!appslot_gfx_claim(1) && appslot_gfx_owner() == GFX_OWNER_WM);
    CHECK(next_resource_gen == a && appslot_gfx_resource_snapshot(1, &b) == -1);
    g_slot[2].state = APP_STATE_RUNNING;
    g_cur = 2; res_owner_set(2); AppSlot *slot = appslot_get(2); slot->cpl3 = 1; slot->hdr_flags = 0;
    CHECK(appslot_gfx_claim(1) == OS32_ERR_INVAL && slot->abort_req);
    CHECK(appslot_gfx_owner() == GFX_OWNER_WM && next_resource_gen == a);
    slot->cpl3 = 0; CHECK(!appslot_gfx_claim(0) && next_resource_gen == a);
    _disable(); res_owner_set(1); gui_mode = 1; init_attempt();
    CHECK(next_resource_gen == a && appslot_gfx_resource_snapshot(1, &b) == -1);
    gui_mode = 0; res_owner_set(0); init_attempt(); CHECK(next_resource_gen == a);
    res_owner_set(2); init_attempt(); CHECK(next_resource_gen == a);
    res_owner_set(1); init_attempt(); CHECK(next_resource_gen == a && !_irq_enabled());
    _enable(); init_attempt(); CHECK(next_resource_gen != a && generation(1));
    CHECK(appslot_gfx_owner() == GFX_OWNER_WM);
    CHECK(policy_calls && returns);
}
void _start(void) { test(); hwb_test_write("PASS M6[3] resources/exec 1-9\n"); hwb_test_exit(0); }
