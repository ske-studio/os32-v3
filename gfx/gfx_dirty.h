#ifndef OS32_GFX_DIRTY_H
#define OS32_GFX_DIRTY_H

#include "gfx_internal.h"

/* index is in [0, queue->count). The returned entry belongs to queue. */
static inline const GFX_Rect *gfx_dirty_at(const DirtyRectQueue *queue, int index)
{
    return &queue->rects[index];
}

void gfx_dirty_add(DirtyRectQueue *queue, int width, int height, int x, int y, int w, int h);
void gfx_dirty_merge(DirtyRectQueue *queue, const DirtyRectQueue *previous, int width, int height);

#endif
