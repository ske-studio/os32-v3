#include "access.h"
#include "../quiesce_rows.h"
int gf_display_stop(void *v)
{
    struct gf_display_context *c=v;
    if (!c) return OS32_ERR_INVAL;
    if (!c->engaged) return 0;
    struct gf_init_context cleanup={.io=c->io};
    int rc=c->io.restore(c->io.opaque,&cleanup);
    /* Always attempt all quiesce writes, even if local cleanup failed. The
     * owner bus rejects access if identity/BAR validation revoked its map. */
#define GF_QUIESCE_WRITE(key,off,rule) if (gf_display_write(c,off,4,0)) rc=-1;
    GF_QUIESCE_ROWS(GF_QUIESCE_WRITE)
#undef GF_QUIESCE_WRITE
    c->active=false;c->blank=false;c->engaged=false;c->stop_ok=!rc;
    if (rc) {c->reusable=false;c->last_rc=-1;}
    return rc ? -1 : 0;
}
