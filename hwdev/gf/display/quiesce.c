#include "access.h"
#include "../quiesce_rows.h"
/* Keep the first failing target/rc; later successful cleanup must not
 * overwrite it (GF_DRIVER_DESIGN §3). */
static void gf_stop_write(struct gf_display_context *c, u32 off, int *rc,
                          u32 *target, int *first_rc)
{
    if (!gf_display_write(c,off,4,0) || *rc) return;
    *rc=-1;*target=off;*first_rc=c->last_rc;
}
int gf_display_stop(void *v)
{
    struct gf_display_context *c=v;
    if (!c) return OS32_ERR_INVAL;
    if (!c->engaged) return 0;
    c->section=GF_DISPLAY_STOP;
    struct gf_init_context cleanup={.io=c->io};
    int rc=c->io.restore(c->io.opaque,&cleanup);
    u32 target=c->target;
    int first_rc=rc;
    if (rc) rc=-1;
    /* Always attempt all quiesce writes, even if local cleanup failed. The
     * owner bus rejects access if identity/BAR validation revoked its map. */
#define GF_QUIESCE_WRITE(key,off,rule) gf_stop_write(c,off,&rc,&target,&first_rc);
    GF_QUIESCE_ROWS(GF_QUIESCE_WRITE)
#undef GF_QUIESCE_WRITE
    c->active=false;c->blank=false;c->engaged=false;c->stop_ok=!rc;
    if (rc) {c->reusable=false;c->target=target;c->last_rc=first_rc ? first_rc : -1;}
    return rc ? -1 : 0;
}
