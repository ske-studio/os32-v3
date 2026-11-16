#include "input_policy.h"
#include "os32_kapi_shared.h"
#include "io.h"
#include "app_ids.h"

static u16 read16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 read32(const u8 *p)
{
    return (u32)read16(p) | ((u32)read16(p + 2) << 16);
}

int ikm_wire_header(const u8 *wire, u32 length, u32 *count)
{
    u32 n;
    if (!wire || !count || length < IKM_HEADER_SIZE ||
        wire[0] != 'I' || wire[1] != 'K' || wire[2] != 'M' || wire[3] != '1' ||
        read16(wire + 4) != 2 || read16(wire + 6) != IKM_ROW_SIZE ||
        read32(wire + 12) != 0)
        return OS32_ERR_INVAL;
    n = read32(wire + 8);
    if (n > IKM_MAX_ROWS || length != IKM_HEADER_SIZE + n * IKM_ROW_SIZE)
        return OS32_ERR_INVAL;
    *count = n;
    return 0;
}

int ikm_wire_row(const u8 *wire, u32 length, u32 index, IkmRow *row)
{
    u32 count;
    const u8 *p;
    IkmRow decoded;
    if (!row || ikm_wire_header(wire, length, &count) || index >= count)
        return OS32_ERR_INVAL;
    p = wire + IKM_HEADER_SIZE + index * IKM_ROW_SIZE;
    decoded.kind = p[0]; decoded.profile = p[1];
    decoded.context = p[2]; decoded.flags = p[3];
    decoded.index = read16(p + 4); decoded.selector = read16(p + 6);
    decoded.value = read32(p + 8); decoded.aux = read32(p + 12);
    *row = decoded;
    return 0;
}

static int valid_map(u8 platform, u16 map)
{
    return map == 2 || (map == 1 && platform == IKM_PLATFORM_PC98);
}

static int valid_sym(u32 sym)
{
    return sym <= 0xffff && !(sym >= 0xd800 && sym <= 0xdfff) &&
        sym < 0xfffe && !(sym >= 0x0300 && sym <= 0x036f);
}

static int row_key(u8 platform, const IkmRow *r, u32 *out)
{
    u32 key, op, slot, group;
    if (!r || (platform != IKM_PLATFORM_PC98 && platform != IKM_PLATFORM_MAC) || r->flags)
        return OS32_ERR_INVAL;
    if (r->kind == 1 || r->kind == 2) {
        if (r->profile || !valid_map(platform, r->context)) return OS32_ERR_INVAL;
        if (r->kind == 1) {
            if (r->index > 1 || r->selector || r->aux ||
                (r->index == 0 && r->value != (r->context == 1 ? 128u : 256u)) ||
                (r->index == 1 && r->value != 0)) return OS32_ERR_INVAL;
            key = (r->context - 1) * 2 + r->index;
        } else {
            if (r->index >= (r->context == 1 ? 128 : 256) || r->selector > 4 ||
                r->aux || !valid_sym(r->value)) return OS32_ERR_INVAL;
            key = 4 + (r->context == 1 ? 0 : 640) + r->index * 5 + r->selector;
        }
    } else if (r->kind == 3 || r->kind == 7) {
        if (r->profile || r->context != 1 || platform != IKM_PLATFORM_PC98 || r->index >= 128)
            return OS32_ERR_INVAL;
        if (r->kind == 3) {
            op = (r->aux >> 8) & 255;
            if (r->selector > 15 || (r->aux & 0xffff0000u) || (r->aux & 255) > 1 ||
                !valid_sym(r->value)) return OS32_ERR_INVAL;
            if ((r->aux & 255) == 0) { if (op) return OS32_ERR_INVAL; }
            else if (r->value != 0xf500 || op == 1 || (op >= 0x7b && op <= 0x7f) ||
                     op > 0xa3 || (op >= 0x89 && op <= 0x97)) return OS32_ERR_INVAL;
            key = 1924 + r->index * 16 + r->selector;
        } else {
            if (r->selector || r->value > 3 || r->aux) return OS32_ERR_INVAL;
            key = 3972 + r->index;
        }
    } else {
        if (r->kind < 5 || r->kind > 8 || r->kind == 7 || r->context > 4 ||
            r->profile < (platform == IKM_PLATFORM_PC98 ? 1 : 3) ||
            r->profile > (platform == IKM_PLATFORM_PC98 ? 2 : 4)) return OS32_ERR_INVAL;
        group = (r->profile - 1) * 5 + r->context;
        if (r->kind == 5) {
            if (!r->index || r->index > 8 || r->selector || r->value > 255 || r->aux)
                return OS32_ERR_INVAL;
            key = 4100 + group * 8 + r->index - 1;
        } else if (r->kind == 6) {
            if (r->index >= 128 || r->aux) return OS32_ERR_INVAL;
            if (r->selector == 0xffff) {
                if (r->context || r->value > 255) return OS32_ERR_INVAL;
                slot = 16;
            } else {
                if ((r->selector & ~0x17u) || r->value > 18) return OS32_ERR_INVAL;
                slot = (r->selector & 7) | ((r->selector & 0x10) >> 1);
            }
            key = 4260 + (group * 128 + r->index) * 17 + slot;
        } else {
            if (!valid_map(platform, r->selector) ||
                r->index >= (r->selector == 1 ? 128 : 256) || r->value > 18 || (r->aux & ~0x17u))
                return OS32_ERR_INVAL;
            slot = (r->aux & 7) | ((r->aux & 0x10) >> 1);
            key = 47780 + (group * 384 + (r->selector == 1 ? 0 : 128) + r->index) * 16 + slot;
        }
    }
    *out = key;
    return 0;
}

