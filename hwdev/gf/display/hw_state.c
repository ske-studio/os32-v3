/* Save order: nouveau dispnv04/disp.c:245,297-302; GF_STOP_REF_DESIGN §3-1. */
#include "hw_state.h"
#include "../internal.h"
#include "../../../include/kprintf.h"

static struct gf4_hw_saved hw_saved;
static bool attempted;

struct gf4_hw_saved *gf4_hw_saved_get(void) { return &hw_saved; }
void gf4_hw_state_reset(void)
{
    gf4_hw_fonts_reset(&hw_saved);
    hw_saved=(struct gf4_hw_saved){0};
    attempted=false;
}
void gf4_hw_save(struct gf4_ctx *c,struct gf_io *io)
{
    int rc,first=0; const char *stage="crtc";
    if (attempted) return;
    attempted=true;
    /* devinit/base.c:94 and dispnv04/hw.h:310-320: unlock extended CR. */
    rc=io->write(io->opaque,GF_MODE_REGISTER_BAR,0x6013d4U,1,0x1fU);
    if (!rc) rc=io->write(io->opaque,GF_MODE_REGISTER_BAR,0x6013d5U,1,0x57U);
    if (rc) { first=rc; stage="unlock"; }
    rc=gf4_hw_fonts_save(io,&hw_saved);
    if (rc) kprintf(KLOG_ATTR,"[gf4] hw save fonts skipped rc=%d\n",rc);
    else hw_saved.fonts_valid=hw_saved.fonts!=0;
    for (u8 h=0;h<GF4_HEADS;h++) {
        rc=gf4_hw_crtc_save(io,h,c->limits[h].ref,&hw_saved);
        if (rc && !first) { first=rc; stage="crtc"; }
    }
    rc=gf4_hw_enc_save(io,c->dcb.routes,c->dcb.nroutes,&hw_saved);
    if (rc && !first) { first=rc; stage="enc"; }
    if (first) kprintf(KLOG_ATTR,"[gf4] hw save failed stage=%s rc=%d\n",stage,first);
    else hw_saved.valid=true;
}

/* GF_STOP_REF_DESIGN §3-3: restore only operations without waits/fences. */
int gf4_hw_restore_nodeadline(struct gf4_ctx *c)
{
    if (!hw_saved.valid) return 0;
    struct gf_io *io=&c->legacy->io;
    int first=gf4_hw_enc_restore(c,io,c->dcb.routes,c->dcb.nroutes,&hw_saved,false,0);
    /* Match the normal stop order: script cleanup can relock CR1F.
     * dispnv04/crtc.c:692-706; devinit/base.c:94,117. */
    int unlock_rc=io->write(io->opaque,GF_MODE_REGISTER_BAR,0x6013d4U,1,RECOVER_CR1F);
    if (!unlock_rc) unlock_rc=io->write(io->opaque,GF_MODE_REGISTER_BAR,0x6013d5U,1,RECOVER_CR1F_UNLOCK);
    if (!first) first=unlock_rc;
    for (u8 h=0;h<GF4_HEADS;++h) {
        int rc=gf4_hw_crtc_restore(io,h,&hw_saved);
        if (!first && rc) first=rc;
    }
    kprintf(KLOG_ATTR,"[gf4] stop TMDS/fonts skipped clock unavailable\n");
    return first;
}
