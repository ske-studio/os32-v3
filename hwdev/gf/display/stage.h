/* Lifecycle stages get independent budgets; waits within a stage keep dl.
 * MIT nouveau c03c1a1 nvkm/engine/gr/nv04.c:1220, nv40.c:109:
 * nvkm_msec starts the timeout at the operation, after device init.
 */
#ifndef HWDEV_GF_DISPLAY_STAGE_H
#define HWDEV_GF_DISPLAY_STAGE_H
#include "api.h"

static inline i32 gf4_stage_deadline(struct gf4_ctx *c,u32 *dl)
{
    if (!c || !c->ch || !c->ch->f) return GF4_EINVAL;
    struct gf_io *io=&c->ch->f->io;
    u32 now;
    if (!io->timing.clock_us || io->timing.clock_us(io->opaque,&now))
        return GF2_EIO;
    c->deadline=*dl=now+GF2_TIMEOUT_US;
    return 0;
}
#endif
