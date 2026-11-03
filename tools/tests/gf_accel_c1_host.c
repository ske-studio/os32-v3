/* C1 lifecycle oracle. A1 UNIT=12 supplies tracing/runtime without component
 * stubs: service lifecycle dependencies are modeled below; V0 is real.
 * Build this TU alone (include_flags from kernel_includes.py), no libc.
 * GF3_SERVICE_SOURCE may select a mutated service.c. No external refs at run.
 * Model: GF_FEAT3_DESIGN N2, F1-F7 and C1 test geometry addendum.
 * This is the service/registry boundary test; PM's F1/F2 fake-MMIO integration
 * remains a separate gate. No hardware completion is inferred here. */
#define GF3_HOST_UNIT 12
#include "gf_accel_host.h"
#include "../../hwdev/gf/accel_service.h"
#include "../../hwdev/gf/production.h"

/* Keep the real registry, including its monotonic ID/generation history.
 * Wrappers inject failures at ownership boundaries without replacing V0. */
#define gf_vram_registry_init real_registry_init
#define gf_vram_reserve real_reserve
#define gf_vram_release real_release
#define gf_vram_views_close real_views_close
#include "../../hwdev/gf/vram_view.c"
#undef gf_vram_registry_init
#undef gf_vram_reserve
#undef gf_vram_release
#undef gf_vram_views_close
#ifndef GF3_SERVICE_SOURCE
#define GF3_SERVICE_SOURCE "../../hwdev/gf/service.c"
#endif
#include GF3_SERVICE_SOURCE

static struct gf3_host host;
/* At most one lifecycle call per boundary, plus its close/hook/clock events;
 * this budget also covers a failed attempt followed by a successful retry. */
static struct gf3_host_event events[H_OP_COUNT * 8U];
static struct gf_production prod, other;
static const struct gf3_test_ops *slot, *other_slot;
static struct gf3_service *service;
static struct gf3_ctx *hook_ctx;
static struct gf3_surface hook_scan;
static bool hook_active, stop_off;
static u32 deadline, framebuffer_calls;
static struct video_fb framebuffer;
static u32 fb_width, fb_height;
static struct gf_vram_view view;
static volatile u8 *loan;
static u8 window[MEM_PAGE_SIZE];
static int unmap_rc;

/* Independent concrete fixture: 641-byte scan rows round to 704 test rows;
 * [123,2687) ends in the first page. Test range is [4096,20480), not
 * [2688,...). B is intentionally unaligned. */
#define FB_PHYS 0x30000000U
#define SCAN_OFF 123U
#define SCAN_PITCH 641U
#define SCAN_HEIGHT 4U
#define TEST_START 4096U
#define TEST_END 20480U
#define TEST_PITCH 704U
#define TEST_DEADLINE 123456U
#define WINDOW_START 0x4c000U /* first full page after a shifted scan */

