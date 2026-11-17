#include "in2_int.h"
#include "io.h"
#include "kbd.h"
#include "legacy_guest.h"
#include "keymap.h"
#include "os32_kapi_shared.h"

static u16 cnt[128];
static u8 mechanical[IN2_MAX_DEV], usb_lock;
/* Sole physical held state, shared by the transitional sc and usage entrances. */
static u8 held[IN2_MAX_DEV][32];
static u8 boot_locks[IN2_MAX_DEV];

u8 input_held_key(u8 src, u16 keycode)
{
    return (held[src - 1][keycode >> 3] >> (keycode & 7)) & 1;
}

static void set_held(u8 src, u16 keycode, u8 down)
{
    u8 bit = 1u << (keycode & 7);
    if (down) held[src - 1][keycode >> 3] |= bit;
    else held[src - 1][keycode >> 3] &= ~bit;
}
/* Compatibility snapshot for existing kbd/IME consumers; merge owns writes. */
volatile u8 kbd_shift_state;

void input_native_locks(u8 src, u8 mods)
{
    mechanical[src - 1] = mods & (SHIFT_CAPS | SHIFT_KANA);
    boot_locks[src - 1] = mechanical[src - 1];
    kbd_shift_state = input_key_mods();
}


int input_key_pressed(u8 sc)
{
    return sc < 128 && cnt[sc] != 0;
}

u8 input_key_mods(void)
{
    unsigned int flags = irq_save();
    u8 mods = usb_lock;
    for (unsigned int i = 0; i < IN2_MAX_DEV; ++i) mods |= mechanical[i];
    if (cnt[KEY_SHIFT]) mods |= SHIFT_SHIFT;
    if (cnt[KEY_CTRL]) mods |= SHIFT_CTRL;
    if (cnt[KEY_GRPH]) mods |= SHIFT_GRPH;
    irq_restore(flags);
    return mods;
}

void input_merge_key(InputEvent *e, u8 old, u8 origin)
{
    u8 sc = e->sc, down = e->down;
    set_held(e->src, sc, down);
    u8 bit = sc == KEY_CAPS ? SHIFT_CAPS : sc == KEY_KANA ? SHIFT_KANA : 0;
    if (origin == INPUT_ORIGIN_NATIVE) {
        boot_locks[e->src - 1] &= ~bit;
        if (down) mechanical[e->src - 1] |= bit;
        else mechanical[e->src - 1] &= ~bit;
    }
    if (old != down) {
        cnt[sc] += down ? 1 : -1;
        if (origin == INPUT_ORIGIN_USB) {
            if (down) usb_lock ^= bit;
        }
        e->buttons = kbd_shift_state = input_key_mods();
        if (legacy_active()) return;
        input_emit_in2(e);
        if (down || cnt[sc] == 0) {
            input_emit_legacy_key(sc, down, e->buttons);
        }
    } else {
        kbd_shift_state = input_key_mods();
        if (down && !legacy_active()) input_emit_legacy_repeat(sc, kbd_shift_state);
    }
}

/* Evaluate and annotate once; both architectures share the legacy exit. */
static void capture_state(KmState *state)
{
    km_state_snapshot(state);
    for (unsigned i = 0; i < IN2_MAX_DEV; ++i) {
        if (boot_locks[i] & SHIFT_CAPS) state->locks |= KM_LOCK_CAPS;
        if (boot_locks[i] & SHIFT_KANA) state->locks |= KM_LOCK_KANA;
    }
}

int input_capture_key(const KmKey *key)
{
    unsigned flags = irq_save();
    KmSource source;
    KmState state;
    KmEvalResult before, result;
    u8 kind, origin, old, sc;
    int rc = OS32_ERR_INVAL;
    if (!key || km_source_lookup(key->src, key->generation, &source) ||
        key->keycode >= source.map->count ||
        (source.map->id == KM_MAP_HID && key->keycode < 4) ||
        key->down > 1 || key->repeat > 1 ||
        !input_src_info(key->src, key->generation, &kind, &origin)) goto out;
    old = input_held_key(key->src, key->keycode);
    if (key->repeat && (!key->down || !old)) goto out;
    capture_state(&state);
    if (km_eval(source.map, key->keycode, &state, &before)) goto out;
    if (origin == INPUT_ORIGIN_NATIVE && key->keycode == KEY_KANA)
        before.effect |= KM_LOCK_KANA;
    if (km_state_apply(key, old, origin, before.effect)) goto out;
    if (origin == INPUT_ORIGIN_NATIVE) {
        if (key->keycode == KEY_CAPS) boot_locks[key->src - 1] &= ~SHIFT_CAPS;
        if (key->keycode == KEY_KANA) boot_locks[key->src - 1] &= ~SHIFT_KANA;
    }
    set_held(key->src, key->keycode, key->down);
    sc = before.compat_sc;
    if (sc < 128 && old != key->down) cnt[sc] += key->down ? 1 : -1;
    capture_state(&state);
    /* Registered descriptors were validated; selection cannot fail here. */
    (void)km_eval(source.map, key->keycode, &state, &result);
    result.is_modifier |= before.is_modifier && key->down && !old;
    /* Keep physical emergency/raw modifiers independent of logical effects. */
    if (origin == INPUT_ORIGIN_NATIVE && sc < 128) {
        u8 bit = sc == KEY_CAPS ? SHIFT_CAPS : sc == KEY_KANA ? SHIFT_KANA : 0;
        if (key->down) mechanical[key->src - 1] |= bit;
        else mechanical[key->src - 1] &= ~bit;
    } else if (origin == INPUT_ORIGIN_USB && key->down && !old) {
        if (sc == KEY_CAPS) usb_lock ^= SHIFT_CAPS;
        if (sc == KEY_KANA) usb_lock ^= SHIFT_KANA;
    }
    kbd_shift_state = input_key_mods();
    km_legacy_compat(&result);
    if (!legacy_active()) {
        InputEvent e = { .src = key->src, .generation = key->generation,
            .time_ms = in2_time_ms(), .type = INPUT_EVENT_KEY, .sc = sc,
            .down = key->down, .buttons = kbd_shift_state };
        if (sc < 128 && old != key->down) input_emit_in2(&e);
        u8 raw_due = sc < 128 && (key->down || cnt[sc] == 0) &&
                     (old != key->down || key->down);
        input_emit_keymap(&result, key->down, raw_due, kbd_shift_state, 0);
    }
    rc = 0;
out:
    irq_restore(flags);
    return rc;
}

void km_release_keys(u8 src, u16 generation)
{
    KmSource source;
    if (km_source_lookup(src, generation, &source)) {
        u8 kind, origin;
        if (input_src_info(src, generation, &kind, &origin) && kind == INPUT_KIND_KBD)
            boot_locks[src - 1] = 0;
        return;
    }
    km_state_cancel_tap(src, generation);
    for (unsigned code = 0; code < source.map->count; ++code) {
        if (!input_held_key(src, code)) continue;
        KmKey key = { .src = src, .generation = generation, .keycode = code };
        (void)input_capture_key(&key);
    }
    mechanical[src - 1] = 0;
    boot_locks[src - 1] = 0;
    kbd_shift_state = input_key_mods();
}
