/* C1, UNIT14: real F1/F2/F3/V0 and F4 service/scan/modeset/flip.
 * Reuse the C2-service integration environment, with an actual typed normal
 * slot and the C1 publication boundary. No product API dependency stubs.
 * A2 must use the C2-service link list plus gf_present.c, -nostdlib,
 * -ffunction-sections/-fdata-sections/--gc-sections, GF_PRODUCTION_MODULE.
 * The included fixture already includes backend_geforce.c and gf_hook.c;
 * do not link either a second time. No libc, refs checkout or generated data.
 * Expected cpp/depth/pitch/base: nouveau c03c1a1 dispnv04/crtc.c:869-903;
 * pin/completion: :1074-1087,1136-1217; format-independent identity:
 * nouveau_bo.c:594-629. OS32 C1 defines the legacy INDEX8-only adapter.
 */
#define _start c1_unused_service_cases
#include "gf4_c2svc_host.c"
#undef _start

/* Storage bpp from crtc.c:869-883: depths 8/16/24 with cpp 1/2/4.
 * VGA geometry is the design's VESA fallback, not a copied product formula. */
#define C1_WIDTH 640U
#define C1_HEIGHT 480U
#define C1_INDEX_BPP 8U
#define C1_RGB_BPP 16U
#define C1_XRGB_BPP 32U
#define C1_RETRACE_HIGH (1U << 3) /* dispnv04/nvreg.h:299 */
#define C1_ARGB_OPAQUE 0xff000000U /* input format: premultiplied ARGB8888 */
static const struct gf4_test_ops *c1_normal;
static const struct gf4_test_ops c1_ops={
    .ctx=&svc_ctx,.set_mode=gf4_set_mode,.flip=gf4_flip,
    .dpms_set=gf4_dpms_set,.cursor_image=gf4_cursor_image,
    .cursor_move=gf4_cursor_move,.cursor_show=gf4_cursor_show,
    .vblank_wait=gf4_vblank_wait,.gamma=gf4_gamma,
    .scan_prepare=gf4_scan_prepare,.scan_discard=gf4_scan_discard
};
/* Select only the callbacks under test. The full module table's unused
 * init/route hooks are discarded, as in the existing C1 integration test. */
static u8 c1_ram[C1_WIDTH*C1_HEIGHT];
GfxCounters gfx_counters;
static GfxBackend c1_hal={
    .bb_base=c1_ram,.bb_pitch=C1_WIDTH,.bb_size=sizeof(c1_ram),.bb_format=GFX_BB_PACKED8,
    .query=geforce_query,.set_mode=geforce_set_mode,.dpms=geforce_dpms,
    .cursor_image=geforce_cursor_image,.cursor_move=geforce_cursor_move,
    .cursor_show=geforce_cursor_show,.vsync=geforce_vsync,
    .page_flip=geforce_page_flip,.gamma=geforce_gamma
};
int gfx_current_height,gfx_flip_enabled,gfx_display_page;
static struct geforce_platform c1_platform={.display_phys=CINT_FB_PHYS};
static const GeforceModuleContext c1_context={
    .backend=&c1_hal,.counters=&gfx_counters,.height=&gfx_current_height,
    .flip=&gfx_flip_enabled,.page=&gfx_display_page,
    .platform=&c1_platform,.display_state=&svc_state,.display_out=&c1_normal
};
const GeforceModuleContext *geforce_context=&c1_context;

