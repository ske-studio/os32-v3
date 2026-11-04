/* [HW3] nouveau dispnv04/crtc.c:175-233, hw.c:85-124,
 * hw.h:238-240. Transaction and encoder ownership: GF_FEAT4_DESIGN B5. */
#include "api.h"
#include "../internal.h"
#include "../foundation.h"

#define DP_BAR 0U
#define DP_CR 0x6013d4U
#define DP_SR 0x0c03c4U
#define DP_STRIDE 0x2000U
#define DP_OWNER 0x44U
#define DP_RPC1 0x1aU
#define DP_MODE 0x17U
#define DP_RESET 0U
#define DP_CLOCK 1U
#define DP_SCREEN_OFF 0x20U
#define DP_SYNC_MASK 0xc0U
#define DP_MODE_ENABLE 0x80U
#define DP_DELAY_US 10000U

static int dp_write(struct gf_io *io,u32 port,u32 index,u32 value)
{
    int rc=io->write(io->opaque,DP_BAR,port,1,index);
    return rc ? rc : io->write(io->opaque,DP_BAR,port+1U,1,value);
}
static int dp_read(struct gf_io *io,u32 port,u32 index,u32 *value)
{
    int rc=io->write(io->opaque,DP_BAR,port,1,index);
    return rc ? rc : io->read(io->opaque,DP_BAR,port+1U,1,value);
}
static int dp_screen(struct gf_io *io,u8 head,bool blank)
{
    u32 sr,port=DP_SR+head*DP_STRIDE;
    int rc=dp_write(io,DP_CR,DP_OWNER,head ? 3U : 0U);
    if (!rc) rc=dp_read(io,port,DP_CLOCK,&sr);
    if (!rc) rc=dp_write(io,port,DP_RESET,1);
    if (!rc) rc=dp_write(io,port,DP_CLOCK,(sr&~DP_SCREEN_OFF)|(blank ? DP_SCREEN_OFF : 0));
    if (!rc) rc=dp_write(io,port,DP_RESET,3);
    return rc;
}
static int dp_crtc(struct gf_io *io,u8 head,enum gf4_dpms mode,bool blank,u32 dl)
{
    u32 cr=DP_CR+head*DP_STRIDE,sr=DP_SR+head*DP_STRIDE;
    u32 rpc,clock,control,now;
    u32 sync=mode==GF4_STANDBY ? 0x80U : mode==GF4_SUSPEND ? 0x40U : mode==GF4_OFF ? 0xc0U : 0;
    int rc=dp_write(io,DP_CR,DP_OWNER,head ? 3U : 0U);
    if (!rc) rc=dp_read(io,cr,DP_RPC1,&rpc);
    if (!rc) rc=dp_write(io,sr,DP_RESET,1);
    if (!rc) rc=dp_read(io,sr,DP_CLOCK,&clock);
    if (!rc) rc=dp_write(io,sr,DP_CLOCK,(clock&~DP_SCREEN_OFF)|((blank || mode!=GF4_ON) ? DP_SCREEN_OFF : 0));
    if (!rc) rc=dp_read(io,cr,DP_MODE,&control);
    if (!rc && io->timing.clock_us(io->opaque,&now)) rc=GF2_EIO;
    if (!rc && ((i32)(now-dl)>=0 || dl-now<DP_DELAY_US)) rc=GF4_ETIME;
    if (!rc && io->timing.wait_us(io->opaque,DP_DELAY_US)) rc=GF2_EIO;
    if (!rc && io->timing.clock_us(io->opaque,&now)) rc=GF2_EIO;
    if (!rc && (i32)(now-dl)>=0) rc=GF4_ETIME;
    if (!rc) rc=dp_write(io,cr,DP_MODE,(control&~DP_MODE_ENABLE)|(mode==GF4_OFF ? 0 : DP_MODE_ENABLE));
    if (!rc) rc=dp_write(io,sr,DP_RESET,3);
    if (!rc) rc=dp_write(io,cr,DP_RPC1,(rpc&~DP_SYNC_MASK)|sync);
    return rc;
}
static int dp_check(struct gf4_ctx *ctx,u8 head)
{
    if (!ctx || !ctx->state || head>=GF4_HEADS) return GF4_EINVAL;
    if (!ctx->ready || !ctx->ch || !ctx->ch->f || ctx->epoch!=ctx->ch->epoch) return GF4_ESTATE;
    return 0;
}
int gf4_dpms_set(struct gf4_ctx *ctx,u8 head,enum gf4_dpms mode,u32 dl)
{
    int rc=dp_check(ctx,head);
    if (rc) return rc;
    if ((unsigned)mode>GF4_OFF) return GF4_EINVAL;
    if (!ctx->state->head[head].enabled) return GF4_ESTATE;
    rc=gf4_change_begin(ctx,dl);
    if (rc) return rc;
    struct gf4_head_image *h=&ctx->state->next[head];
    h->dpms=mode;
    struct gf_io *io=&ctx->ch->f->io;
    rc=dp_crtc(io,head,mode,h->blank,dl);
    if (!rc) {
        switch (h->route.type) {
        case GF4_ANALOG: rc=gf4_analog_dpms(ctx,&h->route,mode,dl); break;
        case GF4_TV: rc=gf4_tv_dpms(ctx,&h->route,mode,dl); break;
        case GF4_TMDS: case GF4_LVDS: rc=gf4_tmds_dpms(ctx,&h->route,mode,dl); break;
        default: rc=GF4_EUNSUPPORTED; break;
        }
    }
    if (!rc && mode==GF4_ON && !h->blank && h->cursor_visible && h->cursor_valid)
        rc=gf4_cursor_apply(ctx,head,true);
    return gf4_change_end(ctx,rc);
}
i32 gf4_blank_set(struct gf4_ctx *ctx,u8 head,bool blank,u32 dl)
{
    int rc=dp_check(ctx,head);
    if (rc) return rc;
    if (!ctx->state->head[head].enabled) return GF4_ESTATE;
    rc=gf4_change_begin(ctx,dl);
    if (rc) return rc;
    struct gf4_head_image *h=&ctx->state->next[head];
    h->blank=blank;
    rc=dp_screen(&ctx->ch->f->io,head,blank || h->dpms!=GF4_ON);
    if (!rc && !blank && h->dpms==GF4_ON && h->cursor_visible && h->cursor_valid)
        rc=gf4_cursor_apply(ctx,head,true);
    return gf4_change_end(ctx,rc);
}