static struct gf3_host_event *event(enum gf3_host_op op)
{ return gf3_host_event_begin(&host, op); }
static i32 done(struct gf3_host_event *e)
{ return gf3_host_event_end(&host, e); }
int gf_vram_registry_init(struct gf2_chan *ch)
{
    struct gf3_host_event *e = event(H_PAUSE);
    assert(ch == &prod.chan && !slot && !hook_active);
    if (!e->rc) e->rc = real_registry_init(ch);
    return done(e);
}
int gf_vram_reserve(struct gf2_chan *ch, const void *owner, u32 off, u32 bytes,
                    struct gf_vram_reservation_node *node)
{
    struct gf3_host_event *e = event(H_ALLOC);
    assert(ch == &prod.chan && owner == node && !slot && !hook_active);
    assert(node == &service->scan_node || node == &service->test_node);
    if (!e->rc) e->rc = real_reserve(ch, owner, off, bytes, node);
    return done(e);
}
int gf_vram_release(struct gf2_chan *ch, const struct gf_vram_reservation *key)
{
    struct gf3_host_event *e = event(H_FREE);
    assert(ch == &prod.chan && !slot && !hook_active);
    /* Test goes first; scan cannot be released while a test node is live. */
    if (key == &service->scan_node.value) assert(!service->test_node.value.owner);
    if (!e->rc) e->rc = real_release(ch, key);
    return done(e);
}
i32 gf_vram_views_close(struct gf2_chan *ch)
{
    struct gf3_host_event *e = event(H_STOP);
    assert(ch == &prod.chan && !slot && !hook_active);
    if (!e->rc) e->rc = real_views_close(ch);
    return done(e);
}
int gf3_init(struct gf3_ctx *c, struct gf2_chan *ch, u32 dl)
{
    struct gf3_host_event *e = event(H_GR_NEW);
    assert(c == &prod.accel && ch == &prod.chan && dl == deadline);
    assert(!slot && !hook_active && !c->ready && service->scan_node.value.owner);
    c->ch = ch; c->epoch = ch->epoch;
    /* Simulate partial live descriptors even on failed initialization. */
    c->obj[0].handle = GF3_HANDLE_NULL;
    c->notify_storage.size = GF3_NOTIFY_BYTES;
    c->notify.handle = GF3_HANDLE_NOTIFY;
    c->notify_off = 100U;
    if (!e->rc) c->ready = true;
    c->last_rc = e->rc;
    return done(e);
}
int gf3_fini(struct gf3_ctx *c, u32 dl)
{
    struct gf3_host_event *e = event(H_DEL);
    assert(c == &prod.accel && c->ready && !slot && !hook_active && !view.mapped);
    assert(dl == deadline);
    c->ready = false;
    return done(e);
}
int gf2_start(struct gf2_chan *ch, struct gf_foundation *f)
{
    struct gf3_host_event *e = event(H_DMA_NEW);
    assert(ch == &prod.chan && f == &prod.foundation);
    if (!e->rc) { ch->state = GF2_RUN; ch->f = f; ++ch->epoch; }
    return done(e);
}
int gf2_stop(struct gf2_chan *ch)
{
    struct gf3_host_event *e = event(H_FENCE);
    assert(ch == &prod.chan && !slot && !hook_active);
    assert(!view.mapped || prod.foundation.revoked);
    if (stop_off) ch->state = GF2_OFF;
    else ch->state = GF2_FAULT;
    return done(e);
}
int gf2_recover(struct gf2_chan *ch)
{
    struct gf3_host_event *e = event(H_RESUME);
    assert(ch == &prod.chan && ch->state == GF2_OFF && !slot && !hook_active);
    if (!e->rc) { ++ch->epoch; ch->state = GF2_RUN; ch->fault.latched = false; }
    return done(e);
}
void gf_hook_ctx_bind(struct gf3_ctx *c)
{
    assert(c == &prod.accel && c->ready && !slot);
    hook_ctx = c;
}
void gf_hook_scan_bind(bool active, const struct gf3_surface *s)
{
    struct gf3_host_event *e = event(H_SURFACE_SET);
    if (active) {
        assert(hook_ctx == &prod.accel && prod.accel.ready && s == &service->scan && !slot);
        hook_scan = *s;
    } else assert(!slot && !s);
    hook_active = active;
    (void)done(e);
}
void geforce_backend_framebuffer_info(struct video_fb *out, u32 *width, u32 *height)
{
    ++framebuffer_calls;
    *out = framebuffer; *width = fb_width; *height = fb_height;
}
static int clock_now(void *opaque, u32 *now)
{
    assert(opaque == &host);
    return gf3_host_clock(opaque, now);
}
static int map_window(void *owner, const struct recover_bar_extent *extent,
                       u32 bytes, volatile u8 **out)
{
    assert(owner == &prod && extent->base == FB_PHYS + WINDOW_START && bytes == sizeof(window));
    *out = window;
    return 0;
}
static int unmap_window(void *owner, volatile u8 *base, u32 bytes)
{
    assert(owner == &prod && base == window && bytes == sizeof(window));
    return unmap_rc;
}
static const struct gf_instmem_map_ops map_ops = {map_window, unmap_window};
static i32 close_window(void *owner, struct gf_vram_view *v)
{
    assert(owner == &prod && v == &view && loan == window);
    int rc = gf_vram_view_unmap(v);
    if (!rc) loan = 0;
    return rc;
}
static void open_window(void)
{
    view.close = close_window; view.close_owner = &prod;
    assert(!gf_vram_view_map(&view, &prod.chan, WINDOW_START, MEM_PAGE_SIZE, &loan));
}

/* Unused drawing/display dependencies keep exact frozen signatures. These
 * return no synthetic packets/images; C1 tests the published function table. */
