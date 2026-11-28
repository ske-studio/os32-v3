#include "input_policy.h"
#include "os32_kapi_shared.h"
#include "policy_table.h"
#include "os32_kapi_slots.h"
#include "appslot.h"
#include "io.h"
#include "in2_int.h"
#include "kbd_contract.h"

unsigned input_test_irq;
static int exclusive, caller = 1, user, deny_read;
static KernelAPI api;
static uptr fault_jump[5];
static u32 read_len, denied_write_addr;
static struct { u32 addr, len; } write_ranges[2];

int ring3_user_range_ok(u32 ptr, u32 len)
{
    (void)ptr;
    read_len = len;
    return !user || !deny_read;
}

int ring3_user_ranges_writable(u32 a, u32 la, u32 b, u32 lb)
{
    write_ranges[0].addr = a;
    write_ranges[0].len = la;
    write_ranges[1].addr = b;
    write_ranges[1].len = lb;
    return !user || ((!la || a != denied_write_addr) &&
                     (!lb || b != denied_write_addr));
}

void ring3_fault_kill(void)
{
    __builtin_longjmp(fault_jump, 1);
}

static AppSlot appslots[APP_SLOT_COUNT];
int res_owner_get(void) { return caller; }
AppSlot *appslot_get(int id) { return id == 1 || id == 2 ? &appslots[id] : 0; }
volatile int ring3_wm_depth;
static int pump_busy;
int ring3_gui_pump_busy(void) { return pump_busy; }
static int handler_owner;
int gui_registered(void) { return 1; }
int appslot_present_owner(void) { return handler_owner ? handler_owner : caller; }
int ring3_call_from_user(void) { return user; }
int input_display_restore_lock(unsigned *flags) { *flags = irq_save(); return 0; }
static void discard(void) {}

int in2_excl_keyboard(void) { return exclusive; }
static unsigned diagnostics;
#ifdef FAIL_INIT
int legacy_shell_dispatch(void);
int write_gui_raw(const int *raw);
int __wrap_ikm_policy_init(u8 platform, const KmBackend *pc98, const KmBackend *hid,
                           int (*pressed)(void))
{
    (void)platform; (void)pc98; (void)hid; (void)pressed;
    return OS32_ERR_INVAL;
}
#endif
volatile u32 tick_count;
void appslot_poll_yield_reset(void) {}
int input_key_pressed(u8 sc) { (void)sc; return 0; }
int in2_excl_drop_raw(u8 sc, int is_break) { (void)sc; (void)is_break; return 0; }
int input_policy_pressed(void) { return 0; }
#ifdef __PPC__
#include "input_usb.h"
#include "usb_platform.h"
int input_src_info(u8 src, u16 gen, u8 *kind, u8 *origin)
{ *kind = INPUT_KIND_KBD; *origin = INPUT_ORIGIN_USB; return src == 1 && gen == 1; }
int usb_input_alloc(int kind) { (void)kind; return 1; }
void usbp_log(const char *fmt, ...) { (void)fmt; ++diagnostics; }
#else
#include "sys/hal/input/in2_int.h"
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; ++diagnostics; }
int input_src_register(u8 kind, u8 origin, u16 *gen)
{ (void)kind; (void)origin; *gen = 1; return 1; }
void input_src_release(u8 src, u16 gen) { (void)src; (void)gen; }
int input_src_info(u8 src, u16 gen, u8 *kind, u8 *origin)
{ *kind = INPUT_KIND_KBD; *origin = INPUT_ORIGIN_NATIVE; return src == 1 && gen == 1; }
#endif