static bool c1_reject_publish;
static i32 c1_admission(void *owner,bool open)
{
    struct gf4_ctx *c=owner;
    c1_normal=0;
    if (open && c1_reject_publish) {
        c1_reject_publish=false;
        return SVC_INJECT; /* One real publication-boundary failure. */
    }
    i32 rc=svc_admission(owner,open);
    if (rc || !open) return rc;
    /* C0 publishes while changing is still true: head[] is the old image. */
    const struct gf4_head_image *h=c->changing ? c->state->next : c->state->head;
    rc=gf4_scan_changed(c,0,&h[0].front);
    if (rc) return rc;
    gf_hook_scan_changed(0,&h[0].front,h[0].mode.id);
    c1_normal=&c1_ops;
    return 0;
}
static bool c1_deliver_vblank=true;
static u32 c1_fail_after_reference;
static void c1_observe(struct gf3_host *h,const struct gf3_host_event *e)
{
    svc_observe(h,e);
    /* Advance the simulated device clock in bounded steps during a stalled
     * reference. Keep the caller deadline and trace capacity unchanged. */
    if (!e->rc && e->op==H_SLEEP && cint_stall_reference)
        h->now+=GF3_TIMEOUT_US/8U;
    /* Inject only after the real PUT has submitted the fill's reference.
     * nv10_fence.c:33-41,53-55: submission precedes completion reads. */
    if (c1_fail_after_reference && cint_references==c1_fail_after_reference &&
        !e->rc && e->op==H_WRITE && e->a[1]==GF2_USER_BASE+GF2_USER_PUT) {
        c1_fail_after_reference=0;
        gf3_host_fail_at(h,H_CLOCK,0,GF1_EIO);
    }
    /* The host environment delivers one observation while foreground waits.
     * Product F1 snapshot/counter, F2 fences and B7 completion stay real. */
    if (!e->rc && e->op==H_SLEEP && c1_deliver_vblank)
        ++cint_prod.foundation.stats.vblank[0];
}
static void c1_clear_trace(void)
{
    gf3_host_trace_clear(&cint_host);
    gf4_host_trace_clear(&svc_host);
}
static void c1_expect(const struct gf3_surface *s,u32 format,u32 bpp)
{
    GFX_ScreenInfo info;
    struct video_fb fb;
    u32 width,height;
    assert(!c1_hal.query(&info));
    assert(info.width==s->width && info.height==s->height);
    assert(info.bpp==bpp && info.format==format);
    assert(c1_hal.selected_mode->width==s->width && c1_hal.selected_mode->height==s->height);
    assert(c1_hal.selected_mode->pitch==s->pitch && c1_hal.selected_mode->format==format);
    const GfxMode *target=gfx_mode_pick(&c1_hal,0);
    assert(target && target->width==s->width && target->height==s->height);
    assert(target->format==format && target->pitch==s->pitch);
    geforce_backend_framebuffer_info(&fb,&width,&height);
    assert(width==s->width && height==s->height && gfx_current_height==(int)s->height);
    assert(fb.phys==CINT_FB_PHYS+s->off && fb.bytes==s->bytes);
    assert(fb.pitch==s->pitch && fb.format==format);
    assert(!gf2_host_memcmp(&selected_front,s,sizeof(*s)));
}
static u32 c1_mode(enum gf3_fmt fmt)
{
    for (u32 i=0;i<svc_state.nmodes;i++)
        if (svc_state.modes[i].fmt==fmt) return svc_state.modes[i].id;
    assert(false);
    return 0;
}
static void c1_formats(void)
{
    const enum gf3_fmt formats[]={GF3_INDEX8,GF3_RGB565,GF3_XRGB8888,GF3_INDEX8};
    const u32 hal_formats[]={GFX_FMT_PACKED8,GFX_FMT_RGB565,GFX_FMT_XRGB8888,GFX_FMT_PACKED8};
    const u32 bpp[]={C1_INDEX_BPP,C1_RGB_BPP,C1_XRGB_BPP,C1_INDEX_BPP};
    for (u32 i=0;i<sizeof(formats)/sizeof(formats[0]);++i) {
        u32 expected_dl=cint_host.now+GF3_TIMEOUT_US;
        assert(!c1_hal.set_mode(c1_mode(formats[i])));
        struct gf3_surface front=svc_state.head[0].front;
        assert(front.fmt==formats[i] && front.width==C1_WIDTH && front.height==C1_HEIGHT);
        assert(front.pitch==C1_WIDTH*(bpp[i]/BITS_PER_BYTE));
        c1_expect(&front,hal_formats[i],bpp[i]);
        assert(svc_ctx.deadline==expected_dl);
        /* Metadata notification is pure and must ignore head B. */
        c1_clear_trace();
        struct gf3_surface other=front; other.width/=2;
        gf_hook_scan_changed(1,&other,U32_MAX);
        c1_expect(&front,hal_formats[i],bpp[i]);
        assert(!cint_host.nevents && !svc_host.nevents);
        /* Even direct rebinds cannot reopen the old INDEX8 adapter on 16/32. */
        gf_hook_scan_bind(true,&front);
        if (formats[i]==GF3_INDEX8) {
            assert(c1_hal.fill_rect==geforce_fill_rect && c1_hal.blit==geforce_blit);
            assert(!geforce_fill_rect(front.width,0,1,1,0));
        } else {
            assert(!c1_hal.fill_rect && !c1_hal.blit);
            assert(geforce_fill_rect(0,0,1,1,0)==OS32_ERR_NOSYS);
            assert(geforce_blit(0,0,0,0,1,1)==OS32_ERR_NOSYS);
            assert(geforce_transfer(0,0,1,1)==OS32_ERR_INVAL);
            assert(svc_ctx.last_rc==GF4_EFORMAT);
        }
        assert(!cint_host.nevents && !svc_host.nevents);
        if (formats[i]==GF3_INDEX8) {
            assert(front.off); /* A dynamic scan is outside the boot CPU map. */
            assert(!geforce_transfer(1,2,3,4));
            assert(svc_ctx.last_rc==0 && cint_host.nevents);
        }
        assert(!gf4_scan_discard(&svc_ctx,0,cint_deadline()));
    }
}
static void c1_geometry(void)
{
    /* VESA DMT XGA 60 Hz, same independently named input as C2-scan.
     * Synthetic mode IDs are fixture tokens; geometry is not derived from
     * either C1 metadata or C2 placement. crtc.c:869-903 defines cpp/pitch. */
    const u32 id=U32_MAX-1U;
    struct gf4_mode xga={.id=id,
        .t={65000,1024,1048,1184,1344,768,771,777,806,GF4_NEG_H|GF4_NEG_V},
        .fmt=GF3_XRGB8888,.pitch=1024U*(C1_XRGB_BPP/BITS_PER_BYTE)};
    u32 index=svc_state.nmodes++;
    svc_state.modes[index]=xga;
    svc_state.mode_connector[index]=svc_state.head[0].route.connector;
    assert(!c1_hal.set_mode(id));
    assert(svc_state.head[0].front.width==1024 && svc_state.head[0].front.height==768);
    c1_expect(&svc_state.head[0].front,GFX_FMT_XRGB8888,C1_XRGB_BPP);
    assert(!gf4_scan_discard(&svc_ctx,0,cint_deadline()));
    /* INDEX8 acceleration supports the new geometry; legacy RAM present is
     * still VGA-only and must reject this mode before touching either BAR. */
    xga.id=id-1U; xga.fmt=GF3_INDEX8; xga.pitch=1024;
    index=svc_state.nmodes++;
    svc_state.modes[index]=xga;
    svc_state.mode_connector[index]=svc_state.head[0].route.connector;
    assert(!c1_hal.set_mode(xga.id));
    c1_expect(&svc_state.head[0].front,GFX_FMT_PACKED8,C1_INDEX_BPP);
    assert(c1_hal.fill_rect==geforce_fill_rect);
    c1_clear_trace();
    assert(geforce_transfer(0,0,1,1)==OS32_ERR_INVAL);
    assert(svc_ctx.last_rc==GF4_EFORMAT && !cint_host.nevents && !svc_host.nevents);
    assert(!gf4_scan_discard(&svc_ctx,0,cint_deadline()));
}
static struct GfxScanout c1_scanout(const struct gf3_surface *s,u32 format)
{
    return (struct GfxScanout){s->reservation_id,s->off,s->bytes,s->pitch,
        s->width,s->height,format};
}
static void c1_flips(void)
{
    assert(!c1_hal.set_mode(c1_mode(GF3_XRGB8888)));
    assert(!gf4_scan_discard(&svc_ctx,0,cint_deadline()));
    struct gf3_surface first=svc_state.head[0].front;
    struct gf3_surface pair[2];
    assert(!gf4_scan_prepare(&svc_ctx,0,c1_mode(GF3_XRGB8888),pair,cint_deadline()));
    struct GfxScanout out=c1_scanout(&pair[0],GFX_FMT_XRGB8888);
    /* Failed lookup/range must neither submit nor change metadata/ownership. */
    struct GfxScanout bad=out; bad.reservation_id=U32_MAX;
    c1_clear_trace();
    assert(c1_hal.page_flip(0,&bad,0,cint_deadline())==OS32_ERR_IO);
    assert(svc_ctx.last_rc==GF3_ESTATE && !svc_ctx.hold[0].pending);
    bad=out; bad.offset=U32_MAX;
    assert(c1_hal.page_flip(0,&bad,0,cint_deadline())==OS32_ERR_INVAL);
    assert(svc_ctx.last_rc==GF3_ERANGE);
    bad=out; bad.format=GFX_FMT_PLANAR4;
    assert(c1_hal.page_flip(0,&bad,0,cint_deadline())==OS32_ERR_INVAL);
    c1_expect(&first,GFX_FMT_XRGB8888,C1_XRGB_BPP);
    assert(!cint_host.nevents && !svc_host.nevents);
    /* Sync=false returns with both faces held. Metadata follows B7's
     * subsequent VBLANK completion, never the successful base submission. */
    u32 dl=cint_deadline(),seen;
    assert(!c1_hal.page_flip(0,&out,0,dl));
    assert(svc_ctx.hold[0].pending);
    c1_expect(&first,GFX_FMT_XRGB8888,C1_XRGB_BPP);
    svc_keys_live(&first); svc_keys_live(&pair[0]);
    assert(!c1_hal.vsync(0,svc_ctx.hold[0].after,dl,&seen));
    c1_expect(&pair[0],GFX_FMT_XRGB8888,C1_XRGB_BPP);
    assert(!svc_ctx.hold[0].pending);
    svc_keys_live(&first); svc_keys_live(&pair[0]);
    /* The input is copied; modifying the HAL descriptor cannot alter scan. */
    out.offset=U32_MAX;
    c1_expect(&pair[0],GFX_FMT_XRGB8888,C1_XRGB_BPP);
    out=c1_scanout(&pair[1],GFX_FMT_XRGB8888);
    const struct gf4_host_read high={0,GF4_HOST_STATUS,1,C1_RETRACE_HIGH};
    gf4_host_reads(&svc_host,&high,1);
    assert(!c1_hal.page_flip(0,&out,1,cint_deadline()));
    assert(svc_host.next_read==1);
    gf4_host_reads(&svc_host,0,0);
    c1_expect(&pair[1],GFX_FMT_XRGB8888,C1_XRGB_BPP);
    /* An expired caller deadline stays expired and leaves the current scan. */
    out=c1_scanout(&pair[0],GFX_FMT_XRGB8888);
    assert(c1_hal.page_flip(0,&out,1,cint_host.now)==OS32_ERR_IO);
    c1_expect(&pair[1],GFX_FMT_XRGB8888,C1_XRGB_BPP);
    svc_keys_live(&pair[0]); svc_keys_live(&pair[1]);
    assert(!gf4_scan_discard(&svc_ctx,0,cint_deadline()));
}
static void c1_failures(void)
{
    struct gf3_surface before=svc_state.head[0].front;
    c1_clear_trace();
    assert(c1_hal.set_mode(U32_MAX)==OS32_ERR_INVAL && svc_ctx.last_rc==GF4_EINVAL);
    assert(!cint_host.nevents && !svc_host.nevents);
    /* Fail before rendering. Same confirmed descriptor and reservation survive. */
    gf3_host_fail_at(&cint_host,H_CLOCK,1,GF1_EIO);
    assert(c1_hal.set_mode(c1_mode(GF3_RGB565))==OS32_ERR_IO);
    assert(svc_ctx.last_rc==GF1_EIO && !svc_ctx.scan_next[0][0].value.owner);
    gf3_host_fail_at(&cint_host,H_CLOCK,0,0);
    c1_expect(&before,GFX_FMT_XRGB8888,C1_XRGB_BPP);
    /* Runtime rejection during render: newly reserved bank is discarded. */
    gf3_host_fail_at(&cint_host,H_CLOCK,2,GF1_EIO);
    assert(c1_hal.set_mode(c1_mode(GF3_RGB565))==OS32_ERR_IO);
    assert(svc_ctx.last_rc==GF1_EIO);
    const struct gf_vram_reservation_node *unused=svc_ctx.scan_bank[0] ?
        svc_ctx.scan[0] : svc_ctx.scan_next[0];
    assert(!unused[0].value.owner && !unused[1].value.owner);
    gf3_host_fail_at(&cint_host,H_CLOCK,0,0);
    c1_expect(&before,GFX_FMT_XRGB8888,C1_XRGB_BPP);
    svc_keys_live(&before);
    /* Fail commit publication once. Real C0 rolls the hardware back and
     * republishes the old image while changing=true; C1 must follow it. */
    c1_reject_publish=true;
    assert(c1_hal.set_mode(c1_mode(GF3_RGB565))==OS32_ERR_IO);
    assert(svc_ctx.last_rc==SVC_INJECT && !c1_reject_publish);
    c1_expect(&before,GFX_FMT_XRGB8888,C1_XRGB_BPP);
    svc_keys_live(&before);
    c1_normal=0; c1_clear_trace();
    assert(c1_hal.set_mode(0)==OS32_ERR_NOSYS);
    assert(c1_hal.page_flip(0,0,0,0)==OS32_ERR_NOSYS);
    assert(c1_hal.dpms(0,GF4_ON)==OS32_ERR_NOSYS);
    assert(c1_hal.cursor_image(0,0)==OS32_ERR_NOSYS);
    assert(c1_hal.cursor_move(0,0,0)==OS32_ERR_NOSYS);
    assert(c1_hal.cursor_show(0,1)==OS32_ERR_NOSYS);
    assert(c1_hal.vsync(0,0,0,0)==OS32_ERR_NOSYS);
    assert(c1_hal.gamma(0,0)==OS32_ERR_NOSYS);
    assert(!cint_host.nevents && !svc_host.nevents);
    c1_normal=&c1_ops;
}
static void c1_other_hooks(void)
{
    assert(c1_hal.dpms(0,U32_MAX)==OS32_ERR_INVAL);
    assert(!c1_hal.dpms(0,GF4_OFF));
    u32 seen=U32_MAX;
    assert(c1_hal.vsync(0,0,cint_deadline(),&seen)==OS32_ERR_IO && seen==U32_MAX);
    assert(!c1_hal.dpms(0,GF4_ON));
    u32 after=cint_prod.foundation.stats.vblank[0],dl=cint_deadline();
    assert(!c1_hal.vsync(0,after,dl,&seen) && seen!=after);
    assert(c1_hal.cursor_image(0,0)==OS32_ERR_INVAL);
    assert(c1_hal.cursor_show(0,2)==OS32_ERR_INVAL);
    assert(!c1_hal.cursor_move(0,-7,9));
    assert(svc_state.head[0].x==-7 && svc_state.head[0].y==9);
    assert(!c1_hal.cursor_show(0,0));
    /* Zero dimensions are rejected by the real image entry after conversion;
     * no fabricated VRAM mapping is needed for this input-contract check. */
    static const u32 pixel=C1_ARGB_OPAQUE;
    const struct GfxCursorImage image={&pixel,sizeof(pixel),sizeof(pixel),0,1,-2,3};
    assert(c1_hal.cursor_image(0,&image)==OS32_ERR_INVAL);
    static u16 gamma[3][VIDEO_PALETTE_ENTRIES];
    for (u32 c=0;c<3;c++) for (u32 i=0;i<VIDEO_PALETTE_ENTRIES;i++)
        gamma[c][i]=(u16)(i*257U); /* identity u16 ramp, crtc.c:778-797 */
    assert(!c1_hal.gamma(0,gamma));
    assert(!gf2_host_memcmp(svc_state.head[0].gamma,gamma,sizeof(gamma)));
}
static void c1_start(void)
{
    svc_fixture();
    svc_links.admission=c1_admission;
    svc_links.scan_changed=gf4_scan_changed;
    cint_host.observe=c1_observe;
    assert(!gf4_service_start(&svc_ctx,cint_deadline()));
    assert(c1_normal==&c1_ops && c1_hal.mode_count==svc_state.nmodes);
}
/* C2 scan banks contain a front/back pair, per gf4_scan_prepare's API.
 * These are value snapshots, never copies of the resident registry nodes. */