/* Sorted delta keys, followed by 0/1/2 byte payloads. No wire copy or
 * separate seen bitmap. At most 8192 delta bytes + 1343 extension bytes
 * + 8192 payload bytes + 3968 second bytes = 21695 bytes.
 * Invalid Unicode surrogate slots encode BSD_SPECIAL without a third byte. */
enum { POLICY_BYTES = 21760, POLICY_STRIDE = 64, POLICY_INDEX = 128 };
typedef struct { u32 key; u16 offset; } PolicyIndex;
typedef struct {
    u8 data[POLICY_BYTES];
    PolicyIndex index[POLICY_INDEX];
    u16 used, count, indexed, term_ext;
    u8 ext_profiles;
} PolicyTable;
STATIC_ASSERT(sizeof(PolicyTable) <= 24 * 1024, ikm_table_budget);
static PolicyTable tables[3];
static const PolicyTable *active, *saved;
static u32 revision = 1, saved_owner, saved_gen;
static u8 saved_valid, machine;
static KmBackend policy_maps[2];
static int (*keys_pressed)(void);

static unsigned payload_size(u32 key)
{
    if (key < 4) return 0;
    return key < 3972 ? 2 : 1;
}

static u32 unpack_delta(const u8 *data, u32 *pos)
{
    u32 value = 0, shift = 0;
    u8 byte;
    do {
        byte = data[(*pos)++];
        value |= (u32)(byte & 127) << shift;
        shift += 7;
    } while (byte & 128);
    return value;
}

static unsigned pack_delta(u8 *data, u32 value)
{
    unsigned n = 0;
    do {
        data[n] = (u8)(value & 127);
        value >>= 7;
        if (value) data[n] |= 128;
        ++n;
    } while (value);
    return n;
}

static u32 payload(const PolicyTable *table, u32 key, u32 *pos)
{
    unsigned n = payload_size(key);
    u32 value = n ? table->data[(*pos)++] : 0;
    if (n == 2) value |= (u32)table->data[(*pos)++] << 8;
    return value;
}

/* Insert before the first greater key. Only adjacent deltas change. This
 * construction is FG1 with IRQ enabled; publication is a pointer exchange. */
