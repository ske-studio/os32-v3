/* One cooperative session, fixed storage, no yield/allocation on a hit. */
#include "probe_wire.h"
#include "debug_probe.h"
#include "hwb_wire.h"
#include "lib/kstring.h"
#include "drivers/char/serial.h"
#include "kprintf.h"
#include "generated/kdebug_import_macros.h"

struct probe_setting {
    char name[PROBE_NAME_SIZE];
    u32 id, count, enabled;
    struct probe_item items[PROBE_ITEM_MAX];
};
struct probe_result { struct probe_item item; u32 value; i32 status, restore_rc; };
struct probe_snapshot {
    char name[PROBE_NAME_SIZE];
    u32 id, hit, count;
    struct probe_result results[PROBE_ITEM_MAX];
};
static struct probe_setting settings[PROBE_ACTIVE_MAX];
static struct probe_snapshot ring[PROBE_SNAPSHOT_MAX];
static u32 setting_count, epoch, head, used, drain_item, hits, captured, dropped, overflow;
static i32 error;
static bool armed, busy, serial;
static struct module_handle owner;

static void probe_stop(void)
{
    if (armed) debug_probe_control(PROBE_DETACH, &owner, sizeof(owner));
    armed = false;
}

static void probe_hit(u32 id)
{
    if (busy) { dropped++; return; }
    if (!armed) return;
    struct probe_setting *p = 0;
    for (u32 i = 0; i < setting_count; i++)
        if (settings[i].id == id && settings[i].enabled) p = &settings[i];
    if (!p) return;
    if (used == PROBE_SNAPSHOT_MAX) {
        dropped++; overflow = 1; error = PROBE_OVERFLOW; probe_stop(); return;
    }
    if (hits == U32_MAX) { error = PROBE_LIMIT; probe_stop(); return; }
    busy = true;
    struct probe_snapshot *s = &ring[(head + used++) % PROBE_SNAPSHOT_MAX];
    kstrncpy(s->name, p->name, sizeof(s->name));
    s->id = id; s->hit = ++hits; s->count = p->count;
    captured++;
    bool failed = false;
    for (u32 i = 0; i < p->count; i++) {
        struct probe_result *r = &s->results[i];
        *r = (struct probe_result){.item = p->items[i], .status = PROBE_SKIP};
        if (!failed) {
            if (r->item.kind == PROBE_IDXIO || r->item.kind == PROBE_IDXMMIO)
                r->status = probe_index_read(&r->item, &r->value, &r->restore_rc);
            else {
                struct hw_req req = {.version = HW_VERSION, .action = HW_READ,
                    .space = r->item.kind == PROBE_IO ? HW_IO : HW_PHYS,
                    .width = r->item.width, .addr = r->item.address};
                r->status = hwb_access(&req, sizeof(req)) ? PROBE_IO_ERROR : (i32)req.status;
                r->value = req.value;
            }
            failed = r->status || r->restore_rc;
            if (failed) {
                p->enabled = 0;
                error = r->restore_rc ? r->restore_rc : r->status;
            }
        }
        if (serial)
            kprintf(ATTR_WHITE, "HW phase=hit op=probe name=%s hit=%u item=%u val=0x%x rc=%d restore_rc=%d\n",
                    s->name, s->hit, i, r->value, r->status, r->restore_rc);
    }
    busy = false;
}

/* Reuse precisely this LIST result for ADD's name/id/epoch binding. */
static bool probe_sync(struct probe_list *list)
{
    if (!setting_count || epoch == list->epoch) return false;
    probe_stop();
    kmemset(settings, 0, sizeof(settings));
    setting_count = 0;
    error = PROBE_ARG;
    return true;
}

