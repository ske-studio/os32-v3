/* S3x uses the actual S2x capture trace plus a remapped physical rescue probe. */
#ifdef S3X_REMAP
#define __wrap_km_backend_register s3x_unused_backend
#define _start s3x_unused_start
#endif
#include "input_s2x_host.c"
#ifdef S3X_REMAP
#undef __wrap_km_backend_register
#undef _start
static KmBackend remapped;
static KmPc98Extensions ext;
static KmExactCell cells[128][8];
int __wrap_km_backend_register(const KmBackend *map)
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
static int rescue(void)
{
    kbd_hal_init(0);
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
void _start(void)
{
    int rc = rescue();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#endif
