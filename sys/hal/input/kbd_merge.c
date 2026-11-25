#include "in2_int.h"
#include "io.h"
#include "kbd.h"
#include "legacy_guest.h"
#include "keymap.h"
#include "os32_kapi_shared.h"

#if OS32_HOOK_IME
#include "../../input/input_context.h"
#endif

static u32 release_gen[128];
static u16 cnt[128];
static u8 mechanical[IN2_MAX_DEV], usb_lock;
/* Sole physical held state, shared by the transitional sc and usage entrances. */
static u8 held[IN2_MAX_DEV][32];
/* A consumed make stays consumed until this source releases that key. */
static u8 suppressed[IN2_MAX_DEV][32];
static u8 boot_locks[IN2_MAX_DEV];

u8 input_held_key(u8 src, u16 keycode)
{
    return (held[src - 1][keycode >> 3] >> (keycode & 7)) & 1;
}

/* W: stable native mechanical Caps/Kana positions alone do not block set.
 * All other held keys, including USB locks and unmapped usages, do. */
int input_policy_pressed(void)
{
    for (unsigned src = 0; src < IN2_MAX_DEV; ++src)
        for (unsigned byte = 0; byte < 32; ++byte) {
            u8 bits = held[src][byte];
            if (byte == KEY_CAPS / 8 && (mechanical[src] & SHIFT_CAPS))
                bits &= (u8)~(1u << (KEY_CAPS & 7));
            if (byte == KEY_KANA / 8 && (mechanical[src] & SHIFT_KANA))
                bits &= (u8)~(1u << (KEY_KANA & 7));
            if (bits) return 1;
        }
    return 0;
}

static void set_held(u8 src, u16 keycode, u8 down)
{
    u8 bit = 1u << (keycode & 7);
    if (down) held[src - 1][keycode >> 3] |= bit;
    else {
        held[src - 1][keycode >> 3] &= ~bit;
        suppressed[src - 1][keycode >> 3] &= ~bit;
    }
}
/* Compatibility snapshot for existing kbd/IME consumers; merge owns writes. */
volatile u8 kbd_shift_state;

void input_native_locks(u8 src, u8 mods)
{
    mechanical[src - 1] = mods & (SHIFT_CAPS | SHIFT_KANA);
    boot_locks[src - 1] = mechanical[src - 1];
    kbd_shift_state = input_key_mods();
}


u32 input_key_release_gen(u8 sc)
{
    return sc < 128 ? release_gen[sc] : 0;
}

static void update_count(u8 sc, u8 down)
{
    cnt[sc] += down ? 1 : -1;
    if (!down && cnt[sc] == 0) ++release_gen[sc];
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

#if OS32_HOOK_IME
int input_capture_ime(const KmKey *key, u8 old, const KmEvalResult *result)
{
    KbdPolicySnapshot dst;
    KbdPolicyImeSnapshot ime;
    if (result->action) {
        if (key->down && !old && !input_context_snapshot(&dst))
            input_ime_action_capture(result->action, &dst, legacy_cooked_pushed());
        return 1;
    }
    if (!key->down || old ||
        (result->compat_sc != KEY_STOP && !(result->kind == KM_CHAR && result->sym == 3)))
        return 0;
    if (input_context_snapshot(&dst)) return 0;
    input_ime_snapshot(&ime);
    if (!ime.active || ime.owner != dst.owner || ime.exec_gen != dst.exec_gen ||
        ime.command_gen != dst.command_gen || input_ime_cancel_capture(&ime)) return 0;
    suppressed[key->src - 1][key->keycode >> 3] |= 1u << (key->keycode & 7);
    return 1;
}

#endif

int input_capture_delivery(const KmKey *key, u8 old, const KmEvalResult *result, u8 raw_due)
{
    u8 *held_byte = &suppressed[key->src - 1][key->keycode >> 3];
    u8 bit = 1u << (key->keycode & 7);
    if (legacy_active()) return 1;
    if (kbd_gui_mode) {
        input_emit_keymap(result, key->down, raw_due, kbd_shift_state, 0);
        return 1;
    }
    if ((*held_byte & bit) || (result->compat_sc == KEY_STOP && input_key_pressed(KEY_CTRL)))
        return 1;
    const struct InputLegacyProvider *p = input_ime_provider();
    if (p && p->capture_ime) return p->capture_ime(key, old, result);
    return result->action != 0;
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
        update_count(sc, down);
        if (origin == INPUT_ORIGIN_USB) {
            if (down) usb_lock ^= bit;
        }
        e->buttons = kbd_shift_state = input_key_mods();
        if (!legacy_active()) input_emit_in2(e);
    } else {
        e->buttons = kbd_shift_state = input_key_mods();
    }
    if (down || (old && cnt[sc] == 0)) {
        input_emit_legacy_event(e, old, 1);
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
    if (sc < 128 && old != key->down) update_count(sc, key->down);
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
    InputEvent e = { .src = key->src, .generation = key->generation,
        .time_ms = in2_time_ms(), .type = INPUT_EVENT_KEY, .sc = sc,
        .down = key->down, .buttons = kbd_shift_state };
    if (!legacy_active() && sc < 128 && old != key->down) input_emit_in2(&e);
    u8 raw_due = sc < 128 && (key->down || cnt[sc] == 0) &&
                 (old != key->down || key->down);
    if (!input_capture_delivery(key, old, &result, raw_due))
        input_emit_keymap(&result, key->down, raw_due, kbd_shift_state, 0);
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
