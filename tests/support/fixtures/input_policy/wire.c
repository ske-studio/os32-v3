#include "input_policy.h"
#include "os32_kapi_shared.h"
#include "sys/kapi/input_policy_provider.h"
#include "appslot.h"
#include "io.h"

unsigned input_test_irq;
static int exclusive, caller = 1;
static AppSlot appslots[APP_SLOT_COUNT];
int res_owner_get(void) { return caller; }
AppSlot *appslot_get(int id) { return id == 1 || id == 2 ? &appslots[id] : 0; }
int av_focus_is_wm(void) { return caller == 1; }
int ring3_call_from_user(void) { return caller == 2; }
int input_display_restore_lock(unsigned *flags) { *flags = irq_save(); return 0; }
static void discard(void) {}

int in2_excl_keyboard(void) { return exclusive; }
static int pressed(void) { return 0; }

static int test_provider(void)
{
    KbdPolicySnapshot snapshot;
    KbdPolicyAction action = {7, 0, 123, 4}, out = {0};
    KbdPolicyKey key = {.keycode = 37}, key_out = {0};
    u8 base[] = {'I','K','M','1',2,0,16,0,1,0,0,0,0,0,0,0,
                 2,0,2,0,37,0,0,0,65,0,0,0,0,0,0,0};
    u8 readback[sizeof base];
    u32 needed;
    if (input_provider_set(base, sizeof base) != OS32_ERR_NOSYS ||
        input_provider_get(readback, sizeof base, &needed) != OS32_ERR_NOSYS ||
        input_provider_reset() != OS32_ERR_NOSYS ||
        input_provider_context_set(INPUT_CONTEXT_RUNNING, INPUT_CONTEXT_ISIG) != OS32_ERR_NOSYS ||
        input_provider_command_begin() != OS32_ERR_NOSYS || input_provider_command_end() != OS32_ERR_NOSYS ||
        input_provider_delivery_set(0) != OS32_ERR_NOSYS ||
        input_provider_action_read(&out, 1) != OS32_ERR_NOSYS ||
        input_provider_key_read(&key_out, 1) != OS32_ERR_NOSYS) return 2;
#ifdef __PPC__
    if (ikm_policy_init(IKM_PLATFORM_MAC, 0, &hid_jp_backend, pressed)) return 3;
#else
    if (ikm_policy_init(IKM_PLATFORM_PC98, &pc98_jp_backend, &hid_jp_backend, pressed)) return 3;
#endif
    appslots[1].exec_gen = 1;
    appslots[2].exec_gen = 2;
    input_context_discard_register(discard);
    if (input_context_owner_register(1, 1, 0, 0) || input_context_owner_select(1, 1) ||
        input_context_owner_register(2, 2, 1, 1) || input_context_snapshot(&snapshot))
        return 4;
    if (input_provider_context_set(INPUT_CONTEXT_RUNNING, INPUT_CONTEXT_ISIG) ||
        input_provider_command_begin() || input_provider_command_end()) return 13;
    if (ikm_policy_set(base, sizeof base, 0, 0) || input_provider_set(base, sizeof base) ||
        input_provider_reset()) return 5;
    caller = 2;
    if (input_provider_reset() != OS32_ERR_BUSY || input_provider_command_begin() != OS32_ERR_BUSY ||
        input_provider_command_end() != OS32_ERR_BUSY) return 5;
    caller = 1;
    ikm_policy_owner_exit(1, 2);
    if (input_provider_get(readback, sizeof base, &needed) || needed != 16) return 6;
    ikm_policy_owner_exit(1, 1);
    if (input_provider_get(readback, sizeof base, &needed) || needed != sizeof base) return 7;
    for (unsigned i = 0; i < needed; ++i)
        if (readback[i] != base[i]) return 8;
    key.map_generation = ikm_policy_generation() - 1;
    if (input_delivery_key(&snapshot, &key)) return 14;
    key.map_generation = ikm_policy_generation();
    if (input_provider_delivery_set(1) || !input_delivery_action(&snapshot, &action) ||
        !input_delivery_key(&snapshot, &key)) return 9;
    exclusive = 1;
    if (input_provider_action_read(0, 0) != OS32_ERR_BUSY ||
        ikm_policy_key_read(2, 2, 0, &key_out, 1) != OS32_ERR_INVAL) return 10;
    exclusive = 0;
    if (ikm_policy_action_read(1, 2, &out, 1) != OS32_ERR_STALE ||
        input_provider_action_read(&out, 1) != 1 || out.action != action.action ||
        input_provider_key_read(&key_out, 1) != 1 || key_out.keycode != key.keycode)
        return 11;
    if (input_context_owner_select(2, 2) || input_delivery_action(&snapshot, &action))
        return 12;
    return 0;
}

int probe(void)
{
    const u8 wire[] = {'I','K','M','1',2,0,16,0,1,0,0,0,0,0,0,0,
                       8,2,1,0,0x34,0x12,0xcd,0xab,4,3,2,1,0x78,0x56,0x34,0x12};
    const u8 rust_wire[] = {RUST_WIRE};
    for (unsigned i = 0; i < sizeof wire; ++i)
        if (wire[i] != rust_wire[i]) return 1;
    IkmRow row;
    u32 count;
    if (ikm_wire_header(wire, sizeof wire, &count) || count != 1 ||
        ikm_wire_row(wire, sizeof wire, 0, &row)) return 1;
    if (row.kind != 8 || row.profile != 2 || row.context != 1 || row.flags ||
           row.index != 0x1234 || row.selector != 0xabcd ||
           row.value != 0x01020304 || row.aux != 0x12345678) return 1;
    return test_provider();
}
