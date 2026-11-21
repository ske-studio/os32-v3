/* Paired API: exercise the production full-slot validator and private boot relay. */
#include "kernel/ime/fep_module.c"
static KbdPolicySnapshot foreground = {.owner=1, .exec_gen=7, .command_gen=3};
int input_context_snapshot(KbdPolicySnapshot *out) { *out = foreground; return 0; }
#include "input/hal/input_ime_gate.c"
static int cleanup_saw_closed;
static int cleanup_saw_pending;
int module_abort(struct module_handle h)
{
    KbdPolicyImeSnapshot state;
    (void)h;
    cleanup_saw_pending = input_ime_cancel_take(&foreground) == 0;
    input_ime_snapshot(&state);
    cleanup_saw_closed = !fep_services_public() && !state.active && !state.owner;
    return 0;
}
static FepStore store;
FepStore *ime_current_store(void) { return &store; }
u32 fep_store_live_connections(const FepStore *s)
{ return !!s->system.group.generation + !!s->user.group.generation; }
u32 body_boot_connections(void);
int services_public;
enum services_stage services_stage;
volatile u32 services_boot_fault;
static int reject_slot;
static int exported;
const u32 *module_exports(struct module_handle h)
{ (void)h; return exported ? (const u32 *)1 : 0; }
static u32 live;
static u32 mock_connections(void) { return live; }
static int mock_init(FepModuleApi *out)
{
    u32 entries[FEP_MODULE_API_SLOTS];
    for (u32 i = 0; i < FEP_MODULE_API_SLOTS; i++) entries[i] = (u32)(uptr)mock_connections;
    if (reject_slot >= 0) entries[reject_slot] = 0;
    kmemcpy(out, entries, sizeof(entries));
    return 0;
}
static int test(void)
{
    struct module_record record = {.text = (u32)(uptr)mock_connections, .text_size = 1};
    if (ime_boot_connections()) return 1;
    for (int i = 0; i < (int)FEP_MODULE_API_SLOTS; i++) {
        candidate_valid = 0; reject_slot = i;
        if (fep_start(&record, (u32)(uptr)mock_init, 0) != MODULE_INIT_FAILED ||
            candidate_valid || ime_boot_connections()) return 2;
    }
    reject_slot = -1;
    if (fep_start(&record, (u32)(uptr)mock_init, 0) || !candidate_valid) return 3;
    /* services_public and module_exports are still false at boot_check time. */
    for (live = 0; live <= 2; live++)
        if (ime_boot_connections() != live) return 4;
    services_public = exported = 1;
    if (ime_boot_connections() != live) return 5;
    store.system.group.generation = 0;
    if (body_boot_connections()) return 6;
    store.system.group.generation = 1;
    if (body_boot_connections() != 1) return 7;
    store.system.isolated = 1; /* Failed close still owns its connection. */
    if (body_boot_connections() != 1) return 8;
    store.user.group.generation = 2;
    store.user_enabled = 1;
    if (body_boot_connections() != 2) return 9;
    store.user_enabled = 0; /* Retained isolated group still counts. */
    if (body_boot_connections() != 2) return 10;
    kbd_policy_ime_state(1, foreground.owner, foreground.command_gen);
    KbdPolicyImeSnapshot state;
    input_ime_snapshot(&state);
    if (input_ime_action_capture(1, &foreground, 0)) return 14;
    if (input_ime_cancel_capture(&state) || fep_module_abort() || !cleanup_saw_closed)
        return 11;
    if (input_ime_action_due(0)) return 14;
    if (cleanup_saw_pending) return 13;
    /* Simulate the next publication with exactly the same owner and generation. */
    fep_closed = 0;
    kbd_policy_ime_state(1, foreground.owner, foreground.command_gen);
    if (!input_ime_cancel_take(&foreground)) return 12;
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
