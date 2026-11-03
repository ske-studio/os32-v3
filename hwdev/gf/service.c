/* GF video operations. Resource/ROM/A2 ownership is supplied by P4. */
#include "internal.h"
#include "command.h"
/* Layer 3 lifecycle: nouveau (c03c1a1), nvkm/engine/fifo/uchan.c:272-298
 * and nv04.c:41-99. OS32 uses one cooperative production owner, explicit
 * recovery and resident VRAM nodes instead of BO/TTM pinning
 * (nouveau_bo.c:594-599,618-629). No hardware methods are added here. */
#include "accel_service.h"
#include "production.h"

static struct gf3_service accel_service;
/* GF_FEAT3_DESIGN N2 and C1 geometry addendum: page reservations, 64B pitch. */
#define GF3_TEST_PITCH_ALIGN 64U

static i32 service_result(struct gf3_service *s, i32 rc)
{
    if (s && s->production && rc) s->production->accel.last_rc = rc;
    return rc;
}

static i32 service_check(struct gf3_service *s)
{
    if (!s) return GF3_EINVAL;
    return s == &accel_service && s->production && s->slot ? 0 : GF3_ESTATE;
}

i32 gf3_service_bind(struct gf_production *p, const struct gf3_test_ops **slot)
{
    if (!p || !slot) return GF3_EINVAL;
    struct gf3_service *s = &accel_service;
    if (s->production)
        return service_result(s, s->production == p && s->slot == slot ? 0 : GF3_ESTATE);
    s->production = p;
    s->slot = slot;
    *slot = 0;
    return 0;
}

struct gf3_service *gf3_service_get(struct gf2_chan *ch)
{
    return accel_service.production && ch == &accel_service.production->chan ?
           &accel_service : 0;
}

static struct gf3_surface service_surface(const struct gf_vram_reservation_node *n,
                                          u32 pitch, u32 width, u32 height)
{
    return (struct gf3_surface){
        .off = n->value.off, .bytes = n->value.bytes, .pitch = pitch,
        .width = width, .height = height, .fmt = GF3_INDEX8,
        .reservation_id = n->value.reservation_id,
        .generation = n->value.generation
    };
}

/* Reuse live nodes on an ordinary failed-start retry; recovery explicitly
 * releases them first. The registry/ID issuer is never reset. */
static i32 service_reserve(struct gf3_service *s)
{
    struct gf2_chan *ch = &s->production->chan;
    u32 mask = MEM_PAGE_SIZE - 1U;
    i32 rc = gf_vram_registry_init(ch);
    if (rc) return rc;
    if (!s->scan_pitch || !s->scan_width || !s->scan_height ||
        s->scan_width > s->scan_pitch || s->scan_height > U32_MAX / s->scan_pitch ||
        s->scan_phys < ch->bar1.base) return GF3_ERANGE;
    u32 off = s->scan_phys - ch->bar1.base;
    u32 bytes = s->scan_pitch * s->scan_height;
    if (off > ch->bar1.length || bytes > ch->bar1.length - off ||
        off > U32_MAX - bytes) return GF3_ERANGE;
    if (!s->scan_node.value.owner) {
        rc = gf_vram_reserve(ch, &s->scan_node, off, bytes, &s->scan_node);
        if (rc) return rc;
    } else if (s->scan_node.value.owner != &s->scan_node ||
               s->scan_node.value.off != off || s->scan_node.value.bytes != bytes)
        return GF3_ESTATE;
    s->scan = service_surface(&s->scan_node, s->scan_pitch, s->scan_width, s->scan_height);
    u32 end = off + bytes;
    if (end > U32_MAX - mask || s->scan_pitch > U32_MAX - (GF3_TEST_PITCH_ALIGN - 1U))
        return GF3_ERANGE;
    u32 start = (end + mask) & ~mask;
    u32 limit = ch->B & ~mask;
    u32 pitch = (s->scan_pitch + GF3_TEST_PITCH_ALIGN - 1U) & ~(GF3_TEST_PITCH_ALIGN - 1U);
    if (start >= limit || (limit - start) / pitch == 0) return GF2_ENOSPC;
    u32 height = (limit - start) / pitch;
    if (height > s->scan_height) height = s->scan_height;
    if (!s->test_node.value.owner) {
        rc = gf_vram_reserve(ch, &s->test_node, start, limit - start, &s->test_node);
        if (rc) return rc;
    } else if (s->test_node.value.owner != &s->test_node ||
               s->test_node.value.off != start || s->test_node.value.bytes != limit - start)
        return GF3_ESTATE;
    s->test = service_surface(&s->test_node, pitch, s->scan_width, height);
    return 0;
}

