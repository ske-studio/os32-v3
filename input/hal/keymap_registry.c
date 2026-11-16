#include "keymap.h"
#include "in2_int.h"
#include "io.h"
#include "os32_kapi_shared.h"

static const KmBackend *maps[2];
static struct {
    u16 generation, map_id;
} bindings[IN2_MAX_DEV];

static int dead(keysym_t sym)
{
    return sym >= 0x0300 && sym <= 0x036f;
}

static int valid_cell(const KmExactCell *cell, int bank1)
{
    if (bank1 && cell->kind == KM_CELL_INHERIT)
        return cell->sym == 0 && cell->op == 0;
    if (cell->kind == KM_CHAR) return cell->op == 0 && !dead(cell->sym);
    if (cell->kind == KM_BSD_SPECIAL)
        return cell->sym == 0xf500 && !(cell->op >= 0x89 && cell->op <= 0x97);
    return 0;
}

static int valid_map(const KmBackend *map)
{
    if (!map || (map->id != KM_MAP_PC98 && map->id != KM_MAP_HID) ||
        map->count != (map->id == KM_MAP_PC98 ? 128 : 256) ||
        !map->entries || !map->compat_sc || !map->extensions ||
        (map->id == KM_MAP_HID && !map->norepeat))
        return 0;
    for (unsigned i = 0; i < map->count; ++i) {
        const KmEntry *e = &map->entries[i];
        if (dead(e->command) || dead(e->group1[0]) || dead(e->group1[1]) ||
            dead(e->group2[0]) || dead(e->group2[1])) return 0;
    }
    if (map->id == KM_MAP_PC98) {
        const KmPc98Extensions *x = map->extensions;
        if (!x->bank0 || !x->flags || x->accent_count || x->accents) return 0;
        for (unsigned i = 0; i < map->count; ++i) {
            if (x->flags[i] > 3) return 0;
            for (unsigned j = 0; j < 8; ++j)
                if (!valid_cell(&x->bank0[i][j], 0) ||
                    (x->bank1 && !valid_cell(&x->bank1[i][j], 1))) return 0;
        }
    } else {
        const KmHidExtensions *x = map->extensions;
        if (!x->order || !x->no_keydata || (x->action_count && !x->actions)) return 0;
        for (unsigned i = 0; i < map->count; ++i)
            if (((x->order[i >> 2] >> ((i & 3) * 2)) & 3) == 3) return 0;
        for (unsigned i = 0; i < x->action_count; ++i) {
            if (x->actions[i][1] < 1 || x->actions[i][1] > 2) return 0;
            for (unsigned j = 0; j < i; ++j)
                if (x->actions[i][0] == x->actions[j][0]) return 0;
        }
    }
    return 1;
}

int km_backend_register(const KmBackend *map)
{
    if (!valid_map(map)) return OS32_ERR_INVAL;
    unsigned flags = irq_save();
    if (maps[map->id - 1]) { irq_restore(flags); return OS32_ERR_INVAL; }
    maps[map->id - 1] = map;
    irq_restore(flags);
    return 0;
}

int km_source_bind(u8 src, u16 generation, u16 map_id)
{
    u8 kind, origin;
    unsigned flags = irq_save();
    if (src < 1 || src > IN2_MAX_DEV || map_id < 1 || map_id > 2 ||
        !maps[map_id - 1] || bindings[src - 1].map_id ||
        !input_src_info(src, generation, &kind, &origin) || kind != INPUT_KIND_KBD) {
        irq_restore(flags);
        return OS32_ERR_INVAL;
    }
    bindings[src - 1].generation = generation;
    bindings[src - 1].map_id = map_id;
    irq_restore(flags);
    return 0;
}

int km_source_lookup(u8 src, u16 generation, KmSource *out)
{
    u8 kind, origin;
    unsigned flags = irq_save();
    if (!out || src < 1 || src > IN2_MAX_DEV || !bindings[src - 1].map_id ||
        bindings[src - 1].generation != generation ||
        !input_src_info(src, generation, &kind, &origin) || kind != INPUT_KIND_KBD) {
        irq_restore(flags);
        return OS32_ERR_INVAL;
    }
    out->map = maps[bindings[src - 1].map_id - 1];
    out->map_generation = 1;
    irq_restore(flags);
    return 0;
}

void km_source_unbind(u8 src, u16 generation)
{
    unsigned flags = irq_save();
    if (src >= 1 && src <= IN2_MAX_DEV && bindings[src - 1].map_id &&
        bindings[src - 1].generation == generation)
        bindings[src - 1].map_id = 0;
    irq_restore(flags);
}

void km_registry_source_released(u8 src, u16 generation)
{
    km_source_unbind(src, generation);
}
