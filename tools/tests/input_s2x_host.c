/* Compare complete traces with the real 08e024265 HAL/merge, in separate processes. */
#include "in2_int.h"
#include "keymap.h"
#include "sys/platform/pc98/input/kbd.h"
#include "io.h"
#define T(c) do { if (!(c)) return __LINE__ % 120 + 1; } while (0)
static u8 config[2];
static unsigned backend_calls, source_calls, bind_calls, sc_calls, key_calls, eval_calls;
static unsigned logs, bad_route, aborts, guests;
static int guest;
volatile u32 tick_count;
static u32 trace[1024];
static unsigned trace_count;
static void flush(void)
{
    unsigned bytes = trace_count * sizeof(trace[0]);
    if (bytes) {
        int wrote;
        __asm__ volatile("int $0x80" : "=a"(wrote) : "a"(4), "b"(1), "c"(trace), "d"(bytes) : "memory");
        if (wrote != (int)bytes) __builtin_trap();
    }
    trace_count = 0;
}
static void record(u32 v) { trace[trace_count++] = v; if (trace_count == 1024) flush(); }
int legacy_active(void) { return guest; }
int legacy_kbd_push(u8 sc) { (void)sc; guests++; return 1; }
void legacy_request_exit(void) { guests++; }
void ring3_abort_request(void) { aborts++; }
void appslot_poll_yield_reset(void) {}
u32 in2_time_ms(void) { return tick_count * 10; }
void bootlog_push(const char *s, u32 n)
{
    unsigned lines = 0;
    for (unsigned i = 0; i < n; i++) if (s[i] == '\n') lines++;
    if (lines != 1 || !n || s[n - 1] != '\n') bad_route++;
    logs++;
}
int __real_km_backend_register(const KmBackend *map);
static KmBackend remapped;
static KmPc98Extensions ext;
static KmExactCell cells[128][8];
static int register_remapped(const KmBackend *map)
{
    if (map->id != KM_MAP_PC98) return __real_km_backend_register(map);
    remapped = *map;
    ext = *(const KmPc98Extensions *)map->extensions;
    for (int sc = 0; sc < 128; sc++)
        for (int col = 0; col < 8; col++) cells[sc][col] = ext.bank0[sc][col];
    for (int col = 0; col < 8; col++) {
        cells[KEY_CAPS][col] = (KmExactCell){0xf103, KM_CHAR, 0};
        cells[KEY_CTRL][col] = (KmExactCell){'a', KM_CHAR, 0};
    }
    ext.bank0 = cells;
    remapped.extensions = &ext;
    return __real_km_backend_register(&remapped);
}
int __wrap_km_backend_register(const KmBackend *map)
{
    if (config[1] == 5) return register_remapped(map);
    backend_calls++;
    if ((config[1] == 1 && backend_calls == 1) ||
        (config[1] == 2 && backend_calls == 2)) return -1;
    return __real_km_backend_register(map);
}
int __real_input_src_register(u8 kind, u8 origin, u16 *gen);
int __wrap_input_src_register(u8 kind, u8 origin, u16 *gen)
{
    source_calls++;
    if (config[1] == 3 && source_calls == 1) return -1;
    return __real_input_src_register(kind, origin, gen);
}
int __real_km_source_bind(u8 src, u16 gen, u16 map);
int __wrap_km_source_bind(u8 src, u16 gen, u16 map)
{
    bind_calls++;
    if (config[1] == 4) return -1;
    return __real_km_source_bind(src, gen, map);
}
int __real_input_capture(const InputEvent *e);
int __wrap_input_capture(const InputEvent *e)
{
    KmSource map;
    sc_calls++;
    if (!km_source_lookup(e->src, e->generation, &map)) bad_route++;
    return __real_input_capture(e);
}
int __real_input_capture_key(const KmKey *key);
int __wrap_input_capture_key(const KmKey *key)
{
    key_calls++;
    return __real_input_capture_key(key);
}
int __real_km_eval(const KmBackend *map, u16 code, const KmState *s, KmEvalResult *r);
int __wrap_km_eval(const KmBackend *map, u16 code, const KmState *s, KmEvalResult *r)
{
    eval_calls++;
    return __real_km_eval(map, code, s, r);
}
#include "s2x_deliver.inc"
static void step(u8 scan)
{
    InputEvent e; u16 seq;
    kbd_deliver(scan);
    record(scan); record(kbd_shift_state); record(aborts); record(guests);
    for (int sc = 0; sc < 128; sc++) record(input_key_pressed(sc));
    int value;
    while ((value = legacy_cooked_take()) != -1) record(value);
    record(0xffffffff);
    while ((value = legacy_raw_take()) != -1) record(value);
    record(0xfffffffe);
    while (in2_ring_pop(&e, &seq)) {
        record(e.type); record(e.sc); record(e.down); record(e.buttons); record(e.time_ms);
    }
    record(0xfffffffd);
}
static int rescue(void)
{
    for (int gui = 0; gui < 2; gui++) {
        KmState state;
        kbd_set_gui_mode(gui);
        kbd_deliver(KEY_CAPS); /* logical Ctrl, no physical Ctrl */
        km_state_snapshot(&state);
        T(state.mods & KM_MOD_CTRL);
        T(!input_key_pressed(KEY_CTRL));
        unsigned before = aborts;
        kbd_deliver(KEY_STOP);
        T(aborts == before);
        if (!gui) T(legacy_cooked_take() == 0x6000);
        kbd_deliver(KEY_STOP | 128);
        kbd_deliver(KEY_CAPS | 128);
        legacy_raw_flush(); legacy_cooked_flush();
        kbd_deliver(KEY_CTRL); /* physical Ctrl, remapped to a */
        km_state_snapshot(&state);
        T(!(state.mods & KM_MOD_CTRL));
        T(input_key_pressed(KEY_CTRL));
        kbd_deliver(KEY_STOP);
        T(aborts == before + 1);
        T(legacy_cooked_take() == -1);
        kbd_deliver(KEY_STOP | 128);
        if (gui) {
            kbd_discard_stop();
            kbd_deliver(KEY_STOP);
            T(aborts == before + 1);
            kbd_deliver(KEY_STOP | 128);
            tick_count += 50;
            kbd_deliver(KEY_STOP);
            T(aborts == before + 2);
            kbd_deliver(KEY_STOP | 128);
        }
        kbd_deliver(KEY_CTRL | 128);
        legacy_raw_flush();
    }
    guest = 1;
    unsigned before = aborts;
    kbd_deliver(KEY_CTRL); kbd_deliver(KEY_STOP);
    T(aborts == before && guests == 4);
    kbd_deliver(KEY_STOP | 128); kbd_deliver(KEY_CTRL | 128);
    guest = 0;
    T(legacy_cooked_take() == -1);
    return 0;
}
static int probe(void)
{
    int n;
    __asm__ volatile("int $0x80" : "=a"(n) : "a"(3), "b"(0), "c"(config), "d"(2) : "memory");
    T(n == 2);
    kbd_hal_init(config[0]);
    if (config[1] == 5) return rescue();
    T(input_key_mods() == config[0]);
#ifndef S2X_OLD
    T(logs == !!config[1]);
    T(backend_calls == (config[1] == 1 ? 1u : 2u));
    T(source_calls == ((config[1] == 3 || config[1] == 4) ? 2u : 1u));
    T(bind_calls == (config[1] == 0 || config[1] == 4 ? 1u : 0u));
    KmSource map;
    T((km_source_lookup(1, 1, &map) == 0) == !config[1]);
#endif
    /* BIOS-seeded locks must already affect text before any lock-key event. */
    step(0x1d); step(0x1d); step(0x9d);
    for (int gui = 0; gui < 2; gui++) {
        kbd_set_gui_mode(gui);
        for (int mask = 0; mask < 32; mask++) {
            for (int sc = 0; sc < 128; sc++) {
                /* All 128 keys, including a modifier already held by the mask. */
                for (int i = 0; i < 5; i++) {
                    int pressed = (mask & (1 << i)) ||
                        (i == 1 && (config[0] & SHIFT_CAPS)) ||
                        (i == 2 && (config[0] & SHIFT_KANA));
                    step((KEY_SHIFT + i) | (pressed ? 0 : 128));
                }
                step(sc); step(sc); step(sc | 128);
            }
        }
        for (int i = 0; i < 5; i++) step((KEY_SHIFT + i) | 128);
        step(KEY_CTRL); step(KEY_STOP); step(KEY_STOP | 128); step(KEY_CTRL | 128);
        if (gui) {
            kbd_discard_stop(); step(KEY_STOP); step(KEY_STOP | 128);
            tick_count += 50; step(KEY_STOP); step(KEY_STOP | 128);
        }
    }
    guest = 1;
    step(KEY_SHIFT); step(0x1d); step(0x1d); step(0x9d); step(KEY_SHIFT | 128);
    step(KEY_CAPS); step(KEY_CAPS | 128); step(KEY_KANA); step(KEY_KANA | 128);
    guest = 0;
    T(!input_key_mods());
    kbd_set_gui_mode(0);
    step(0x1d); step(0x1d); step(0x9d);
    guest = 1; step(KEY_CAPS); step(KEY_KANA); guest = 0;
    step(0x1d); step(0x1d); step(0x9d);
    step(KEY_CAPS | 128); step(KEY_KANA | 128);
    step(0x1d); step(0x9d);
#ifndef S2X_OLD
    T(!bad_route);
    if (config[1]) T(sc_calls && !key_calls && eval_calls);
    else T(!sc_calls && key_calls && eval_calls == key_calls * 2);
#endif
    flush();
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}

int in2_excl_drop_raw(u8 sc, int br) { (void)sc; (void)br; return 0; }