static int insert(PolicyTable *table, u32 key, u32 value)
{
    u32 pos = 0, previous = (u32)-1, found = 0, start = 0, after = 0;
    u8 bytes[8];
    unsigned n, width = payload_size(key);
    for (unsigned i = 0; i < table->count; ++i) {
        start = pos;
        found = previous + unpack_delta(table->data, &pos);
        after = pos;
        if (found == key) return OS32_ERR_INVAL; /* W2_DUPLICATE */
        if (found > key) break;
        (void)payload(table, found, &pos);
        previous = found;
        start = after = pos;
    }
    n = pack_delta(bytes, key - previous);
    if (width) bytes[n++] = (u8)value;
    if (width == 2) bytes[n++] = (u8)(value >> 8);
    if (start < table->used) n += pack_delta(bytes + n, found - key);
    int growth = (int)n - (int)(after - start);
    if ((int)table->used + growth > POLICY_BYTES) return OS32_ERR_INVAL;
    if (growth >= 0) {
        for (u32 i = table->used; i > after; --i)
            table->data[i - 1 + growth] = table->data[i - 1];
    } else {
        for (u32 i = after; i < table->used; ++i)
            table->data[(int)i + growth] = table->data[i];
    }
    for (unsigned i = 0; i < n; ++i) table->data[start + i] = bytes[i];
    table->used = (u16)(table->used + growth);
    ++table->count;
    return 0;
}

static void index_table(PolicyTable *table)
{
    u32 pos = 0, key = (u32)-1;
    table->indexed = 0;
    for (unsigned i = 0; i < table->count; ++i) {
        key += unpack_delta(table->data, &pos);
        if (!(i % POLICY_STRIDE)) {
            PolicyIndex *idx = &table->index[table->indexed++];
            idx->key = key; idx->offset = (u16)pos;
        }
        (void)payload(table, key, &pos);
    }
}

/* Binary search plus at most 64 rows, including explicit value zero. */
static int find(const PolicyTable *table, u32 wanted, u32 *value)
{
    unsigned low = 0, high;
    if (!table || !table->indexed || wanted < table->index[0].key) return 0;
    high = table->indexed;
    while (low + 1 < high) {
        unsigned mid = (low + high) / 2;
        if (table->index[mid].key <= wanted) low = mid;
        else high = mid;
    }
    u32 key = table->index[low].key, pos = table->index[low].offset;
    for (unsigned i = low * POLICY_STRIDE;
         i < table->count && i < (low + 1) * POLICY_STRIDE; ++i) {
        u32 v = payload(table, key, &pos);
        if (key == wanted) { *value = v; return 1; }
        if (key > wanted || pos == table->used) break;
        key += unpack_delta(table->data, &pos);
    }
    return 0;
}

static u32 row_value(const IkmRow *r)
{
    if (r->kind == 3 && (r->aux & 255)) return 0xd800 | (r->aux >> 8);
    return r->value;
}

static void key_row(u32 key, u32 value, IkmRow *r)
{
    *r = (IkmRow){0};
    if (key < 4) {
        r->kind = 1; r->context = key / 2 + 1; r->index = key % 2;
        r->value = r->index ? 0 : (r->context == 1 ? 128 : 256);
        return;
    }
    r->value = value;
    if (key < 1924) {
        key -= 4; r->kind = 2; r->context = key < 640 ? 1 : 2;
        if (key >= 640) key -= 640;
        r->index = key / 5; r->selector = key % 5;
    } else if (key < 3972) {
        key -= 1924; r->kind = 3; r->context = 1;
        r->index = key / 16; r->selector = key % 16;
        if (value >= 0xd800 && value <= 0xd8ff) {
            r->value = 0xf500; r->aux = ((value & 255) << 8) | 1;
        }
    } else if (key < 4100) {
        r->kind = 7; r->context = 1; r->index = key - 3972;
    } else {
        u32 group, slot;
        if (key < 4260) {
            key -= 4100; r->kind = 5; group = key / 8; r->index = key % 8 + 1;
        } else if (key < 47780) {
            key -= 4260; r->kind = 6; slot = key % 17; key /= 17;
            group = key / 128; r->index = key % 128;
            r->selector = slot == 16 ? 0xffff : (slot & 7) | ((slot & 8) << 1);
        } else {
            key -= 47780; r->kind = 8; slot = key % 16; key /= 16;
            group = key / 384; key %= 384;
            r->selector = key < 128 ? 1 : 2;
            r->index = key < 128 ? key : key - 128;
            r->aux = (slot & 7) | ((slot & 8) << 1);
        }
        r->profile = group / 5 + 1; r->context = group % 5;
    }
}

static int lookup(const PolicyTable *table, IkmRow *r, u32 *value)
{
    u32 key;
    return !row_key(machine, r, &key) && find(table, key, value);
}

static int contextual(const PolicyTable *table, IkmRow *r, u32 *value)
{
    if (lookup(table, r, value)) return 1;
    r->context = 0;
    return lookup(table, r, value);
}