#define U __attribute__((unused))
int gf3_fill(struct gf3_ctx *c U, const struct gf3_surface *s U, struct gf3_rect r U, u32 color U, u8 alu U, u32 mask U, u32 dl U) { return 0; }
int gf3_copy(struct gf3_ctx *c U, const struct gf3_surface *s U, const struct gf3_surface *d U, struct gf3_rect r U, u32 x U, u32 y U, u8 alu U, u32 mask U, u32 dl U) { return 0; }
int gf3_mono(struct gf3_ctx *c U, const struct gf3_surface *s U, struct gf3_rect r U, const struct gf3_ram *bits U, u32 fg U, u32 bg U, struct gf3_rect clip U, u32 dl U) { return 0; }
int gf3_ifc(struct gf3_ctx *c U, const struct gf3_surface *s U, struct gf3_rect r U, const struct gf3_ram *ram U, u32 dl U) { return 0; }
int gf3_upload(struct gf3_ctx *c U, const struct gf3_surface *s U, struct gf3_rect r U, const struct gf3_ram *ram U, bool force U, u32 dl U) { return 0; }
int gf3_download(struct gf3_ctx *c U, const struct gf3_surface *s U, struct gf3_rect r U, struct gf3_ram *ram U, u32 dl U) { return 0; }
int gf3_scale(struct gf3_ctx *c U, const struct gf3_surface *s U, const struct gf3_surface *d U, struct gf3_rect sr U, struct gf3_rect dr U, struct gf3_rect clip U, bool bilinear U, u32 dl U) { return 0; }
int gf3_present_m2mf(struct gf3_ctx *c U, const struct gf3_surface *s U, struct gf3_rect r U, const struct gf3_ram *ram U, u32 dl U) { return 0; }
int gf_display_query(void *v U, struct video_info *out U) { return 0; }
int gf_display_framebuffer(void *v U, struct video_fb *out U) { return 0; }
int gf_display_start(void *v U, u32 off U) { return 0; }
int gf_display_palette(struct gf_display_context *c U, u32 first U, u32 count U, const u8 *rgb U) { return 0; }
int gf_display_blank(struct gf_display_context *c U, int blank U) { return 0; }
int gf_display_modeset(struct gf_display_context *c U) { return 0; }
int gf_display_baseline(struct gf_display_context *c U) { return 0; }
int gf_display_checks(struct gf_display_context *c U) { return 0; }
int gf_init_run(struct gf_init_context *c U, struct gf_init_program *p U) { return 0; }
int gfinit_mode_calc(const struct gfinit_pll_limits *limits U, const struct gfinit_mode_saved *saved U, struct gfinit_mode *out U) { return 0; }
int gf_display_mode_info(void *v U, u32 index, struct video_mode *out)
{
    assert(index == 0 && out);
    *out = (struct video_mode){.id = 42};
    return 0;
}
/* C2 is owned by another stage. This local failure sink closes admission;
 * it does not pretend to validate C2 restoration or F1/F2 MMIO behavior. */
static u32 display_stops;
int gf_display_stop(void *v)
{
    assert(v == &prod.display);
    ++display_stops;
    return gf3_service_close(service);
}
static int owner_ok(struct gf_display_context *c)
{ assert(c == &prod.display); return 0; }
static void owner_release(struct gf_display_context *c)
{ assert(c == &prod.display); }
static struct gf2_chan *command_get(struct gf_display_context *c, struct gf_foundation **f)
{
    assert(c == &prod.display);
    *f = &prod.foundation;
    return &prod.chan;
}
static const struct gf_display_owner display_owner = {
    owner_ok, owner_release, owner_ok, owner_ok, owner_ok, command_get
};
static u8 palette[VIDEO_PALETTE_BYTES];