static struct gf_vram_reservation c1_retained[2];
static u32 c1_failed_calls;
static i32 c1_failed_stop(void *owner,i32 cause,u32 dl)
{
    struct gf4_ctx *c=owner;
    assert(c==&svc_ctx && !c->reusable && !c->accepting);
    assert(dl==c->deadline);
    ++c1_failed_calls;
    const struct gf_vram_reservation_node *nodes=c->scan_bank[0] ? c->scan[0] : c->scan_next[0];
    for (u32 i=0;i<sizeof(c1_retained)/sizeof(c1_retained[0]);++i) {
        assert(nodes[i].value.owner==&nodes[i]);
        c1_retained[i]=nodes[i].value;
    }
    /* Deny F2's stop writes, so the real service cannot confirm OFF.
     * nouveau_drm.c:955-988 orders stop before ownership release. */
    gf3_host_fail_at(&cint_host,H_WRITE,0,GF1_EIO);
    return svc_failed(owner,cause,dl);
}
static void c1_render_failures(void)
{
    const i32 errors[]={GF2_ETIME,GF2_EIO};
    for (u32 n=0;n<sizeof(errors)/sizeof(errors[0]);++n) {
        c1_start();
        struct gf3_surface before=svc_state.head[0].front;
        u8 bank=svc_ctx.scan_bank[0];
        u32 opens=svc_opens,dl=cint_host.now+GF3_TIMEOUT_US;
        c1_failed_calls=0;
        svc_links.failed=c1_failed_stop;
        /* Next emitted reference belongs to fill; stall its completion.
         * Count is derived from the live consumer, not a packet ordinal. */
        u32 submitted=cint_references+1U;
        cint_stall_reference=submitted;
        if (errors[n]==GF2_EIO) c1_fail_after_reference=submitted;
        c1_clear_trace();
        assert(c1_hal.set_mode(c1_mode(GF3_RGB565))==OS32_ERR_IO);
        assert(cint_references==submitted && cint_reference!=cint_prod.chan.seq);
        assert(c1_failed_calls==1 && svc_ctx.last_rc==errors[n]);
        /* service_stop refreshes stage budgets after a render timeout.
         * Injected clock failure leaves the original deadline intact. */
        assert(svc_ctx.deadline==(errors[n]==GF2_ETIME ?
               cint_host.now+GF2_TIMEOUT_US : dl));
        assert(svc_ctx.recovery_rc);
        assert(cint_prod.chan.state!=GF2_OFF && !c1_normal);
        assert(!svc_ctx.reusable && !svc_ctx.accepting);
        assert(svc_opens==opens && svc_ctx.scan_bank[0]==bank);
        c1_expect(&before,GFX_FMT_PACKED8,C1_INDEX_BPP);
        svc_keys_live(&before);
        for (u32 i=0;i<sizeof(c1_retained)/sizeof(c1_retained[0]);++i) {
            struct gf_vram_reservation key;
            const struct gf_vram_reservation *v=&c1_retained[i];
            assert(!gf_vram_match(svc_ctx.ch,v->owner,v->reservation_id,
                v->generation,v->off,v->bytes,&key));
        }
        /* A later HAL call or direct prepare cannot recycle these keys. */
        assert(c1_hal.set_mode(c1_mode(GF3_RGB565))==OS32_ERR_NOSYS);
        struct gf3_surface pair[2];
        assert(gf4_scan_prepare(&svc_ctx,0,c1_mode(GF3_RGB565),pair,dl)==GF4_ESTATE);
        cint_stall_reference=0;
        svc_finish(); /* Clear injection; confirmed stop now releases all. */
        assert(!c1_normal && !hook_active);
    }
    /* A one-shot wait I/O failure can drain with the original deadline:
     * only that confirmed completion permits normal candidate discard. */
    c1_start();
    u32 submitted=cint_references+1U;
    cint_stall_reference=submitted;
    c1_clear_trace();
    gf3_host_fail_at(&cint_host,H_SLEEP,1,GF1_EIO);
    assert(c1_hal.set_mode(c1_mode(GF3_RGB565))==OS32_ERR_IO);
    assert(svc_ctx.last_rc==GF2_EIO && c1_normal && svc_ctx.reusable);
    assert(cint_references>submitted && cint_reference==cint_prod.chan.seq);
    const struct gf_vram_reservation_node *unused=svc_ctx.scan_bank[0] ? svc_ctx.scan[0] : svc_ctx.scan_next[0];
    assert(!unused[0].value.owner && !unused[1].value.owner);
    cint_stall_reference=0;
    svc_finish();
}
#ifndef GF4_C1_ENTRY
#define GF4_C1_ENTRY _start
#endif
void GF4_C1_ENTRY(void)
{
    c1_start();
    c1_expect(&svc_state.head[0].front,GFX_FMT_PACKED8,C1_INDEX_BPP);
    c1_formats(); c1_geometry(); c1_flips(); c1_failures(); c1_other_hooks();
    svc_finish();
    assert(!c1_normal && !hook_active);
    c1_render_failures();
    gf2_host_exit(0);
}