static int profile_ok(u8 profile)
{
    return machine && profile >= (machine == IKM_PLATFORM_PC98 ? 1 : 3) &&
        profile <= (machine == IKM_PLATFORM_PC98 ? 2 : 4);
}

static int term(const PolicyTable *table, u8 profile, u8 context, u16 id, u8 *out)
{
    IkmRow row = { .kind = 5, .profile = profile, .context = context, .index = id };
    u32 value;
    static const u8 pc98[8] = {3, 0x1b, 0x1a, 0, 0, 8, 13, 9};
    static const u8 mac[8] = {3, 0x15, 4, 0x0b, 0x0c, 8, 13, 9};
    if (contextual(table, &row, &value)) { *out = (u8)value; return 0; }
    if (machine == IKM_PLATFORM_PC98 && (id == 4 || id == 5)) return OS32_ERR_NOTFOUND;
    *out = (machine == IKM_PLATFORM_PC98 ? pc98 : mac)[id - 1];
    return 0;
}

int ikm_policy_term(u8 profile, u8 context, u16 index, u8 *value)
{
    if (!value || !profile_ok(profile) || context > 4 || index < 1 || index > 8)
        return OS32_ERR_INVAL;
    return term(active, profile, context, index, value);
}

static void policy_lookup(const KmBackend *base, KmSource *out)
{
    out->map = &policy_maps[base->id - 1];
    out->map_generation = revision;
}

static void policy_read(const KmBackend *map, u16 code, KmPolicyKey *out)
{
    IkmRow row = { .kind = 2, .context = map->id, .index = code };
    u32 value;
    if (map != &policy_maps[map->id - 1]) return;
    for (unsigned i = 0; i < 5; ++i) {
        row.selector = i;
        if (!lookup(active, &row, &value)) continue;
        if (!i) out->entry.command = (keysym_t)value;
        else if (i < 3) out->entry.group1[i - 1] = (keysym_t)value;
        else out->entry.group2[i - 3] = (keysym_t)value;
    }
    if (map->id != KM_MAP_PC98) return;
    row.kind = 7; row.selector = 0;
    if (lookup(active, &row, &value)) out->flags = (u8)value;
    row.kind = 3;
    for (unsigned i = 0; i < 16; ++i) {
        row.selector = i;
        if (!lookup(active, &row, &value)) continue;
        KmExactCell *cell = &out->bank[i / 8][i % 8];
        if (value >= 0xd800 && value <= 0xd8ff)
            *cell = (KmExactCell){0xf500, KM_BSD_SPECIAL, (u8)value};
        else *cell = (KmExactCell){(keysym_t)value, KM_CHAR, 0};
        if (i >= 8) out->bank1 = 1;
    }
}

int ikm_policy_init(u8 platform, const KmBackend *pc98, const KmBackend *hid,
                    int (*pressed)(void))
{
    if (machine || !pressed || !hid || hid->id != KM_MAP_HID || hid->count != 256 ||
        (platform != IKM_PLATFORM_PC98 && platform != IKM_PLATFORM_MAC) ||
        (platform == IKM_PLATFORM_PC98 && (!pc98 || pc98->id != KM_MAP_PC98 || pc98->count != 128)))
        return OS32_ERR_INVAL;
    if (pc98 && platform == IKM_PLATFORM_PC98) policy_maps[0] = *pc98;
    policy_maps[1] = *hid;
    keys_pressed = pressed; machine = platform;
    km_policy_read_install(policy_read);
    km_policy_lookup_install(policy_lookup);
    return 0;
}

static int busy(u32 owner, u32 gen)
{
    return (saved_valid && (owner != saved_owner || gen != saved_gen)) || keys_pressed();
}

static void bump(void)
{
    if (++revision == 0) ++revision;
}

static int publish(const PolicyTable *table, u32 owner, u32 gen)
{
    unsigned flags = irq_save();
    if (busy(owner, gen)) { irq_restore(flags); return OS32_ERR_BUSY; } /* W3_BUSY */
    if (owner && !saved_valid) { /* W3_SAVE_ONCE */
        saved = active; saved_owner = owner; saved_gen = gen; saved_valid = 1;
    }
    active = table;
    bump();
    irq_restore(flags);
    return 0;
}