static int check_unavailable(u8 *base, unsigned size)
{
    u8 readback[32];
    u32 needed;
    KbdPolicyAction out;
    KbdPolicyKey key_out;
    if (api.kbd_policy_set(base, size) != OS32_ERR_NOSYS ||
        api.kbd_policy_get(readback, size, &needed) != OS32_ERR_NOSYS ||
        api.kbd_policy_reset() != OS32_ERR_NOSYS ||
        api.kbd_policy_context_set(INPUT_CONTEXT_RUNNING, INPUT_CONTEXT_ISIG) != OS32_ERR_NOSYS ||
        api.kbd_policy_command_begin() != OS32_ERR_NOSYS || api.kbd_policy_command_end() != OS32_ERR_NOSYS ||
        api.kbd_policy_delivery_set(0) != OS32_ERR_NOSYS ||
        api.kbd_policy_action_read(&out, 1) != OS32_ERR_NOSYS ||
        api.kbd_policy_key_read(&key_out, 1) != OS32_ERR_NOSYS) return 2;
    return 0;
}

static int check_guards(void)
{
    union { u32 needed; u8 bytes[32]; } buffer = {0};
    KbdPolicyAction action = {0};
    KbdPolicyKey key = {0};
    u32 needed = 0;
    user = 1;
    deny_read = 1;
    denied_write_addr = (u32)&action;
    read_len = write_ranges[0].len = 0;
    if (__builtin_setjmp(fault_jump)) return 1;
    if (api.kbd_policy_set(buffer.bytes, 17) != OS32_ERR_INVAL ||
        api.kbd_policy_set((void *)0xfffffff1UL, 16) != OS32_ERR_INVAL ||
        api.kbd_policy_get(buffer.bytes + 1, 16, &buffer.needed) != OS32_ERR_INVAL ||
        api.kbd_policy_get(buffer.bytes, 16, (u32 *)(buffer.bytes + 1)) != OS32_ERR_INVAL ||
        api.kbd_policy_get((void *)0xfffffff1UL, 16, &needed) != OS32_ERR_INVAL ||
        api.kbd_policy_action_read(NULL, 1) != OS32_ERR_INVAL ||
        api.kbd_policy_action_read((void *)0xfffffff4UL, 1) != OS32_ERR_INVAL ||
        api.kbd_policy_key_read(&key, KBD_POLICY_READ_MAX + 1) != OS32_ERR_INVAL ||
        read_len || write_ranges[0].len) return 1;
    if (!__builtin_setjmp(fault_jump)) {
        api.kbd_policy_set(buffer.bytes, sizeof buffer);
        return 1;
    }
    if (read_len != sizeof buffer) return 1;
    if (!__builtin_setjmp(fault_jump)) {
        api.kbd_policy_action_read(&action, 1);
        return 1;
    }
    if (write_ranges[0].addr != (u32)&action ||
        write_ranges[0].len != sizeof action) return 1;
    deny_read = 0;
    denied_write_addr = 0;
    if (api.kbd_policy_action_read((void *)1, 0) != OS32_ERR_NOSYS ||
        api.kbd_policy_key_read(NULL, 0) != OS32_ERR_NOSYS) return 1;
    user = 0;
    return api.kbd_policy_set(buffer.bytes, sizeof buffer) != OS32_ERR_NOSYS;
}

static int check_get_fault(int deny_wire)
{
    /* Static outputs retain their values across the simulated fault. */
    static u32 needed;
    static u8 wire[32];
    needed = 0x5a5a5a5a;
    for (unsigned i = 0; i < sizeof wire; ++i)
        wire[i] = 0x5a;
    user = 1;
    denied_write_addr = deny_wire ? (u32)wire : (u32)&needed;
    write_ranges[0].len = write_ranges[1].len = 0;
    if (!__builtin_setjmp(fault_jump)) {
        api.kbd_policy_get(wire, sizeof wire, &needed);
        return 1;
    }
    user = 0;
    denied_write_addr = 0;
    if (write_ranges[0].addr != (u32)&needed || write_ranges[0].len != sizeof needed ||
        write_ranges[1].addr != (u32)wire || write_ranges[1].len != sizeof wire ||
        needed != 0x5a5a5a5a) return 1;
    for (unsigned i = 0; i < sizeof wire; ++i)
        if (wire[i] != 0x5a) return 1;
    return 0;
}

