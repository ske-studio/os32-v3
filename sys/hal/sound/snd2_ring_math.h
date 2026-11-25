#ifndef SND2_RING_MATH_H
#define SND2_RING_MATH_H
#include "types.h"

/* Position samples are comparable only within one revolution. */
static inline int snd2_backtracked(u32 prev_seg, u32 prev, u32 seg, u32 pos)
{
    return prev_seg == seg && pos < prev;
}

static inline u32 snd2_room(u32 capacity, u32 used)
{
    return capacity - used;
}

/* Plan a read or write without publishing the cursor. Both the legacy FIFO
 * and the retained staging buffer can span the physical end in one request. */
static inline u32 snd2_window(u32 cursor, u32 capacity, u32 available,
                              u32 frames, u32 *off)
{
    *off = cursor % capacity;
    return frames < available ? frames : available;
}

static inline u32 snd2_prefix(u32 capacity, u32 off, u32 n)
{
    u32 head = capacity - off;
    return n < head ? n : head;
}
#endif