static i32 service_publish(struct gf3_service *s, u32 dl)
{
    struct gf_production *p = s->production;
    i32 reserved = service_reserve(s);
    /* No test capacity is nonfatal to display: initialize the scan hook,
     * leave dispatch closed and report ENOSPC to the start caller. */
    if (reserved && (reserved != GF2_ENOSPC || !s->scan_node.value.owner ||
                     s->test_node.value.owner)) return service_result(s, reserved);
    i32 rc = gf3_init(&p->accel, &p->chan, dl);
    if (rc) return service_result(s, rc);
    gf_hook_ctx_bind(&p->accel);
    gf_hook_scan_bind(true, &s->scan);
    if (!reserved) {
        s->ops = (struct gf3_test_ops){
            .ctx = &p->accel, .test_surface = &s->test,
            .fill = gf3_fill, .copy = gf3_copy, .mono = gf3_mono,
            .ifc = gf3_ifc, .upload = gf3_upload, .download = gf3_download,
            .scale = gf3_scale, .present_m2mf = gf3_present_m2mf,
            .map = gf_vram_view_map, .unmap = gf_vram_view_unmap
        };
        *s->slot = &s->ops;
    }
    return service_result(s, reserved);
}

i32 gf3_service_start(struct gf3_service *s, u32 dl)
{
    i32 rc = service_check(s);
    if (rc) return service_result(s, rc);
    struct gf_production *p = s->production;
    if (p->accel.ready || p->chan.state != GF2_RUN || !p->chan.epoch ||
        !p->chan.f || p->chan.fault.latched ||
        (p->accel.ch && p->accel.ch != &p->chan))
        return service_result(s, GF3_ESTATE);
    if (!s->scan_pitch) {
        struct video_fb fb = {0};
        u32 width = 0, height = 0;
        geforce_backend_framebuffer_info(&fb, &width, &height);
        /* F3: fetch only during the initial video_start; diagnostic START
         * after STOP borrows the saved geometry, just as recover does.
         * Layer 3 retains the packed8 display contract (section 9). */
        if (fb.format != GFX_FMT_PACKED8) return service_result(s, GF3_EFORMAT);
        s->scan_pitch = fb.pitch; s->scan_width = width;
        s->scan_height = height; s->scan_phys = fb.phys;
    }
    return service_publish(s, dl);
}

i32 gf3_service_close(struct gf3_service *s)
{
    i32 rc = service_check(s);
    if (rc) return service_result(s, rc);
    *s->slot = 0;
    gf_hook_scan_bind(false, 0);
    return service_result(s, gf_vram_views_close(&s->production->chan));
}

i32 gf3_service_fini(struct gf3_service *s, u32 dl)
{
    i32 rc = service_check(s);
    if (rc) return service_result(s, rc);
    struct gf_production *p = s->production;
    if (!p->accel.ready || p->chan.state != GF2_RUN || p->chan.fault.latched ||
        p->accel.epoch != p->chan.epoch) return 0;
    return service_result(s, gf3_fini(&p->accel, dl));
}

void gf3_service_invalidate(struct gf3_service *s)
{
    if (!s || !s->production) return;
    struct gf3_ctx *c = &s->production->accel;
    for (u32 i = 0; i < GF3_OBJECT_COUNT; ++i) c->obj[i] = (struct gf2_object){0};
    c->notify = (struct gf2_object){0};
    c->notify_storage = (struct gf_inst){0};
    c->notify_off = c->epoch = 0;
    c->ready = false;
}