static int check_rejected_reads(void)
{
    KbdPolicyAction action = {.action = 0x5a};
    KbdPolicyKey key = {.keycode = 0x5a};

    /* While exclusive, Action's target reports BUSY and a non-WM Key
     * target reports INVAL. The same ranges fault first on the user path. */
    exclusive = 1;
    denied_write_addr = (u32)&action;
    if (api.kbd_policy_action_read(&action, 1) != OS32_ERR_BUSY) return 1;
    user = 1;
    if (!__builtin_setjmp(fault_jump)) {
        api.kbd_policy_action_read(&action, 1);
        return 1;
    }
    denied_write_addr = (u32)&key;
    if (!__builtin_setjmp(fault_jump)) {
        api.kbd_policy_key_read(&key, 1);
        return 1;
    }
    exclusive = 0;
    caller = 2;
    denied_write_addr = (u32)&action;
    if (!__builtin_setjmp(fault_jump)) {
        api.kbd_policy_action_read(&action, 1);
        return 1;
    }
    user = 0;
    if (api.kbd_policy_action_read(&action, 1) != OS32_ERR_BUSY ||
        api.kbd_policy_key_read(&key, 1) != OS32_ERR_INVAL) return 1;
    caller = 1;
    denied_write_addr = 0;
    return action.action != 0x5a || key.keycode != 0x5a;
}

static int check_row_limit(int public)
{
    static u8 wire[16 + 1025 * 16] = {'I','K','M','1',2,0,16,0,0,4};
    u32 count;
    for (unsigned i = 0; i < 1024; ++i) {
        u8 *row = wire + 16 + i * 16;
        row[0] = 2;
        row[2] = 2;
        row[4] = i / 5;
        row[6] = i % 5;
        row[8] = 65;
    }
    wire[8] = 0;
    if (public) {
        if (api.kbd_policy_set(wire, sizeof wire - 16)) return 1;
        u32 generation = ikm_policy_generation();
        wire[8] = 1;
        return api.kbd_policy_set(wire, sizeof wire) != OS32_ERR_INVAL ||
               ikm_policy_generation() != generation;
    }
    if (ikm_policy_set(wire, sizeof wire - 16, 0, 0)) return 1;
    wire[8] = 1;
    return ikm_wire_header(wire, sizeof wire, &count) != OS32_ERR_INVAL;
}

