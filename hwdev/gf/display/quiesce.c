#include "access.h"
#include "../command.h"
#include "../accel_service.h"
#include "../quiesce_rows.h"
/* Keep the first failing target/rc; later successful cleanup must not
 * overwrite it (docs/design/hwdev/gf/display/quiesce.md). */
static void gf_stop_write(struct gf_display_context *c, u32 off, int *rc,
                          u32 *target, int *first_rc)
{
    if (!gf_display_write(c,off,4,0) || *rc) return;
    *rc=-1;*target=off;if (!*first_rc) *first_rc=c->last_rc;
}
int gf_display_stop(void *v)
{
    struct gf_display_context *c=v;
    if (!c) return OS32_ERR_INVAL;
    struct gf_foundation *f=0;
    struct gf2_chan *ch=c->owner->command_get ? c->owner->command_get(c,&f) : 0;
    struct gf3_service *s=ch ? gf3_service_get(ch) : 0;
    int first_rc=0, rc=0;
    bool closed=true;
    bool revoked=f && f->revoked;
    if (!c->engaged && !s) return 0;
    c->section=GF_DISPLAY_STOP;
    c->active=false;
    if (s) {
        rc=gf3_service_close(s);
        if (rc) {
            first_rc=rc; closed=false;
            if (!revoked) goto fail;
        }
        if (!c->engaged && ch->state==GF2_OFF) {
            if (closed && ch->state==GF2_OFF) {
                rc=gf3_service_release(s);
                if (rc && !first_rc) first_rc=rc;
            }
            if (first_rc) goto fail;
            return 0;
        }
        if (closed) {
            u32 now;
            if (!c->io.timing.clock_us || c->io.timing.clock_us(c->io.opaque,&now))
                rc=GF2_EIO;
            else
                rc=gf3_service_fini(s,now+GF3_TIMEOUT_US);
            if (rc && !first_rc) first_rc=rc;
        }
    }
    /* nouveau_drm.c:926-940: stop before restoration. Revoked access
     * still attempts cleanup; the outer backend selects the 98 display. */
    rc=ch ? gf2_stop(ch) : 0;
    if (rc && !first_rc) first_rc=rc;
    if (s && ch->state==GF2_OFF) gf3_service_invalidate(s);
    if (ch && ch->state!=GF2_OFF && !revoked) {
        if (!first_rc) first_rc=GF2_ESTOP;
        goto fail;
    }
    if (rc && !s && !revoked) goto fail;
    int stopped=c->owner->foundation_stop(c);
    int stop_rc=c->last_rc;
    if (stopped) {
        c->reusable=false;c->stop_ok=false;
        if (c->engaged) {
            if (first_rc) c->last_rc=first_rc;
            return -1;
        }
    }
    struct gf_init_context cleanup={.io=c->io};
    rc=c->io.restore(c->io.opaque,&cleanup);
    u32 target=c->target;
    if (stopped && !first_rc) first_rc=stop_rc ? stop_rc : -1;
    if (rc && !first_rc) first_rc=rc;
    rc=(stopped || rc) ? -1 : 0;
    /* Always attempt all quiesce writes, even if local cleanup failed. The
     * owner bus rejects access if identity/BAR validation revoked its map. */
#define GF_QUIESCE_WRITE(key,off,rule) gf_stop_write(c,off,&rc,&target,&first_rc);
    GF_QUIESCE_ROWS(GF_QUIESCE_WRITE)
#undef GF_QUIESCE_WRITE
    /* Keep display cleanup pending across retries, independently of errors
     * from fini or a later reservation release. */
    if (!rc && closed && (!ch || ch->state==GF2_OFF)) c->engaged=false;
    if (!rc && s && closed && ch->state==GF2_OFF) {
        int release_rc=gf3_service_release(s);
        if (release_rc) {rc=-1;if (!first_rc) first_rc=release_rc;}
    }
    if (first_rc) rc=-1;
    c->active=false;c->blank=false;c->stop_ok=!rc;
    if (rc) {c->reusable=false;c->target=target;c->last_rc=first_rc ? first_rc : -1;}
    return rc ? -1 : 0;
fail:
    c->last_rc=first_rc ? first_rc : -1;
    c->reusable=false;c->stop_ok=false;
    return -1;
}
