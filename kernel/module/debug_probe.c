#include "debug_probe.h"
#include "memmap.h"
#include "kstring.h"

struct probe_entry {
    char name[PROBE_NAME_SIZE];
    struct module_handle owner;
    struct probe_slot *slot;
};
static struct probe_entry entries[PROBE_POINT_MAX];
static struct module_handle callback_owner;
static u32 attached;

static int same_owner(struct module_handle a, struct module_handle b)
{
    return a.slot == b.slot && a.generation == b.generation;
}

/* Subtraction checks include the full object and avoid end-address overflow. */
static int contains(u32 base, u32 length, const void *p, u32 size)
{
    u32 address = (u32)(uptr)p;
    return address >= base && size <= length && address - base <= length - size;
}

static int slots_overlap(const struct probe_slot *a, const struct probe_slot *b)
{
    u32 x = (u32)(uptr)a, y = (u32)(uptr)b;
    return x >= y ? x - y < sizeof(*a) : y - x < sizeof(*a);
}

static int writable_slot(const struct module_record *r, struct probe_slot *slot)
{
    u32 address = (u32)(uptr)slot;
    u32 backing = (u32)(uptr)P2V(r->backing);
    u32 size = sizeof(*slot);
    if (address % __alignof__(struct probe_slot) || address < r->base ||
        r->pages > U32_MAX / MODULE_PAGE_SIZE ||
        !contains(backing, r->pages * MODULE_PAGE_SIZE, slot, size)) return 0;
    /* Module body pages outside text/ro are resident writable storage. */
    if ((address < r->text && r->text - address < size) ||
        contains(r->text, r->text_size, slot, 1) ||
        (address < r->ro && r->ro - address < size) ||
        contains(r->ro, r->ro_size, slot, 1)) return 0;
    return 1;
}

static int valid_name(const struct module_record *r, const char *name)
{
    for (u32 i = 0; i < PROBE_NAME_SIZE; i++) {
        /* Validate each byte before reading it, including the terminator. */
        if (!contains(r->ro, r->ro_size, name, i + 1)) return 0;
        u8 c = (u8)name[i];
        if (!c) return i != 0;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.')) return 0;
    }
    return 0;
}

static void stop_attachment(void)
{
    /* Remove every executable pointer before releasing callback ownership. */
    for (u32 i = 0; i < PROBE_POINT_MAX; i++) {
        if (!entries[i].slot) continue;
        entries[i].slot->hit = 0;
        entries[i].slot->id = 0;
    }
    if (attached) {
        module_ref_release(callback_owner, MODULE_REF_REGISTRATION);
        attached = 0;
        callback_owner = (struct module_handle){0, 0};
    }
}

i32 debug_probe_register(struct module_handle owner,
                         const struct probe_decl *table, u32 count)
{
    struct module_record *r = module_record_get(owner);
    u32 free_count = 0, acquired = 0;
    if (!r || (r->state != MODULE_STARTING && r->state != MODULE_RUNNING))
        return PROBE_ERR_ARG;
    if (attached) return PROBE_ERR_BUSY;
    if (count > PROBE_POINT_MAX) return PROBE_ERR_LIMIT;
    if (!count) return 0;
    if ((u32)(uptr)table % __alignof__(struct probe_decl) ||
        !contains(r->ro, r->ro_size, table, count * sizeof(*table)))
        return PROBE_ERR_ARG;
    for (u32 i = 0; i < PROBE_POINT_MAX; i++) free_count += !entries[i].slot;
    if (count > free_count) return PROBE_ERR_LIMIT;
    for (u32 i = 0; i < count; i++) {
        if (!valid_name(r, table[i].name) || !writable_slot(r, table[i].slot))
            return PROBE_ERR_ARG;
        for (u32 j = 0; j < i; j++)
            if (slots_overlap(table[i].slot, table[j].slot) ||
                !kstrcmp(table[i].name, table[j].name)) return PROBE_ERR_ARG;
        for (u32 j = 0; j < PROBE_POINT_MAX; j++)
            if (entries[j].slot && (slots_overlap(entries[j].slot, table[i].slot) ||
                !kstrcmp(entries[j].name, table[i].name))) return PROBE_ERR_ARG;
    }
    /* Acquire all references before modifying either the registry or slots. */
    while (acquired < count) {
        if (!module_ref_acquire(owner, r->owner, MODULE_REF_REGISTRATION, 0)) {
            while (acquired) {
                module_ref_release(owner, MODULE_REF_REGISTRATION);
                acquired--;
            }
            return PROBE_ERR_ARG;
        }
        acquired++;
    }
    for (u32 i = 0, j = 0; i < count; i++) {
        while (entries[j].slot) j++;
        struct probe_entry *e = &entries[j];
        table[i].slot->hit = 0;
        table[i].slot->id = 0;
        kstrncpy(e->name, table[i].name, sizeof(e->name));
        e->owner = owner;
        e->slot = table[i].slot;
    }
    return 0;
}