static int test_provider(void)
{
    KbdPolicySnapshot snapshot;
    KbdPolicyAction action = {7, 0, 123, 4}, out = {0};
    KbdPolicyKey key = {.keycode = 37}, key_out = {0};
    u8 base[] = {'I','K','M','1',2,0,16,0,1,0,0,0,0,0,0,0,
                 2,0,2,0,37,0,0,0,65,0,0,0,0,0,0,0};
    u8 readback[sizeof base];
    u32 needed;
    bind_policy(&api);
    if (KAPI_VERSION != 92 || KAPI_SLOT_KBD_POLICY_SET != 340 ||
        KAPI_SLOT_KBD_POLICY_KEY_READ != 348 || check_guards() ||
        check_get_fault(0) || check_get_fault(1)) return 19;
    if (check_unavailable(base, sizeof base)) return 2;
    user = 1;
    if (check_unavailable(base, sizeof base)) return 2;
    user = 0;
#ifdef __PPC__
    pm_input_keymap_boot();
#else
    u16 generation;
    if (pc98_input_keymap_register(&generation) != 1) return 3;
#endif
#ifdef FAIL_INIT
#ifdef __PPC__
    if (usbp_input_src_alloc(USBP_INPUT_KBD) != 1) return 3;

    if (km_source_bind(1, 1, KM_MAP_HID)) return 3;
    const u16 keys[] = {4, 40};
#else
    const u16 keys[] = {0x1d, 0x1c};
#endif
    KmSource source;
    KmState state = {0};
    KmEvalResult result;
    if (km_source_lookup(1, 1, &source)) return 15;
    for (int gui = 1; gui >= 0; --gui) {
        kbd_set_gui_mode(gui);
        for (int i = 0; i < (gui ? 1 : 2); ++i) {
            if (km_eval(source.map, keys[i], &state, &result)) return 16;
            km_legacy_compat(&result);
            input_emit_keymap(&result, 1, 1, 0, 0);
        }
        if (gui) {
            int raw = legacy_raw_take();
            if (raw != (0x1d | KBD_RAW_MAKE) || write_gui_raw(&raw) != sizeof raw ||
                legacy_raw_take() != -1 ||
                legacy_cooked_take() != -1) return 17;
        } else if (!legacy_shell_dispatch() || legacy_raw_take() != -1) return 18;
    }
    return diagnostics != 1 || check_unavailable(base, sizeof base);
#endif
    if (diagnostics || check_row_limit(0)) return 3;
    appslots[1].exec_gen = 1;
    appslots[2].exec_gen = 2;
    input_context_discard_register(discard);
    if (input_context_owner_register(1, 1, 0, 0) || input_context_owner_select(1, 1) ||
        input_context_owner_register(2, 2, 1, 1) || input_context_snapshot(&snapshot))
        return 4;
    if (check_row_limit(1)) return 3;
    ikm_policy_owner_exit(1, 1);
    if (api.kbd_policy_context_set(INPUT_CONTEXT_RUNNING, INPUT_CONTEXT_ISIG) ||
        api.kbd_policy_command_begin() || api.kbd_policy_command_end()) return 13;
    if (ikm_policy_set(base, sizeof base, 0, 0) || api.kbd_policy_set(base, sizeof base) ||
        api.kbd_policy_reset()) return 5;
    if (check_get_fault(0) || check_get_fault(1)) return 19;
    if (api.kbd_policy_get(NULL, 0, &needed) != OS32_ERR_FULL || needed != 16) return 20;
    readback[0] = 0x5a;
    if (api.kbd_policy_get(readback, 1, &needed) != OS32_ERR_FULL ||
        needed != 16 || readback[0] != 0x5a) return 20;
    caller = 2;
    if (api.kbd_policy_reset() != OS32_ERR_BUSY || api.kbd_policy_command_begin() != OS32_ERR_BUSY ||
        api.kbd_policy_command_end() != OS32_ERR_BUSY) return 5;
    caller = 1;
    ikm_policy_owner_exit(1, 2);
    if (api.kbd_policy_get(readback, sizeof base, &needed) || needed != 16) return 6;
    ikm_policy_owner_exit(1, 1);
    if (api.kbd_policy_get(readback, sizeof base, &needed) || needed != sizeof base) return 7;
    for (unsigned i = 0; i < needed; ++i)
        if (readback[i] != base[i]) return 8;
    key.map_generation = ikm_policy_generation() - 1;
    if (input_delivery_key(&snapshot, &key)) return 14;
    key.map_generation = ikm_policy_generation();
    if (api.kbd_policy_delivery_set(1) || !input_delivery_action(&snapshot, &action) ||
        !input_delivery_key(&snapshot, &key)) return 9;
    if (check_rejected_reads()) return 21;
    exclusive = 1;
    caller = 2;
    user = 1;
    if (api.kbd_policy_key_read(&key_out, 1) != OS32_ERR_INVAL) return 10;
    caller = 1;
    if (api.kbd_policy_action_read(0, 0) != OS32_ERR_BUSY ||
        ikm_policy_key_read(2, 2, 0, &key_out, 1) != OS32_ERR_INVAL) return 10;
    exclusive = 0;
    if (api.kbd_policy_action_read(NULL, 0) ||
        api.kbd_policy_action_read(NULL, 1) != OS32_ERR_INVAL ||
        api.kbd_policy_key_read(NULL, 0) != OS32_ERR_INVAL) return 10;
    user = 0;
    if (api.kbd_policy_key_read(NULL, 0)) return 10;
    if (ikm_policy_action_read(1, 2, &out, 1) != OS32_ERR_STALE ||
        api.kbd_policy_action_read(&out, 1) != 1 || out.action != action.action ||
        out.command_gen != action.command_gen || out.time_ms != action.time_ms ||
        out.value != action.value || api.kbd_policy_key_read(&key_out, 1) != 1 ||
        key_out.keycode != key.keycode || key_out.map_generation != key.map_generation)
        return 11;
    caller = 2;
    pump_busy = 1;
    if (api.kbd_policy_key_read(NULL, 0) != OS32_ERR_INVAL) return 22;
    ring3_wm_depth = 1;
    if (!input_delivery_key(&snapshot, &key) || api.kbd_policy_key_read(&key_out, 1) != 1)
        return 23;
    pump_busy = 0;
    if (api.kbd_policy_key_read(NULL, 0) != OS32_ERR_INVAL) return 24;
    handler_owner = APP_ID_SHELL;
    if (!input_delivery_key(&snapshot, &key) || api.kbd_policy_key_read(&key_out, 1) != 1 ||
        api.kbd_policy_key_read(&key_out, 1)) return 25;
    for (unsigned incoming = 0; incoming <= 1; ++incoming) {
        for (unsigned i = 0; i < 128; ++i) {
            key.time_ms = i;
            key.action = KBD_POLICY_ACTION_NONE;
            key.sc_down_repeat = KBD_POLICY_SC_DOWN;
            if (i == 5 || i == 64) key.sc_down_repeat |= KBD_POLICY_SC_REPEAT;
            if (!input_delivery_key(&snapshot, &key)) return 70;
        }
        key.sc_down_repeat = KBD_POLICY_SC_DOWN | KBD_POLICY_SC_REPEAT;
        if (input_delivery_key(&snapshot, &key)) return 71;
        key.sc_down_repeat = incoming ? KBD_POLICY_SC_DOWN : 0;
        key.action = KBD_POLICY_ACTION_SWITCH_APP;
        key.time_ms = 128;
        if (!input_delivery_key(&snapshot, &key)) return 72;
        for (unsigned i = 0; i <= 128; ++i) {
            if (i == 5) continue;
            if (api.kbd_policy_key_read(&key_out, 1) != 1 || key_out.time_ms != i ||
                (i == 128 && (key_out.action != KBD_POLICY_ACTION_SWITCH_APP ||
                              key_out.sc_down_repeat != key.sc_down_repeat)))
                return 73;
        }
        if (api.kbd_policy_key_read(&key_out, 1)) return 74;
    }
    for (unsigned i = 0; i < 128; ++i) {
        key.time_ms = i;
        key.sc_down_repeat = i % 2 ? 0 : KBD_POLICY_SC_DOWN;
        if (!input_delivery_key(&snapshot, &key)) return 75;
    }
    key.sc_down_repeat = KBD_POLICY_SC_DOWN | KBD_POLICY_SC_REPEAT;
    if (input_delivery_key(&snapshot, &key)) return 76;
    /* A plain Tab has no reserved seat among 128 unread transitions. */
    key.action = KBD_POLICY_ACTION_NONE;
    key.physical_mods = key.logical_mods = 0;
    key.keycode = 15;
    key.sc_down_repeat = 15 | KBD_POLICY_SC_DOWN;
    if (input_delivery_key(&snapshot, &key)) return 77;
    for (unsigned i = 0; i < 128; ++i)
        if (api.kbd_policy_key_read(&key_out, 1) != 1 || key_out.time_ms != i)
            return 78;
    handler_owner = 0;
    ring3_wm_depth = 0;
    caller = 1;
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
