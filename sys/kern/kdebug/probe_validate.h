#ifndef KDEBUG_PROBE_VALIDATE_H
#define KDEBUG_PROBE_VALIDATE_H
#include <stdbool.h>
#include "probe_wire.h"

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
#endif
