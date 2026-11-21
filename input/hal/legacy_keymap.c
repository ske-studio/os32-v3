#include "keymap.h"
#include "in2_int.h"
#include "kbd.h"
#include "io.h"

static const KmBackend *legacy_map;

void input_legacy_keymap_set(const KmBackend *map)
{
    unsigned flags = irq_save();
    const struct InputLegacyProvider *p = input_ime_provider();
    if (p && p->flush_actions) p->flush_actions();
    legacy_map = map;
    irq_restore(flags);
}

void km_legacy_compat(KmEvalResult *result)
{
    /* INPUT_BIND_DESIGN §2a B: COMPAT is an explicit sc-wide override,
     * not a second character map. Keep RLDN's 03 distinct from CHAR ETX. */
    static const struct { u8 sc, byte; } compat[] = {
        {0x0f, 0x09}, {0x1c, 0x0d}, {0x35, 0x00},
        {0x36, 0x12}, {0x37, 0x03}, {0x38, 0x16}, {0x39, 0x7f},
        {0x3a, 0x1e}, {0x3b, 0x1d}, {0x3c, 0x1c}, {0x3d, 0x1f},
        {0x3e, 0x01}, {0x3f, 0x05}, {0x51, 0x00},
        {0x52, 0x00}, {0x53, 0x00}, {0x54, 0x00}, {0x61, 0x00},
        {0x62, 0x00}, {0x63, 0x00}, {0x64, 0x00}, {0x65, 0x00},
        {0x66, 0x00}, {0x67, 0x00}, {0x68, 0x00}, {0x69, 0x00},
        {0x6a, 0x00}, {0x6b, 0x00}
    };
    for (unsigned i = 0; i < sizeof(compat) / sizeof(compat[0]); ++i) {
        if (result->compat_sc != compat[i].sc) continue;
        result->kind = KM_LEGACY_CONTROL;
        result->op = compat[i].byte;
        return;
    }
}

void input_emit_keymap(const KmEvalResult *result, u8 down, u8 raw_due,
                       u8 physical_mods, u8 consumed)
{
    u8 sc = result->compat_sc, byte = 0;
    if (kbd_gui_mode) {
        if (sc != 0xff && legacy_stop_filter(sc, !down, physical_mods)) return;
        if (sc != 0xff && raw_due) legacy_raw_push(sc, !down, physical_mods);
        return;
    }
    if (consumed || !down || result->is_modifier || result->no_keydata) return;
    if (sc == KEY_STOP && input_key_pressed(KEY_CTRL)) return;
    if (result->kind == KM_CHAR && result->sym < 0x80) byte = result->sym;
    if (result->kind == KM_CHAR && is_keypad(result->sym)) byte = result->sym & 0x7f;
    if (result->kind == KM_LEGACY_CONTROL) byte = result->op;
    if (sc == 0xff) sc = 0;
    if (sc == 0 && byte == 0) return;
    legacy_cooked_push(((u16)sc << 8) | byte);
}

/* The unbound sc entrance uses its physical snapshot as local km state.
 * It never asks the registry, nor modifies the bound-source state machine. */
void input_emit_legacy_event(const InputEvent *event, u8 old, u8 raw_due)
{
    u8 sc = event->sc, mods = event->buttons;
    KmKey key = { .src = event->src, .generation = event->generation,
                  .keycode = sc, .down = event->down, .repeat = event->down && old };
    KmState state = {0};
    KmEvalResult result = { .compat_sc = sc };
    if (mods & SHIFT_SHIFT) state.mods |= KM_MOD_SHIFT;
    if (mods & SHIFT_CTRL) state.mods |= KM_MOD_CTRL;
    if (mods & SHIFT_GRPH) state.mods |= KM_MOD_ALT;
    if (mods & SHIFT_CAPS) state.locks |= KM_LOCK_CAPS;
    if (mods & SHIFT_KANA) state.locks |= KM_LOCK_KANA;
    /* Raw-only users need no descriptor (e.g. the transitional PPC sc path). */
    if (legacy_map) {
        if (km_eval(legacy_map, sc, &state, &result)) return;
    } else if (!kbd_gui_mode) return;
    km_legacy_compat(&result);
    if (!input_capture_delivery(&key, old, &result, raw_due))
        input_emit_keymap(&result, key.down, raw_due, mods, 0);
}