i32 debug_probe_unregister(struct module_handle owner)
{
    if (!module_record_get(owner)) return PROBE_ERR_ARG;
    u32 found = 0;
    for (u32 i = 0; i < PROBE_POINT_MAX; i++)
        if (entries[i].slot && same_owner(entries[i].owner, owner)) found = 1;
    if (found || (attached && same_owner(callback_owner, owner))) stop_attachment();
    for (u32 i = 0; i < PROBE_POINT_MAX; i++) {
        if (!entries[i].slot || !same_owner(entries[i].owner, owner)) continue;
        entries[i].slot->hit = 0;
        entries[i].slot->id = 0;
        kmemset(&entries[i], 0, sizeof(entries[i]));
        module_ref_release(owner, MODULE_REF_REGISTRATION);
    }
    return 0;
}

static i32 attach(const struct probe_attach *a)
{
    struct module_record *r = module_record_get(a->owner);
    if (attached) return PROBE_ERR_BUSY;
    if (a->count > PROBE_ACTIVE_MAX) return PROBE_ERR_LIMIT;
    if (!a->count || !a->callback || !r || r->state != MODULE_RUNNING ||
        r->module_id != MODULE_ID_KDEBUG ||
        !module_text_contains(r, (u32)(uptr)a->callback)) return PROBE_ERR_ARG;
    for (u32 i = 0; i < a->count; i++) {
        if (a->ids[i] >= PROBE_POINT_MAX) return PROBE_ERR_ARG;
        const struct probe_entry *e = &entries[a->ids[i]];
        struct module_record *provider = module_record_get(e->owner);
        if (!e->slot || !provider || provider->state != MODULE_RUNNING)
            return PROBE_ERR_ARG;
        for (u32 j = 0; j < i; j++)
            if (a->ids[j] == a->ids[i]) return PROBE_ERR_ARG;
    }
    if (!module_ref_acquire(a->owner, r->owner, MODULE_REF_REGISTRATION, 0))
        return PROBE_ERR_ARG;
    callback_owner = a->owner;
    attached = 1;
    for (u32 i = 0; i < a->count; i++) {
        struct probe_slot *slot = entries[a->ids[i]].slot;
        slot->id = a->ids[i];
        slot->hit = a->callback;
    }
    return 0;
}

i32 debug_probe_control(u32 action, void *arg, u32 size)
{
    if (!arg) return PROBE_ERR_ARG;
    if (action == PROBE_ATTACH && size == sizeof(struct probe_attach))
        return attach(arg);
    if (action == PROBE_DETACH && size == sizeof(struct module_handle)) {
        if (attached && !same_owner(*(struct module_handle *)arg, callback_owner))
            return PROBE_ERR_ARG;
        stop_attachment();
        return 0;
    }
    if (action == PROBE_LIST && size == sizeof(struct probe_list)) {
        struct probe_list *list = arg;
        kmemset(list, 0, sizeof(*list));
        for (u32 i = 0; i < PROBE_POINT_MAX; i++) {
            const struct probe_entry *e = &entries[i];
            struct module_record *r = module_record_get(e->owner);
            if (!e->slot || !r || r->state != MODULE_RUNNING) continue;
            struct probe_info *info = &list->points[list->count++];
            kstrncpy(info->name, e->name, sizeof(info->name));
            info->owner = e->owner;
            info->id = i;
            info->enabled = e->slot->hit != 0;
        }
        return 0;
    }
    return PROBE_ERR_ARG;
}
