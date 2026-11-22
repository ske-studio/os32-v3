#include "kernel/module/module.h"
#include "debug_probe_snapshot.h"
#include "kdebug/probe_validate.h"

/* The loader resolves this typed slot before publishing dbg_call. */
struct {
    void (*snapshot)(struct probe_list *);
} kdebug_import_slots __attribute__((section(".data.kdebug_import_slots")));

static i32 diag_call(u32 op, void *buf, u32 size)
{
    struct probe_wire *w = buf;
    if (op != PROBE_OP) return OS32_ERR_NOSYS;
    if (!w || size != sizeof(*w) || w->version != PROBE_VERSION ||
        w->size != sizeof(*w) || !wire_valid(w)) return OS32_ERR_INVAL;
    if (w->action != PROBE_CMD_LIST) {
        w->status = PROBE_NOSYS;
        return 0;
    }

    struct probe_list list;
    kdebug_import_slots.snapshot(&list);
    u32 cursor = w->id;
    if (cursor < list.count) {
        const struct probe_info *point = &list.points[cursor];
        for (u32 i = 0; i < sizeof(w->name); i++) w->name[i] = point->name[i];
        w->id = point->id;
        w->flags = point->enabled;
        w->more = cursor + 1 < list.count;
    } else if (cursor) w->status = PROBE_ARG;
    return 0;
}

const struct kdebug_ops kdebug_ops = {
    .version = KDEBUG_OPS_VERSION, .dbg_call = diag_call
};

int kdebug_init(struct kdebug_ops *candidate)
{
    for (u32 i = 0; i < sizeof(*candidate); i++)
        ((u8 *)candidate)[i] = ((const u8 *)&kdebug_ops)[i];
    return 0;
}
