/* Native legacy mapping service; feature-off never opens a transaction. */
#include "legacy_guest.h"
#include "legacy_lowmap.h"
#include "v86_mem.h"
int legacy_lowmap_busy(void) { return paging_lowmap_busy(); }

/* V86 geometry stays at the caller; mm only knows a low PT transaction. */
int paging_v86_session_open(void) { return paging_lowmap_busy(); }
int paging_v86_snapshot(struct v86_session_state *s)
{
    return paging_lowmap_snapshot(&s->lowmap, s->low_pte,
                                 V86_GUEST_MAP_END / PAGE_SIZE,
                                 V86_GUEST_MAP_END);
}
int paging_v86_restore(struct v86_session_state *s)
{
    return paging_lowmap_restore(&s->lowmap);
}
int paging_v86_map_range(u32 base, u32 end, u32 phys, u32 flags)
{
    return paging_lowmap_map_range(&v86_session.lowmap, base, end, phys, flags);
}