static void clear_trace(void)
{
    gf3_host_trace_clear(&host);
    gf2_host_memset(host.fail_at, 0, sizeof(host.fail_at));
    gf2_host_memset(host.fail_rc, 0, sizeof(host.fail_rc));
}
static void reset(void)
{
    /* Fixture cleanup, never reset the registry or node identity history. */
    unmap_rc = 0;
    if (view.mapped) assert(!real_views_close(&prod.chan));
    if (service->test_node.value.owner)
        assert(!real_release(&prod.chan, &service->test_node.value));
    if (service->scan_node.value.owner)
        assert(!real_release(&prod.chan, &service->scan_node.value));
    assert(!registry.head && !views);
    gf2_host_memset(&prod, 0, sizeof(prod));
    prod.chan.f = &prod.foundation;
    prod.chan.state = GF2_RUN; prod.chan.epoch = 7;
    prod.chan.B = TEST_END + 19U;
    prod.chan.bar1 = (struct recover_bar_extent){.base = FB_PHYS,
        .length = 64U * 1024U * 1024U, .valid = true};
    prod.foundation.imem.vram_bytes = prod.chan.bar1.length;
    prod.foundation.imem.reserved = 1024U * 1024U;
    prod.foundation.io.opaque = &host;
    prod.foundation.io.timing.clock_us = clock_now;
    prod.chan.map_ops = &map_ops; prod.chan.map_owner = &prod;
    prod.display.engaged = prod.display.initialized = prod.display.reusable = true;
    prod.display.owner = &display_owner;
    prod.display.bars[GF_MODE_VRAM_BAR].length = GF_MODE_VRAM_BYTES;
    prod.video = (struct video_device){.context = &prod.display, .ops = &gf_video_ops};
    service->scan = service->test = (struct gf3_surface){0};
    service->scan_pitch = service->scan_width = service->scan_height = service->scan_phys = 0;
    slot = 0; hook_ctx = 0; hook_active = false;
    framebuffer = (struct video_fb){FB_PHYS + SCAN_OFF, SCAN_PITCH * SCAN_HEIGHT,
                                   SCAN_PITCH, GFX_FMT_PACKED8};
    fb_width = 640U; fb_height = SCAN_HEIGHT;
    stop_off = true; framebuffer_calls = display_stops = 0;
    host.now = 100U; deadline = TEST_DEADLINE;
    clear_trace();
}
static void inject(enum gf3_host_op op, u32 nth, int rc)
{
    /* A1 intentionally disallows generic FREE injection; release is an i32
     * service boundary here, rather than A1's void F1 inst_free operation. */
    host.fail_at[op] = nth; host.fail_rc[op] = rc;
}
static void start_ok(void)
{
    assert(!gf3_service_start(service, deadline));
    assert(slot == &service->ops && slot->ctx == &prod.accel);
    assert(slot->test_surface == &service->test && prod.accel.ready);
    assert(hook_active && hook_ctx == &prod.accel);
    assert(!gf2_host_memcmp(&hook_scan, &service->scan, sizeof(hook_scan)));
    assert(slot->fill == gf3_fill && slot->copy == gf3_copy && slot->mono == gf3_mono);
    assert(slot->ifc == gf3_ifc && slot->upload == gf3_upload && slot->download == gf3_download);
    assert(slot->scale == gf3_scale && slot->present_m2mf == gf3_present_m2mf);
    assert(slot->map == gf_vram_view_map && slot->unmap == gf_vram_view_unmap);
}
static void check_geometry(void)
{
    assert(service->scan.off == SCAN_OFF && service->scan.bytes == 2564U);
    assert(service->scan.pitch == 641U && service->scan.width == 640U && service->scan.height == 4U);
    assert(service->scan.fmt == GF3_INDEX8);
    assert(service->test.off == TEST_START && service->test.bytes == TEST_END - TEST_START);
    assert(service->test.pitch == TEST_PITCH && service->test.width == 640U && service->test.height == 4U);
    assert(service->test.fmt == service->scan.fmt);
    assert(service->scan.reservation_id == service->scan_node.value.reservation_id);
    assert(service->test.generation == service->test_node.value.generation);
}
static void no_publication(void)
{ assert(!slot && !hook_active); }
static void clock_deadline(void) { deadline = host.now + GF3_TIMEOUT_US; }