i32 gf3_service_release(struct gf3_service *s)
{
    i32 rc = service_check(s);
    if (rc) return service_result(s, rc);
    struct gf2_chan *ch = &s->production->chan;
    if (s->test_node.value.owner) {
        rc = gf_vram_release(ch, &s->test_node.value);
        if (rc) return service_result(s, rc);
        s->test = (struct gf3_surface){0};
    }
    if (s->scan_node.value.owner) {
        rc = gf_vram_release(ch, &s->scan_node.value);
        if (rc) return service_result(s, rc);
        s->scan = (struct gf3_surface){0};
    }
    return 0;
}

i32 gf3_service_stop(struct gf3_service *s)
{
    i32 first = gf3_service_close(s);
    if (service_check(s)) return first;
    struct gf_production *p = s->production;
    if (first && !p->foundation.revoked) return first;
    u32 now;
    i32 rc;
    if (!p->chan.f || !p->chan.f->io.timing.clock_us ||
        p->chan.f->io.timing.clock_us(p->chan.f->io.opaque, &now))
        rc = GF2_EIO;
    else rc = gf3_service_fini(s, now + GF3_TIMEOUT_US);
    if (!first) first = rc;
    rc = gf2_stop(&p->chan);
    if (!first) first = rc;
    if (p->chan.state == GF2_OFF) gf3_service_invalidate(s);
    else if (!first) first = GF2_ESTOP;
    return service_result(s, first);
}

i32 gf3_service_recover(struct gf3_service *s, u32 dl)
{
    i32 rc = gf3_service_close(s);
    if (service_check(s)) return rc;
    struct gf_production *p = s->production;
    if (rc && !p->foundation.revoked) return rc;
    i32 first = rc;
    rc = gf2_stop(&p->chan);
    if (p->chan.state != GF2_OFF)
        return service_result(s, first ? first : (rc ? rc : GF2_ESTOP));
    rc = gf2_recover(&p->chan);
    /* Old F2 was confirmed OFF, even if new F2 failed to start. */
    gf3_service_invalidate(s);
    if (rc || first) return service_result(s, first ? first : rc);
    if (!p->display.engaged) return 0;
    rc = gf3_service_release(s);
    if (rc) return service_result(s, rc);
    return service_publish(s, dl);
}

