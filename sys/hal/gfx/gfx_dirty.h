#ifndef OS32_GFX_DIRTY_H
#define OS32_GFX_DIRTY_H

#include "gfx_internal.h"

#include "gfx_measure_wire.h"
#include "kdebug_resident_contract.h"

/* The boot-paired debugger owns the session and all output. */
#define GFX_TRACE(k, f, x, y, w, h, n, d) do { \
    if (kdebug.ops.dbg_call) { \
        struct gfx_trace_event event = {k, f, x, y, w, h, n, d}; \
        kdebug.ops.dbg_call(GFX_TRACE_EVENT, &event, sizeof(event)); \
    } \
} while (0)

static inline int gfx_trace_present_policy(int packed)
{
    if (!kdebug.ops.dbg_call) return GFX_TRACE_NORMAL;
    return kdebug.ops.dbg_call(GFX_TRACE_POLICY, 0, packed);
}

/* index is in [0, queue->count). The returned entry belongs to queue. */
static inline const GFX_Rect *gfx_dirty_at(const DirtyRectQueue *queue, int index)
{
    return &queue->rects[index];
}

void gfx_dirty_add(DirtyRectQueue *queue, int width, int height, int x, int y, int w, int h);
void gfx_dirty_merge(DirtyRectQueue *queue, const DirtyRectQueue *previous, int width, int height);

#endif
