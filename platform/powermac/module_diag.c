#include "kernel/module/module.h"
#include "debug_probe_snapshot.h"
#include "kdebug/probe_validate.h"

#include "diag_slots.h"

struct pm_diag_slots kdebug_import_slots __attribute__((section(".data.kdebug_import_slots")));

static i32 diag_call(u32 op, void *buf, u32 size)
{
    struct probe_wire *w = buf;
    if (op == HW_OP) {
        struct hw_req *req = buf;
        if (req && size >= HW_REQ_BYTES && req->version == HW_VERSION && req->action == 7) {
            if (size != HW_RANGE_REQ_BYTES) return OS32_ERR_INVAL;
            struct hw_range_req *range = buf;
            u32 pa = 0;
            if (req->width != 1 || req->bdf > HW_BDF_MASK || range->bar >= 6 ||
                (req->space != HW_IO && req->space != HW_PHYS) || !range->bytes ||
                req->value || req->status) req->status = HW_ARG;
            else req->status = kdebug_import_slots.bar(req->bdf, range->bar, req->space,
                req->addr, range->offset, range->bytes, &pa);
            req->value = req->status == HW_OK ? pa : 0;
            return 0;
        }
        return hwb_access(buf, size);
    }
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