u32 ikm_policy_generation(void) { return revision; }

int ikm_policy_set(const void *wire, u32 length, u32 owner, u32 gen)
{
    const u8 *p = wire;
    u32 count;
    PolicyTable *next = 0;
    if (!machine || owner > APP_ID_MAX || ikm_wire_header(p, length, &count)) return OS32_ERR_INVAL;
    if (busy(owner, gen)) return OS32_ERR_BUSY;
    for (unsigned i = 0; i < 3; ++i)
        if (&tables[i] != active && (!saved_valid || &tables[i] != saved)) { next = &tables[i]; break; }
    if (!next) return OS32_ERR_BUSY;
    next->used = next->count = next->indexed = next->term_ext = next->ext_profiles = 0;
    for (u32 i = 0; i < count; ++i) {
        IkmRow row;
        u32 key;
        /* A row is sampled once. No second pass reads caller memory. */
        const u8 *r = p + IKM_HEADER_SIZE + i * IKM_ROW_SIZE;
        row.kind = r[0]; row.profile = r[1]; row.context = r[2]; row.flags = r[3];
        row.index = read16(r + 4); row.selector = read16(r + 6);
        row.value = read32(r + 8); row.aux = read32(r + 12);
        if (row_key(machine, &row, &key)) return OS32_ERR_INVAL;
        if (row.kind == 5 || row.kind == 6) {
            if (++next->term_ext > 128) return OS32_ERR_INVAL;
            if (row.kind == 6) next->ext_profiles |= 1u << row.profile;
        }
        if (insert(next, key, row_value(&row))) return OS32_ERR_INVAL;
    }
    index_table(next);
    /* Byte aliases are checked after default completion, separately for each
     * profile/context; context-specific IDs replace their common counterparts. */
    for (u8 profile = machine == IKM_PLATFORM_PC98 ? 1 : 3;
         profile <= (machine == IKM_PLATFORM_PC98 ? 2 : 4); ++profile)
        for (u8 context = 0; context <= 4; ++context)
            for (u16 i = 1; i <= 8; ++i) {
                u8 a, b;
                if (term(next, profile, context, i, &a)) continue;
                for (u16 j = i + 1; j <= 8; ++j)
                    if (!term(next, profile, context, j, &b) && a == b) return OS32_ERR_INVAL;
            }
    return publish(next, owner, gen);
}

int ikm_policy_reset(u32 owner, u32 gen)
{
    if (!machine || owner > APP_ID_MAX) return OS32_ERR_INVAL;
    return publish(0, owner, gen);
}

void ikm_policy_owner_exit(u32 owner, u32 gen)
{
    unsigned flags = irq_save();
    if (saved_valid && owner == saved_owner && gen == saved_gen) {
        active = saved; /* W3_RESTORE */
        saved = 0; saved_valid = 0;
        bump();
    }
    irq_restore(flags);
}

static void write16(u8 *p, u16 value)
{
    p[0] = (u8)value; p[1] = (u8)(value >> 8);
}

static void write32(u8 *p, u32 value)
{
    write16(p, (u16)value); write16(p + 2, (u16)(value >> 16));
}

int ikm_policy_get(void *wire, u32 capacity, u32 *needed)
{
    u8 *p = wire;
    const PolicyTable *table = active;
    u32 count = table ? table->count : 0, pos = 0, key = (u32)-1;
    if (!machine || !needed) return OS32_ERR_INVAL;
    *needed = IKM_HEADER_SIZE + count * IKM_ROW_SIZE;
    if (capacity < *needed) return OS32_ERR_FULL; /* W4_SHORT */
    if (!p) return OS32_ERR_INVAL;
    p[0] = 'I'; p[1] = 'K'; p[2] = 'M'; p[3] = '1';
    write16(p + 4, 2); write16(p + 6, IKM_ROW_SIZE);
    write32(p + 8, count); write32(p + 12, 0);
    for (u32 i = 0; i < count; ++i) {
        IkmRow row;
        key += unpack_delta(table->data, &pos);
        key_row(key, payload(table, key, &pos), &row);
        u8 *r = p + IKM_HEADER_SIZE + i * IKM_ROW_SIZE;
        r[0] = row.kind; r[1] = row.profile; r[2] = row.context; r[3] = 0;
        write16(r + 4, row.index); write16(r + 6, row.selector);
        write32(r + 8, row.value); write32(r + 12, row.aux);
    }
    return 0;
}