static int gf_failure(struct gf_display_context *c)
{
    u32 section=c->section,ordinal=c->ordinal,target=c->target;
    int rc=c->last_rc ? c->last_rc : -1;
    c->reusable=false;
    gf_display_stop(c);
    c->section=section;c->ordinal=ordinal;c->target=target;c->last_rc=rc;
    return -1;
}
static int gf_start(void *v, u32 mode_id, const u8 *rgb)
{
    struct gf_display_context *c=v;
    struct video_mode mode;
    if (!c || !rgb || gf_display_mode_info(c,0,&mode) || mode_id!=mode.id)
        return OS32_ERR_INVAL;
    if (!c->reusable) return -1;
    if (c->bars[GF_MODE_VRAM_BAR].length<GF_MODE_VRAM_BYTES) return OS32_ERR_INVAL;
    for (u32 i=0;i<VIDEO_PALETTE_BYTES;i++) c->palette[i]=rgb[i];
    c->engaged=true;c->stop_ok=false;
    if (!c->initialized) {
        /* acquire supplies borrowed init/program and limits; no retained ROM. */
        c->section=GF_DISPLAY_INIT;
        int rc=c->owner->acquire(c);
        if (!rc) rc=gf_init_run(c->init,c->program);
        if (!rc) {c->section=GF_DISPLAY_BASELINE;rc=gf_display_baseline(c);}
        c->owner->release(c);
        c->init=0;c->program=0;
        if (rc) goto fail;
        c->initialized=true;
    } else if (c->owner->validate(c)) goto fail;
    c->section=GF_DISPLAY_CALC;
    if (gfinit_mode_calc(&c->limits,&c->baseline,&c->mode) ||
        !gfinit_mode_pll_ok(&c->mode) || gf_display_modeset(c)) goto fail;
    c->section=GF_DISPLAY_CHECK;
    if (gf_display_checks(c) || c->owner->foundation_start(c)) goto fail;
    struct gf_foundation *f=0;
    struct gf2_chan *ch=c->owner->command_get ? c->owner->command_get(c,&f) : 0;
    int rc;
    if (ch && (rc=gf2_start(ch,f))) {c->last_rc=rc;goto fail;}
    struct gf3_service *service = gf3_service_get(ch);
    if (service) {
        u32 now;
        if (!f || !f->io.timing.clock_us || f->io.timing.clock_us(f->io.opaque,&now)) {
            c->last_rc=service_result(service,GF2_EIO);goto fail;
        }
        rc=gf3_service_start(service,now+GF3_TIMEOUT_US);
        if (rc && !(rc==GF2_ENOSPC && service->production->accel.ready)) {
            c->last_rc=rc;goto fail;
        }
    }
    c->active=true;c->blank=false;c->last_rc=0;
    return 0;
fail:
    return gf_failure(c);
}
static int gf_palette(void *v, u32 first, u32 count, const u8 *rgb)
{
    struct gf_display_context *c=v;
    if (!c || !rgb || first>=VIDEO_PALETTE_ENTRIES || !count || count>VIDEO_PALETTE_ENTRIES-first)
        return OS32_ERR_INVAL;
    if (!c->active) return -1;
    c->section=GF_DISPLAY_PALETTE;
    int rc=gf_display_palette(c,first,count,rgb);
    if (rc) return gf_failure(c);
    return rc;
}
static int gf_blank(void *v, int blank)
{
    struct gf_display_context *c=v;
    if (!c || (blank!=0 && blank!=1)) return OS32_ERR_INVAL;
    if (c->blank==(bool)blank) return 0; /* same value: no I/O, also after stop */
    if (!c->active) return -1;
    c->section=GF_DISPLAY_BLANK;
    int rc=gf_display_blank(c,blank);
    if (rc) return gf_failure(c);
    return rc;
}
const struct video_ops gf_video_ops={
    gf_display_query,gf_display_mode_info,gf_start,gf_display_framebuffer,
    gf_palette,gf_display_start,gf_blank,gf_display_stop
};
/* Generic internal facade. Tables contain eight non-NULL entries (P0). */
int video_query(struct video_device *d, struct video_info *out)
{
    if (out) *out=(struct video_info){0};
    return d ? d->ops->query(d->context,out) : OS32_ERR_INVAL;
}
int video_mode_info(struct video_device *d, u32 i, struct video_mode *out)
{
    if (out) *out=(struct video_mode){0};
    return d ? d->ops->mode_info(d->context,i,out) : OS32_ERR_INVAL;
}
int video_framebuffer(struct video_device *d, struct video_fb *out)
{
    if (out) *out=(struct video_fb){0};
    return d ? d->ops->framebuffer(d->context,out) : OS32_ERR_INVAL;
}
int video_start(struct video_device *d, u32 id, const u8 *rgb)
{ return d ? d->ops->start(d->context,id,rgb) : OS32_ERR_INVAL; }
int video_palette(struct video_device *d, u32 first, u32 count, const u8 *rgb)
{ return d ? d->ops->palette(d->context,first,count,rgb) : OS32_ERR_INVAL; }
int video_display_start(struct video_device *d, u32 offset)
{ return d ? d->ops->display_start(d->context,offset) : OS32_ERR_INVAL; }
int video_blank(struct video_device *d, int blank)
{ return d ? d->ops->blank(d->context,blank) : OS32_ERR_INVAL; }
int video_stop(struct video_device *d)
{ return d ? d->ops->stop(d->context) : OS32_ERR_INVAL; }
