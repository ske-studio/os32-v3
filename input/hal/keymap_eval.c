#include "keymap.h"
#include "os32_kapi_shared.h"

/* NetBSD wscons keysyms; the selected character cell owns the effect. */
static KmEffect cell_effect(keysym_t sym, u8 kind, u8 op, u16 map_id)
{
    if (kind == KM_BSD_SPECIAL) {
        switch (op) {
        case 0x02: case 0x03: return KM_MOD_SHIFT;
        case 0x09: case 0x80: return KM_MOD_CTRL;
        case 0x07: case 0x81: return KM_MOD_ALT;
        case 0x84: return KM_MOD_META;
        case 0x83: return KM_MOD_MODE;
        case 0x04: return KM_LOCK_CAPS;
        case 0x05: return map_id == KM_MAP_HID ? 0 : KM_LOCK_NUM;
        case 0x06: return KM_LOCK_SCROLL;
        case 0x82: return KM_LOCK_ALT;
        case 0x9b: case 0x9c: return KM_MOD_SHIFT | KM_EFFECT_SHIFTA;
        case 0x9d: case 0x9e: return KM_MOD_CTRL | KM_EFFECT_SHIFTA;
        case 0x9f: case 0xa0: return KM_MOD_ALT | KM_EFFECT_SHIFTA;
        default: return 0;
        }
    }
    if (kind != KM_CHAR) return 0;
    switch (sym) {
    case 0xf101: case 0xf102: return KM_MOD_SHIFT;
    case 0xf103: case 0xf104: return KM_MOD_CTRL;
    case 0xf107: case 0xf108: return KM_MOD_ALT;
    case 0xf110: case 0xf111: return map_id == KM_MAP_HID ? KM_MOD_COMMAND : KM_MOD_META;
    case 0xf10a: return KM_MOD_MODE;
    case 0xf105: return KM_LOCK_CAPS;
    case 0xf10b: return map_id == KM_MAP_HID ? 0 : KM_LOCK_NUM;
    case 0xf10c: return KM_LOCK_SCROLL;
    default: return 0;
    }
}

static keysym_t ctrl_char(keysym_t c)
{
    if ((c >= '@' && c <= '_') || (c >= 'a' && c <= 'z')) return c & 0x1f;
    if (c == ' ' || c == '2') return 0;
    if (c >= '3' && c <= '7') return c - '3' + 0x1b;
    if (c == '8') return 0x7f;
    return c;
}

int km_eval(const KmBackend *map, u16 keycode, const KmState *state,
            KmEvalResult *out)
{
    KmEvalResult result = {0};
    const KmEntry *entry;
    if (!map || !state || !out || !map->entries || !map->compat_sc ||
        !map->extensions || keycode >= map->count ||
        !((map->id == KM_MAP_PC98 && map->count == 128) ||
          (map->id == KM_MAP_HID && map->count == 256))) return OS32_ERR_INVAL;
    entry = &map->entries[keycode];
    result.command = entry->command;
    result.compat_sc = map->compat_sc[keycode];
    if (map->id == KM_MAP_PC98) {
        const KmPc98Extensions *ext = map->extensions;
        const KmExactCell *cell;
        unsigned col = (!!(state->mods & KM_MOD_SHIFT)) |
                       (!!(state->mods & KM_MOD_CTRL) << 1) |
                       (!!(state->mods & KM_MOD_ALT) << 2);
        unsigned bank = !!(state->mods & KM_MOD_MODE) ^ !!(state->locks & KM_LOCK_ALT);
        if (!ext->bank0 || !ext->flags || ext->flags[keycode] > 3 ||
            ext->accent_count || ext->accents) return OS32_ERR_INVAL;
        if (((ext->flags[keycode] & 1) && (state->locks & KM_LOCK_CAPS)) ||
            ((ext->flags[keycode] & 2) && (state->locks & KM_LOCK_NUM))) col ^= 1;
        cell = bank && ext->bank1 ? &ext->bank1[keycode][col] : &ext->bank0[keycode][col];
        if (cell->kind == KM_CELL_INHERIT && bank && ext->bank1)
            cell = &ext->bank0[keycode][col];
        if ((cell->kind != KM_CHAR && cell->kind != KM_BSD_SPECIAL) ||
            (cell->kind == KM_CHAR && cell->op)) return OS32_ERR_INVAL;
        result.sym = cell->sym;
        result.kind = cell->kind;
        result.op = cell->op;
    } else {
        const KmHidExtensions *ext = map->extensions;
        const keysym_t *group = (state->mods & KM_MOD_MODE) ? entry->group2 : entry->group1;
        unsigned level = !!(state->mods & KM_MOD_SHIFT);
        unsigned i;
        if (!map->norepeat || !ext->order || !ext->no_keydata ||
            (ext->action_count && !ext->actions)) return OS32_ERR_INVAL;
        if (is_keypad(group[1])) level = 1;
        result.sym = group[level];
        if ((state->locks & KM_LOCK_CAPS) &&
            ((result.sym >= 'a' && result.sym <= 'z') ||
             (result.sym >= 'A' && result.sym <= 'Z'))) result.sym ^= 0x20;
        if ((state->mods & KM_MOD_CTRL) && result.sym < 0x100)
            result.sym = ctrl_char(result.sym);
        result.no_keydata = (ext->no_keydata[keycode >> 3] >> (keycode & 7)) & 1;
        for (i = 0; i < ext->action_count; ++i) {
            if (ext->actions[i][1] < 1 || ext->actions[i][1] > 2) return OS32_ERR_INVAL;
            if (ext->actions[i][0] == keycode) result.action = ext->actions[i][1];
        }
    }
    result.effect = cell_effect(result.sym, result.kind, result.op, map->id);
    result.is_modifier = result.effect != 0 ||
                         (result.kind == KM_CHAR && result.sym >= 0xf101 && result.sym <= 0xf111) ||
                         (result.compat_sc >= 0x70 && result.compat_sc <= 0x74);
    *out = result;
    return 0;
}