int ikm_policy_action(u8 profile, u8 context, u16 map, u16 keycode,
                       u8 sc, u16 mods, u8 *action)
{
    IkmRow row = { .kind = 8, .profile = profile, .context = context,
        .index = keycode, .selector = map, .aux = mods & 0x17 };
    u32 value;
    if (!action || !profile_ok(profile) || context > 4 || !valid_map(machine, map) ||
        keycode >= (map == 1 ? 128 : 256)) return OS32_ERR_INVAL;
    if (contextual(active, &row, &value)) { /* W2_KEY_ACTION_FIRST */
        *action = (u8)value; return 0;
    }
    /* LANG1/2 are OS KEY_ACTION defaults and stay independent of kind6's
     * per-profile replacement. Match the generated HID action metadata. */
    if (map == KM_MAP_HID) {
        const KmHidExtensions *ext = policy_maps[1].extensions;
        for (unsigned i = 0; i < ext->action_count; ++i)
            if (ext->actions[i][0] == keycode) { *action = ext->actions[i][1]; return 0; }
    }
    row = (IkmRow){ .kind = 6, .profile = profile, .context = context,
        .index = sc, .selector = mods & 0x17 };
    if (sc < 128 && contextual(active, &row, &value)) { *action = (u8)value; return 0; }
    *action = 0;
    if (active && (active->ext_profiles & (1u << profile))) return 0;
    unsigned mask = mods & 0x17;
    if (profile == 1 && sc == 0x60 && !mask) {
        if (context == 1) *action = 4;
        if (context == 2) *action = 3;
    }
    if (profile == 3 && sc == 0x31 && mask == KM_MOD_COMMAND &&
        (context == 1 || context == 2)) *action = context == 1 ? 4 : 3;
    if (profile == 3 && sc == 0x24 && mask == KM_MOD_COMMAND) *action = 9;
    if (profile == 2 || profile == 4) {
        unsigned command = profile == 2 ? KM_MOD_CTRL : KM_MOD_COMMAND;
        if (mask == command) {
            switch (sc) {
            case 0x2a: *action = 11; break;
            case 0x2b: *action = 10; break;
            case 0x2c: *action = 12; break;
            case 0x29: *action = 13; break;
            case 0x1d: *action = 15; break;
            case 0x15: if (profile == 2) *action = 14; break;
            case 0x11: if (profile == 4) *action = 17; break;
            case 0x0f: if (profile == 4) *action = 16; break;
            }
        }
        if (profile == 4 && sc == 0x29 && mask == (KM_MOD_COMMAND | KM_MOD_SHIFT)) *action = 14;
        if (profile == 2 && mask == KM_MOD_ALT) {
            if (sc == 0x0f) *action = 16;
            if (sc == 0x65) *action = 17;
        }
    }
    return 0;
}

int ikm_policy_compat(u8 profile, u8 sc, u8 *byte)
{
    IkmRow row = { .kind = 6, .profile = profile, .index = sc, .selector = 0xffff };
    u32 value;
    static const u8 pairs[][2] = {
        {0x0f,9}, {0x1c,13}, {0x35,0}, {0x36,0x12}, {0x37,3}, {0x38,0x16},
        {0x39,0x7f}, {0x3a,0x1e}, {0x3b,0x1d}, {0x3c,0x1c}, {0x3d,0x1f},
        {0x3e,1}, {0x3f,5}, {0x51,0}, {0x52,0}, {0x53,0}, {0x54,0}, {0x61,0},
        {0x62,0}, {0x63,0}, {0x64,0}, {0x65,0}, {0x66,0}, {0x67,0},
        {0x68,0}, {0x69,0}, {0x6a,0}, {0x6b,0}
    };
    if (!byte || !profile_ok(profile) || sc >= 128) return OS32_ERR_INVAL;
    if (lookup(active, &row, &value)) { *byte = (u8)value; return 0; }
    if (active && (active->ext_profiles & (1u << profile))) return OS32_ERR_NOTFOUND;
    for (unsigned i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
        if (pairs[i][0] == sc) { *byte = pairs[i][1]; return 0; }
    return OS32_ERR_NOTFOUND;
}
