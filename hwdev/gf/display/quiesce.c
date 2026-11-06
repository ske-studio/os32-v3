/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "access.h"
#include "state.h"
#include "../../../include/kprintf.h"
#include "../command.h"
#include "../accel_service.h"
#include "../quiesce_rows.h"
#ifdef GF_PRODUCTION_MODULE
#include "api.h"
#endif
/* Keep the first failing target/rc; later successful cleanup must not
 * overwrite it (docs/design/hwdev/gf/display/quiesce.md). */
static void gf_stop_write(struct gf_display_context *c, u32 off, int *rc,
                          u32 *target, int *first_rc)
{
    if (!gf_display_write(c,off,4,0) || *rc) return;
    *rc=-1;*target=off;if (!*first_rc) *first_rc=c->last_rc;
}
/* X3: caller has already stopped F2. nouveau (c03c1a1)
 * nouveau_drm.c:944-988 orders display/fences before the object tree.
 * PC-98 local restoration and these rows are OS32-specific cleanup. */
i32 gf_display_quiesce(struct gf_display_context *c)
{
    if (!c) return OS32_ERR_INVAL;
    int first_rc=0, rc=0;
    bool revoked=false;
    struct gf_foundation *f=0;
    if (c->owner->command_get) c->owner->command_get(c,&f);
    revoked=f && f->revoked;
    int stopped=c->owner->foundation_stop(c);
    int stop_rc=c->last_rc;
    if (stopped) first_rc=stop_rc ? stop_rc : -1;
    /* Restore both scan origins even when lower stop failed. Retain all
     * reservations on error; MMIO still rejects a revoked hardware lease.
     * MIT nouveau dispnv04/hw.c:772 (PCRTC_START restore). */
    if (stopped)
        kprintf(KLOG_ATTR,"GF4 ERROR stage=stop_foundation rc=%d target=%08x\n",
                stop_rc ? stop_rc : -1,c->target);
    u32 target=c->target;
    if (c->baseline_valid) for (u32 h=0;h<2U;h++) {
        u32 off=C_START+h*RECOVER_HEAD_STRIDE;
        if (gf_display_write(c,off,4,c->baseline_start[h])) {
            if (!first_rc) { first_rc=c->last_rc; target=off; }
            rc=-1;
            kprintf(KLOG_ATTR,"GF4 ERROR stage=stop_scan_restore rc=%d head=%u target=%08x\n",
                    c->last_rc,h,off);
        }
    }
    if (stopped) {
        c->reusable=false;c->stop_ok=false;
        if (c->engaged && !revoked) {
            c->target=target;
            c->last_rc=stop_rc ? stop_rc : -1;
            return c->last_rc;
        }
    }
    struct gf_init_context cleanup={.io=c->io};
    int local_rc=c->io.restore(c->io.opaque,&cleanup);
    if (!rc) rc=local_rc;
    if (!rc && !first_rc) target=c->target;
    if (stopped && !first_rc) first_rc=stop_rc ? stop_rc : -1;
    if (rc && !first_rc) first_rc=rc;
    rc=(stopped || rc) ? -1 : 0;
    /* Always attempt all quiesce writes, even if local cleanup failed. The
     * owner bus rejects access if identity/BAR validation revoked its map. */
#define GF_QUIESCE_WRITE(key,off,rule) gf_stop_write(c,off,&rc,&target,&first_rc);
    GF_QUIESCE_ROWS(GF_QUIESCE_WRITE)
#undef GF_QUIESCE_WRITE
    if (rc) {
        c->reusable=false;c->stop_ok=false;c->target=target;
        c->last_rc=first_rc ? first_rc : -1;
    }
    return rc ? c->last_rc : 0;
}
int gf_display_stop(void *v)
{
    struct gf_display_context *c=v;
    if (!c) return OS32_ERR_INVAL;
    struct gf_foundation *f=0;
    struct gf2_chan *ch=c->owner->command_get ? c->owner->command_get(c,&f) : 0;
    struct gf3_service *s=ch ? gf3_service_get(ch) : 0;
    int first_rc=0, rc=0;
    bool clock_failed=false;
    bool f4_owned=false;
#ifdef GF_PRODUCTION_MODULE
    f4_owned=c->display4!=0;
    if (c->display4) {
        u32 now;
        if (c->io.timing.clock_us &&
            !c->io.timing.clock_us(c->io.opaque,&now))
            return gf4_service_stop(c->display4,now+GF3_TIMEOUT_US);
        /* X4: no valid deadline means no fini fence. Still attempt the
         * existing lower stop/cleanup, retaining F4 reservations and loan. */
        first_rc=GF2_EIO;clock_failed=true;
        c->display4->accepting=false;c->display4->reusable=false;
        c->display4->last_rc=first_rc;
        if (c->display4->links && c->display4->links->admission) {
            rc=c->display4->links->admission(c->display4->links->owner,false);
            if (rc && !(f && f->revoked)) goto fail;
        }
    }
#endif
    bool closed=true;
    bool revoked=f && f->revoked;
    if (!c->engaged && !s && !clock_failed) return 0;
    c->section=GF_DISPLAY_STOP;
    c->active=false;
    if (s) {
        rc=gf3_service_close(s);
        if (rc) {
            if (!first_rc) first_rc=rc;
            closed=false;
            if (!revoked) goto fail;
        }
        if (!c->engaged && ch->state==GF2_OFF) {
            if (closed && ch->state==GF2_OFF && !f4_owned) {
                rc=gf3_service_release(s);
                if (rc && !first_rc) first_rc=rc;
            }
            if (first_rc) goto fail;
            return 0;
        }
        if (closed && !clock_failed) {
            u32 now;
            if (!c->io.timing.clock_us || c->io.timing.clock_us(c->io.opaque,&now))
                rc=GF2_EIO;
            else
                rc=gf3_service_fini(s,now+GF3_TIMEOUT_US);
            if (rc && !first_rc) first_rc=rc;
        }
    }
    /* nouveau_drm.c:944-988: stop before restoration. Revoked access
     * still attempts cleanup; the outer backend selects the 98 display. */
    rc=ch ? gf2_stop(ch) : 0;
    if (rc && !first_rc) first_rc=rc;
    if (s && ch->state==GF2_OFF) gf3_service_invalidate(s);
    if (ch && ch->state!=GF2_OFF && !revoked) {
        if (!first_rc) first_rc=GF2_ESTOP;
        goto fail;
    }
    if (rc && !s && !revoked) goto fail;
    rc=gf_display_quiesce(c);
    u32 target=c->target;
    if (rc && !first_rc) first_rc=rc;
    /* Keep display cleanup pending across retries, independently of errors
     * from fini or a later reservation release. */
    if (!rc && closed && !clock_failed && (!ch || ch->state==GF2_OFF)) c->engaged=false;
    if (!rc && s && closed && ch->state==GF2_OFF && !f4_owned) {
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