void _start(void)
{
    gf3_host_init(&host); host.events = events;
    host.cevents = sizeof(events) / sizeof(events[0]);
    assert(!gf3_service_get(0) && !gf3_service_get(&prod.chan));
    assert(gf3_service_bind(0, &slot) == GF3_EINVAL);
    assert(gf3_service_bind(&prod, 0) == GF3_EINVAL);
    slot = (const struct gf3_test_ops *)&other;
    assert(!gf3_service_bind(&prod, &slot) && !slot);
    service = gf3_service_get(&prod.chan);
    assert(service && !gf3_service_get(&host.ch) && !gf3_service_get(0));
    assert(!gf3_service_bind(&prod, &slot));
    assert(gf3_service_bind(&other, &slot) == GF3_ESTATE);
    assert(gf3_service_bind(&prod, &other_slot) == GF3_ESTATE);
    assert(gf3_service_start(0, 1) == GF3_EINVAL);
    assert(gf3_service_close(0) == GF3_EINVAL);
    assert(gf3_service_fini(0, 1) == GF3_EINVAL);
    assert(gf3_service_release(0) == GF3_EINVAL);
    assert(gf3_service_stop(0) == GF3_EINVAL);
    assert(gf3_service_recover(0, 1) == GF3_EINVAL);
    gf3_service_invalidate(0);

    reset(); start_ok(); check_geometry();
    u32 id = service->scan.reservation_id, gen = service->scan.generation;
    u32 test_id = service->test.reservation_id;
    assert(!gf3_service_bind(&prod, &slot) && slot == &service->ops);
    assert(gf3_service_start(service, deadline) == GF3_ESTATE);
    assert(!gf3_service_close(service) && prod.accel.ready);
    no_publication(); assert(!gf3_service_fini(service, deadline));
    assert(!gf2_stop(&prod.chan)); gf3_service_invalidate(service);
    assert(!prod.accel.ready && !prod.accel.epoch && prod.accel.ch == &prod.chan);
    assert(!prod.accel.obj[0].handle && !prod.accel.notify.handle && !prod.accel.notify_storage.size);
    assert(!gf3_service_release(service));
    assert(!service->scan.bytes && !service->test.bytes);
    assert(service->scan_node.value.reservation_id == id && service->scan_node.value.generation == gen);
    assert(!gf3_service_release(service));

    /* Recover keeps framebuffer geometry, but invalidates every old key.
     * Backend metadata is intentionally made unusable before recovery. */
    reset(); start_ok(); id = service->scan.reservation_id; gen = service->scan.generation;
    test_id = service->test.reservation_id;
    framebuffer.phys = 0; fb_width = fb_height = 0;
    clear_trace(); assert(!gf3_service_recover(service, deadline));
    assert(framebuffer_calls == 1 && prod.chan.epoch == 8 && prod.accel.epoch == 8);
    assert(slot == &service->ops && prod.accel.ready); check_geometry();
    assert(service->scan.reservation_id != id && service->scan.generation == gen + 1U);
    assert(service->test.reservation_id != test_id);
    struct gf_vram_reservation key;
    assert(gf_vram_match(&prod.chan, 0, id, gen, SCAN_OFF, 1, &key) == GF3_ESTATE);

    /* Fault rc plus OFF is sufficient for recover; diagnostic STOP preserves
     * the first failure while still invalidating dead F3 descriptors. */
    reset(); start_ok(); clear_trace(); prod.chan.fault.latched = true;
    inject(H_FENCE, 1, GF2_EFAULT);
    assert(!gf3_service_recover(service, deadline) && slot);
    reset(); start_ok(); id = service->scan.reservation_id; clear_trace(); clock_deadline();
    inject(H_FENCE, 1, GF2_EFAULT);
    assert(gf3_service_stop(service) == GF2_EFAULT);
    no_publication(); assert(prod.chan.state == GF2_OFF && !prod.accel.ready);
    assert(service->scan_node.value.reservation_id == id && service->test_node.value.owner);
    assert(prod.accel.last_rc == GF2_EFAULT && prod.display.engaged);
    prod.chan.state = GF2_RUN; ++prod.chan.epoch; prod.chan.fault.latched = false;
    framebuffer.phys = 0; fb_width = fb_height = 0;
    clear_trace(); start_ok();
    assert(framebuffer_calls == 1 && !host.calls[H_ALLOC] && service->scan.reservation_id == id);

    reset(); start_ok(); clear_trace(); stop_off = false;
    inject(H_FENCE, 1, GF2_ESTOP);
    assert(gf3_service_recover(service, deadline) == GF2_ESTOP);
    no_publication(); assert(prod.accel.ready && prod.accel.obj[0].handle);
    assert(!host.calls[H_RESUME] && !host.calls[H_FREE]);
    reset(); start_ok(); clear_trace(); inject(H_RESUME, 1, GF2_EIO);
    assert(gf3_service_recover(service, deadline) == GF2_EIO);
    no_publication(); assert(!prod.accel.ready && !prod.accel.notify_storage.size);
    assert(service->scan_node.value.owner && service->test_node.value.owner && !host.calls[H_FREE]);

    /* Reservation failures retain only the not-yet-released nodes, and a
     * retry touches only those nodes. Failed init leaves partial ctx for C2. */
    for (u32 nth = 1; nth <= 2; ++nth) {
        reset(); start_ok(); clear_trace(); inject(H_FREE, nth, GF3_ESTATE);
        assert(gf3_service_recover(service, deadline) == GF3_ESTATE);
        no_publication(); assert(!prod.accel.ready && !host.calls[H_GR_NEW]);
        assert(service->scan_node.value.owner);
        assert((bool)service->test_node.value.owner == (nth == 1));
        clear_trace(); assert(!gf3_service_recover(service, deadline) && slot);
    }
    for (u32 nth = 1; nth <= 2; ++nth) {
        reset(); start_ok(); clear_trace(); inject(H_ALLOC, nth, GF1_EIO);
        assert(gf3_service_recover(service, deadline) == GF1_EIO);
        no_publication(); assert(!prod.accel.ready && !host.calls[H_GR_NEW]);
        assert((bool)service->scan_node.value.owner == (nth == 2));
        clear_trace(); assert(!gf3_service_recover(service, deadline) && slot);
    }
    reset(); start_ok(); clear_trace(); inject(H_GR_NEW, 1, GF2_ETIME);
    assert(gf3_service_recover(service, deadline) == GF2_ETIME);
    no_publication(); assert(!prod.accel.ready && prod.accel.notify_storage.size);
    assert(service->scan_node.value.owner && service->test_node.value.owner);
    clear_trace(); clock_deadline(); assert(!gf3_service_stop(service));
    assert(!host.calls[H_DEL] && !prod.accel.notify_storage.size);

    reset(); start_ok(); clear_trace(); prod.display.engaged = false;
    id = service->scan.reservation_id;
    assert(!gf3_service_recover(service, deadline)); no_publication();
    assert(!host.calls[H_ALLOC] && !host.calls[H_FREE] && !host.calls[H_GR_NEW]);
    assert(service->scan_node.value.reservation_id == id && !prod.accel.ready);

    /* Fini eligibility and stop failure ordering. */
    reset(); start_ok(); clear_trace(); ++prod.chan.epoch;
    assert(!gf3_service_fini(service, deadline) && !host.calls[H_DEL]);
    --prod.chan.epoch; prod.chan.fault.latched = true;
    assert(!gf3_service_fini(service, deadline) && !host.calls[H_DEL]);
    prod.chan.fault.latched = false; prod.chan.state = GF2_FAULT;
    assert(!gf3_service_fini(service, deadline) && !host.calls[H_DEL]);
    reset(); start_ok(); clear_trace(); clock_deadline(); inject(H_DEL, 1, GF2_ETIME);
    assert(gf3_service_stop(service) == GF2_ETIME && host.calls[H_FENCE] == 1);
    assert(!prod.accel.ready && !prod.accel.notify_storage.size);
    reset(); start_ok(); clear_trace(); inject(H_CLOCK, 1, GF1_EIO);
    assert(gf3_service_stop(service) == GF2_EIO && !host.calls[H_DEL] && host.calls[H_FENCE] == 1);
    assert(!prod.accel.ready && prod.accel.last_rc == GF2_EIO);
    reset(); start_ok(); clear_trace(); stop_off = false; clock_deadline();
    assert(gf3_service_stop(service) == GF2_ESTOP && service->scan_node.value.owner);

    /* A real V0 window callback survives slot closure and failed unmap.
     * Move the display/test ranges beyond the fixed boot-display map. */
    reset(); framebuffer.phys += GF_MODE_VRAM_BYTES;
    prod.chan.B += GF_MODE_VRAM_BYTES;
    start_ok(); assert(service->test.off == WINDOW_START); open_window();
    clear_trace(); unmap_rc = -1;
    assert(gf3_service_stop(service) == GF2_EMAP); no_publication();
    assert(view.mapped && loan && prod.accel.ready && !host.calls[H_FENCE]);
    assert(gf3_service_recover(service, deadline) == GF2_EMAP && !host.calls[H_RESUME]);
    unmap_rc = 0; clear_trace(); clock_deadline();
    assert(!gf3_service_stop(service) && !view.mapped && !loan);
    assert(service->test_node.value.owner && !prod.accel.ready);
    reset(); start_ok(); clear_trace(); prod.foundation.revoked = true;
    inject(H_STOP, 1, GF2_EMAP); clock_deadline();
    assert(gf3_service_stop(service) == GF2_EMAP && host.calls[H_FENCE] == 1);
    no_publication();

    /* Capacity and exact pitch/height rules, including height truncation. */
    reset(); prod.chan.B = TEST_START + MEM_PAGE_SIZE;
    fb_height = 6; framebuffer.bytes = SCAN_PITCH * 6U;
    start_ok(); assert(service->test.height == 5U && service->test.pitch == 704U);
    reset(); framebuffer.pitch = 1024; fb_height = 3;
    prod.chan.B = 2U * MEM_PAGE_SIZE;
    start_ok(); assert(service->test.pitch == 1024U && service->test.height == 3U);
    reset(); framebuffer.pitch = 3000; fb_height = 1;
    prod.chan.B = 2U * MEM_PAGE_SIZE;
    start_ok(); assert(service->test.pitch == 3008U && service->test.height == 1U);
    reset(); framebuffer.pitch = 5000; fb_height = 1;
    prod.chan.B = 3U * MEM_PAGE_SIZE;
    assert(gf3_service_start(service, deadline) == GF2_ENOSPC);
    assert(!slot && hook_active && prod.accel.ready && !service->test_node.value.owner);
    reset(); prod.chan.B = TEST_START;
    assert(gf3_service_start(service, deadline) == GF2_ENOSPC && !slot && prod.accel.ready);
    reset(); framebuffer.pitch = 640; framebuffer.phys = FB_PHYS;
    start_ok(); assert(service->test.pitch == 640U);

    /* Failed ordinary start retries preserve a successfully reserved scan. */
    reset(); inject(H_ALLOC, 2, GF1_EIO);
    assert(gf3_service_start(service, deadline) == GF1_EIO);
    id = service->scan_node.value.reservation_id; gen = service->scan_node.value.generation;
    clear_trace(); start_ok();
    assert(host.calls[H_ALLOC] == 1 && service->scan.reservation_id == id && service->scan.generation == gen);
    reset(); inject(H_PAUSE, 1, GF3_ESTATE);
    assert(gf3_service_start(service, deadline) == GF3_ESTATE && !host.calls[H_ALLOC]);
    reset(); prod.chan.state = GF2_OFF;
    assert(gf3_service_start(service, deadline) == GF3_ESTATE && !framebuffer_calls);
    reset(); prod.chan.epoch = 0;
    assert(gf3_service_start(service, deadline) == GF3_ESTATE);
    reset(); framebuffer.format = GFX_FMT_PACKED8 + 1U;
    assert(gf3_service_start(service, deadline) == GF3_EFORMAT && !host.calls[H_ALLOC]);
    reset(); framebuffer.pitch = U32_MAX; fb_height = 2;
    assert(gf3_service_start(service, deadline) == GF3_ERANGE && !host.calls[H_ALLOC]);

    /* Exercise the actual gf_video_ops.start connection, including clock
     * failure and the display-only ENOSPC outcome. */
    reset(); clock_deadline();
    assert(!video_start(&prod.video, 42, palette) && prod.display.active && slot);
    reset(); clock_deadline(); inject(H_CLOCK, 1, GF1_EIO);
    assert(video_start(&prod.video, 42, palette) == -1 && display_stops == 1);
    assert(prod.display.last_rc == GF2_EIO && !host.calls[H_GR_NEW]); no_publication();
    reset(); clock_deadline(); inject(H_GR_NEW, 1, GF2_ETIME);
    assert(video_start(&prod.video, 42, palette) == -1 && display_stops == 1);
    assert(prod.display.last_rc == GF2_ETIME); no_publication();
    reset(); clock_deadline(); prod.chan.B = TEST_START;
    assert(!video_start(&prod.video, 42, palette) && prod.display.active && !slot && hook_active);
    reset();
    gf2_host_exit(0);
}