static bool wire_valid(const struct probe_wire *w)
{
    if (w->action < PROBE_CMD_LIST || w->action > PROBE_CMD_STATUS) return false;
    struct probe_wire allowed = {.version = w->version, .size = w->size, .action = w->action};
    if (w->action == PROBE_CMD_LIST) allowed.id = w->id;
    if (w->action == PROBE_CMD_ARM) allowed.flags = w->flags & PROBE_SERIAL;
    if (w->action == PROBE_CMD_ADD) {
        bool terminated = false;
        for (u32 i = 0; i < sizeof(w->name); i++) {
            char c = w->name[i];
            if (!c) terminated = true;
            else if (terminated || !((c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
            allowed.name[i] = c;
        }
        if (!w->name[0] || !terminated) return false;
        allowed.kind = w->kind; allowed.width = w->width; allowed.address = w->address;
        allowed.data_address = w->data_address; allowed.index = w->index;
    }
    const u8 *a = (const u8 *)&allowed, *b = (const u8 *)w;
    for (u32 i = 0; i < sizeof(*w); i++) if (a[i] != b[i]) return false;
    return true;
}

i32 probe_call(void *buf, u32 size)
{
    struct probe_wire *w = buf;
    if (!w || size != sizeof(*w) || w->size != sizeof(*w) || w->version != PROBE_VERSION ||
        !wire_valid(w)) return OS32_ERR_INVAL;
    w->status = PROBE_OK;
    struct probe_list list;
    bool stale = false;
    if (w->action == PROBE_CMD_LIST || w->action == PROBE_CMD_ADD ||
        w->action == PROBE_CMD_ARM || w->action == PROBE_CMD_STATUS || w->action == PROBE_CMD_DRAIN) {
        if (debug_probe_control(PROBE_LIST, &list, sizeof(list))) {
            w->status = PROBE_NOSYS; return 0;
        }
        if (w->action != PROBE_CMD_LIST) stale = probe_sync(&list);
    }
    if (w->action == PROBE_CMD_LIST) {
        u32 cursor = w->id;
        if (cursor < list.count) {
            kstrncpy(w->name, list.points[cursor].name, sizeof(w->name));
            w->id = list.points[cursor].id;
            for (u32 i = 0; i < setting_count; i++)
                if (settings[i].id == w->id)
                    w->flags = list.points[cursor].enabled && armed && settings[i].enabled;
            w->more = cursor + 1 < list.count;
        } else if (cursor) w->status = PROBE_ARG;
    } else if (w->action == PROBE_CMD_STOP) probe_stop();
    else if (w->action == PROBE_CMD_STATUS) {
        w->flags = armed; w->status = error;
    } else if (armed) w->status = PROBE_BUSY;
    else if (w->action == PROBE_CMD_RESET) {
        kmemset(settings, 0, sizeof(settings)); kmemset(ring, 0, sizeof(ring));
        setting_count = epoch = head = used = drain_item = hits = captured = dropped = overflow = 0;
        error = PROBE_OK; serial = false;
    } else if (w->action == PROBE_CMD_ADD) {
        struct probe_item item = {w->kind, w->width, w->address, w->data_address, w->index};
        const struct probe_info *info = 0;
        for (u32 i = 0; i < list.count; i++)
            if (!kstrcmp(w->name, list.points[i].name)) info = &list.points[i];
        if (!info || !probe_item_valid(&item)) w->status = PROBE_ARG;
        else {
            u32 i;
            for (i = 0; i < setting_count; i++) if (settings[i].id == info->id) break;
            if (i == PROBE_ACTIVE_MAX || (i < setting_count && settings[i].count == PROBE_ITEM_MAX))
                w->status = PROBE_LIMIT;
            else {
                struct probe_setting *p = &settings[i];
                if (i == setting_count) {
                    *p = (struct probe_setting){.id = info->id};
                    kstrncpy(p->name, info->name, sizeof(p->name));
                    setting_count++;
                }
                p->items[p->count++] = item; epoch = list.epoch; w->id = info->id;
            }
        }
    } else if (w->action == PROBE_CMD_ARM) {
        if (stale || !setting_count) w->status = PROBE_ARG;
        else {
            struct probe_attach a = {.callback = probe_hit, .count = setting_count};
            for (u32 i = 0; i < setting_count; i++) {
                a.ids[i] = settings[i].id;
                for (u32 j = 0; j < settings[i].count; j++)
                    if (!probe_item_valid(&settings[i].items[j])) w->status = PROBE_ARG;
            }
            const struct serial_setup *setup = 0;
            if (w->flags & PROBE_SERIAL) {
                setup = serial_get_setup();
                if (serial_gate_active() || !setup || !setup->actual) w->status = PROBE_NOSYS;
            }
            if (!w->status) {
                if (debug_probe_control(PROBE_KDEBUG_OWNER, &a.owner, sizeof(a.owner)))
                    w->status = PROBE_ARG;
                else {
                    i32 rc = debug_probe_control(PROBE_ATTACH, &a, sizeof(a));
                    if (rc) w->status = rc == PROBE_ERR_BUSY ? PROBE_BUSY : PROBE_ARG;
                    else {
                        owner = a.owner; armed = true; serial = !!(w->flags & PROBE_SERIAL);
                        for (u32 i = 0; i < setting_count; i++) settings[i].enabled = 1;
                    }
                }
            }
        }
    } else if (w->action == PROBE_CMD_DRAIN && used) {
        struct probe_snapshot *s = &ring[head];
        struct probe_result *r = &s->results[drain_item];
        kstrncpy(w->name, s->name, sizeof(w->name));
        w->id = s->id; w->hit = s->hit; w->item = drain_item;
        w->kind = r->item.kind; w->width = r->item.width; w->address = r->item.address;
        w->data_address = r->item.data_address; w->index = r->item.index;
        w->value = r->value; w->status = r->status; w->restore_rc = r->restore_rc;
        *r = (struct probe_result){0};
        if (++drain_item == s->count) {
            *s = (struct probe_snapshot){0}; head = (head + 1) % PROBE_SNAPSHOT_MAX;
            used--; drain_item = 0;
        }
        w->more = used != 0;
    }
    w->captured = captured; w->dropped = dropped; w->overflow = overflow;
    return 0;
}
