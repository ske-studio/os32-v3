/* A3 wire/session/case0..7: UNIT 14 supplies helpers, no dependency stubs.
 * Compile this TU alone, -m32 -nostdlib, with include/ on the include path.
 * The local slot callbacks model C3/C2's public contract, not hardware.
 * A2 owns registration/CHECK inputs and mutation execution at integration. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#ifndef GF4_A3_SOURCE
#define GF4_A3_SOURCE "../../kdebug/gf_display_test.c"
#endif
#include GF4_A3_SOURCE

enum session_event { SESSION_SAVE, SESSION_RESTORE, SESSION_RELEASE,
    CASE_PREPARE, CASE_DISCARD, CASE_UPLOAD, CASE_DOWNLOAD, CASE_SET,
    CASE_PALETTE, CASE_DPMS, CASE_DDC, CASE_CURSOR_IMAGE,
    CASE_CURSOR_MOVE, CASE_CURSOR_SHOW, CASE_FLIP, CASE_VBLANK,
    CASE_GAMMA, CASE_ENTER, CASE_LEAVE };
/* One start + initial restore + final restore/release, with retry room. */
#define SESSION_EVENTS (GF4_MODES * 16U)
#define SESSION_NOW 123U
static u32 session_now = SESSION_NOW;
static bool slow_operations;
#define SESSION_FAILURE (-777)
static bool forced_leave;
static i32 abandon_rc;
static u32 abandons;
static u32 heat_clear_calls;
static int heat_clear_rc;
int geforce_heat_latch_clear(void) { heat_clear_calls++; return heat_clear_rc; }
static int current_owner = 2;
int res_owner_get(void) { return current_owner; }
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_test_ops normal, replacement;
static struct gf4_restore_ops management, replacement_management;
static const struct gf4_test_ops *normal_slot;
static const struct gf4_restore_ops *management_slot;
static enum session_event events[SESSION_EVENTS];
static u32 nevents, normal_reads, management_reads;
static i32 save_rc, restore_rc, release_rc, clock_rc;
static bool publish, swap_after_restore, swap_after_clock;
static struct gf3_test_ops accel_ops;
static const struct gf3_test_ops *accel_slot;
static struct gf4_route routes[2];
/* A 1032-wide 32bpp mode crosses the 4096-byte row-fragment boundary. */
#define IMAGE_WIDTH 1032U
#define IMAGE_HEIGHT 3U
#define IMAGE_BYTES (IMAGE_WIDTH * IMAGE_HEIGHT * 4U)
#define LOG_BYTES (GF4_MODES * 256U)
static u8 image[IMAGE_BYTES], back_image[IMAGE_BYTES];
static u8 head0_image[2][IMAGE_BYTES];
static bool output_fixture, revoke_gamma, enter_invalid, leave_failed, perturb_other;
static bool backend_failed, core_reject;
static u32 core_inits, core_shutdowns;
static const GfxMode core_mode = {
    .width=IMAGE_WIDTH, .height=IMAGE_HEIGHT, .format=GFX_FMT_PACKED8
};
static u32 gammas, enters, leaves, prepared_head[GF4_HEADS], discarded_head[GF4_HEADS];
/* docs/decisions/realhw/GF_FEAT4_DESIGN.md:574: identity, dark curve,
 * identity; palette return has no gamma. */
enum { EXPECTED_GAMMA_APPLIES = 3 };
static u32 gamma_checksum[EXPECTED_GAMMA_APPLIES];
static char output[LOG_BYTES];
static u32 log_bytes, uploads, downloads, prepares, discards, sets, powers, ddcs;
static enum gf4_dpms power_seen[sizeof(display_power) / sizeof(display_power[0])];
static enum session_event fail_event;
static bool corrupt, revoke_after_upload, revoke_after_prepare, revoke_normal_after_prepare;
static u32 failure_nth, failure_calls;
static i32 failure_rc, first_port_rc;
static bool fail_restore_after_set;
static u32 port_reads;
/* Fixture bus choices; the first is valid unless first_port_rc rejects it. */
#define FIRST_DDC_BUS 3U
#define NEXT_DDC_BUS (FIRST_DDC_BUS + 1U)
static u8 expected_ddc_bus;

/* Independent A3-card oracle: 10 camera steps, two initial cursor images
 * plus the mode-change upload; two flip steps of 16 frames each. */
enum { EXPECTED_CURSOR_READY = 10, EXPECTED_CURSOR_IMAGES = 3,
       EXPECTED_FLIP_FRAMES = 16, EXPECTED_FLIP_STEPS = 2,
       EXPECTED_FLIP_TOTAL = EXPECTED_FLIP_FRAMES * EXPECTED_FLIP_STEPS };
static u32 cursor_images, cursor_moves, cursor_shows, flips, waits, snapshots;
static u32 cursor_sides[EXPECTED_CURSOR_IMAGES], cursor_copy[GF4_CURSOR_BYTES / sizeof(u32)];
static i32 move_x[16], move_y[16];
static bool show_seen[16], sync_seen[EXPECTED_FLIP_TOTAL], revoke_cursor, revoke_flip, wrong_base;
static struct gf3_surface flip_surface;
static u8 *surface_image(const struct gf3_surface *s)
{
    if (s->off >= GF3_STAGING_BYTES + 2U * IMAGE_BYTES)
        return head0_image[s->off == GF3_STAGING_BYTES + 2U * IMAGE_BYTES ? 0 : 1];
    return s->off == GF3_STAGING_BYTES ? image : back_image;
}
static struct gf1_test_ops foundation_ops;
static bool revoke_snapshot;
static i32 snapshot_rc;
const struct gf1_test_ops *geforce_foundation_ops(void) { return &foundation_ops; }
/* Borrowed F1 snapshot dispatch, foundation_test.c:65-94. */
static int snapshot_call(struct gf_foundation *f, const struct gf1_test_request *q,
                         struct gf1_test_reply *out)
{
    assert(f == &host.base.f && q->size == sizeof(*q) &&
           q->op == GF1T_SNAPSHOT && !q->heads);
    ++snapshots; out->stats = f->stats;
    if (revoke_snapshot) normal_slot = NULL;
    return snapshot_rc;
}
const struct gf3_test_ops *geforce_accel_ops(void) { return accel_slot; }
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s)
{
    while (*s) {
        assert(log_bytes + 1U < sizeof(output));
        output[log_bytes++] = *s++;
    }
    output[log_bytes] = 0;
}
static i32 case_event(enum session_event e)
{
    assert(nevents < SESSION_EVENTS); events[nevents++] = e;
    if (slow_operations) session_now += GF3_TIMEOUT_US / 3U;
    if (e == fail_event && failure_nth && ++failure_calls == failure_nth) return failure_rc;
    return 0;
}
static int modes_call(struct gf4_ctx *ctx, u8 connector,
                      const struct gf4_mode **m, u32 *n)
{
    assert(ctx == &host.ctx && (connector == routes[0].connector ||
           (output_fixture && connector == routes[1].connector)));
    *m = state.modes; *n = state.nmodes; return 0;
}
static i32 prepare_call(struct gf4_ctx *ctx, u8 head, u32 id,
                        struct gf3_surface out[2], u32 dl)
{
    assert(ctx == &host.ctx && (head == 1 || (output_fixture && head == 0)) && id >= 1 && id <= state.nmodes);
    assert(dl == session_now + GF3_TIMEOUT_US);
    ++prepares; ++prepared_head[head];
    i32 rc = case_event(CASE_PREPARE);
    if (rc) return rc;
    const struct gf4_mode *m = &state.modes[id - 1U];
    out[0] = (struct gf3_surface){.off=GF3_STAGING_BYTES, .bytes=m->pitch*m->t.vd,
        .pitch=m->pitch, .width=m->t.hd, .height=m->t.vd, .fmt=m->fmt,
        .reservation_id=id, .generation=1};
    if (head == 0) out[0].off += 2U * IMAGE_BYTES;
    out[1] = out[0]; out[1].off += IMAGE_BYTES; ++out[1].reservation_id;
    if (revoke_after_prepare) accel_slot = NULL;
    if (revoke_normal_after_prepare) normal_slot = NULL;
    return 0;
}
static i32 discard_call(struct gf4_ctx *ctx, u8 head, u32 dl)
{
    assert(ctx == &host.ctx && (head == 1 || (output_fixture && head == 0)) && !ctx->saved_closed);
    assert(dl == session_now + GF3_TIMEOUT_US);
    ++discards; ++discarded_head[head]; return case_event(CASE_DISCARD);
}
static int upload_call(struct gf3_ctx *ctx, const struct gf3_surface *s,
                       struct gf3_rect r, const struct gf3_ram *ram, bool force, u32 dl)
{
    assert(ctx == &host.base.ctx && force && r.h == 1 && s->reservation_id);
    assert(dl == session_now + GF3_TIMEOUT_US);
    u32 cpp = s->fmt == GF3_INDEX8 ? 1U : s->fmt == GF3_RGB565 ? 2U : 4U;
    assert(ram->bytes == r.w * cpp && ram->pitch == ram->bytes);
    assert(r.x + r.w <= s->width && r.y < s->height);
    u32 off = r.y * s->pitch + r.x * cpp;
    assert(off + ram->bytes <= sizeof(image));
    ++uploads;
    i32 rc = case_event(CASE_UPLOAD);
    if (rc) return rc;
    gf2_host_memcpy(surface_image(s) + off, ram->ptr, ram->bytes);
    if (revoke_after_upload) accel_slot = NULL;
    return 0;
}
static int download_call(struct gf3_ctx *ctx, const struct gf3_surface *s,
                         struct gf3_rect r, struct gf3_ram *ram, u32 dl)
{
    assert(ctx == &host.base.ctx && dl == session_now + GF3_TIMEOUT_US);
    ++downloads;
    i32 rc = case_event(CASE_DOWNLOAD);
    if (rc) return rc;
    u32 cpp = s->fmt == GF3_INDEX8 ? 1U : s->fmt == GF3_RGB565 ? 2U : 4U;
    gf2_host_memcpy(ram->ptr, surface_image(s) + r.y * s->pitch + r.x * cpp, ram->bytes);
    if (corrupt) ((u8 *)ram->ptr)[0] ^= 1U;
    if (cpp == 4) /* Hardware may return any unused X byte. */
        for (u32 x = 0; x < r.w; ++x) ((u8 *)ram->ptr)[x * cpp + 3U] = U8_MAX;
    return 0;
}
static int set_call(struct gf4_ctx *ctx, const struct gf4_route *r, u32 id,
                    const struct gf3_surface *s, u32 dl)
{
    assert(ctx == &host.ctx && (r->connector == routes[0].connector || output_fixture));
    assert(r->head == 1 || (output_fixture && r->head == 0));
    assert(s->reservation_id == id && dl == session_now + GF3_TIMEOUT_US);
    assert(uploads == downloads && uploads);
    ++sets;
    i32 rc = case_event(CASE_SET);
    if (rc && fail_restore_after_set) restore_rc = SESSION_FAILURE;
    if (!rc) {
        state.head[r->head].mode = state.modes[id-1U]; state.head[r->head].front = *s;
        state.head[r->head].enabled = true; state.head[r->head].dpms = GF4_ON;
    }
    return rc;
}
static i32 palette_call(struct gf4_ctx *ctx, u8 head, u32 first, u32 n, const u8 *rgb)
{
    assert(ctx == &host.ctx && (head == 1 || (output_fixture && head == 0)) && first == 0 && n == 8);
    /* RGB cube endpoint oracle, independent of the product palette. */
    for (u32 i = 0; i < n; ++i)
        for (u32 c = 0; c < 3; ++c)
            assert(rgb[i * 3 + c] == ((i & (4U >> c)) ? 255 : 0));
    return case_event(CASE_PALETTE);
}
static int dpms_call(struct gf4_ctx *ctx, u8 head, enum gf4_dpms power, u32 dl)
{
    assert(ctx == &host.ctx && (head == 1 || output_fixture) && dl == session_now + GF3_TIMEOUT_US);
    assert(powers < sizeof(power_seen) / sizeof(power_seen[0]));
    power_seen[powers++] = power;
    i32 rc = case_event(CASE_DPMS);
    if (!rc) {
        state.head[head].dpms = power;
        if (perturb_other) ++state.head[head ^ 1U].front.off;
    }
    return rc;
}
static int cursor_image_call(struct gf4_ctx *ctx, u8 head,
                             const struct gf4_cursor *c, u32 dl)
{
    assert(ctx == &host.ctx && head == 1 && dl == session_now + GF3_TIMEOUT_US);
    assert(c->w == c->h && (c->w == 32 || c->w == 64));
    assert(c->pitch == c->w * sizeof(u32) && c->bytes == c->pitch * c->h);
    assert(c->hot_x == 3 && c->hot_y == 5 && cursor_images < EXPECTED_CURSOR_IMAGES);
    cursor_sides[cursor_images++] = c->w;
    i32 rc = case_event(CASE_CURSOR_IMAGE);
    if (rc) return rc;
    gf2_host_memcpy(cursor_copy, c->argb_pm, c->bytes);
    /* Independent PM oracle: first arrow column covers alpha endpoints;
     * red=A, green=floor(A/2). This input has no hardware alpha+1 fixup. */
    static const u32 expected[] = {0x00000000U,0x01010000U,0xfefe7f00U,0xffff7f00U};
    for (u32 y = 0; y < 4; ++y) assert(cursor_copy[y*c->w] == expected[y]);
    assert(cursor_copy[c->w-1U] == 0); /* Asymmetric, transparent outside arrow. */
    state.head[head].cursor_valid = true;
    if (revoke_cursor) normal_slot = NULL;
    return 0;
}
static int cursor_move_call(struct gf4_ctx *ctx, u8 head, i32 x, i32 y)
{
    assert(ctx == &host.ctx && head == 1 && cursor_moves < 16);
    move_x[cursor_moves] = x; move_y[cursor_moves++] = y;
    return case_event(CASE_CURSOR_MOVE);
}
static int cursor_show_call(struct gf4_ctx *ctx, u8 head, bool show)
{
    assert(ctx == &host.ctx && head == 1 && cursor_shows < 16);
    show_seen[cursor_shows++] = show;
    i32 rc = case_event(CASE_CURSOR_SHOW);
    if (!rc) state.head[head].cursor_visible = show;
    return rc;
}
static int flip_call(struct gf4_ctx *ctx, u8 head, const struct gf3_surface *s,
                     bool sync, u32 dl)
{
    assert(ctx == &host.ctx && head == 1 && dl == session_now + GF3_TIMEOUT_US);
    assert(flips < EXPECTED_FLIP_TOTAL && !ctx->hold[head].pending && uploads == downloads);
    /* First target is back, then front, repeated independently of product constants. */
    if (output_fixture) assert(sync && s->off == GF3_STAGING_BYTES + IMAGE_BYTES);
    else assert(s->off == GF3_STAGING_BYTES + ((flips % 2U) ? 0U : IMAGE_BYTES));
    sync_seen[flips++] = sync;
    i32 rc = case_event(CASE_FLIP);
    if (rc) return rc;
    flip_surface = *s;
    ctx->hold[head].pending = !sync;
    if (sync) { state.head[head].front = *s; ++host.base.f.stats.vblank[head]; }
    if (revoke_flip) normal_slot = NULL;
    return 0;
}
static int vblank_call(struct gf4_ctx *ctx, u8 head, u32 after, u32 dl, u32 *seen)
{
    assert(ctx == &host.ctx && head == 1 && dl == session_now + GF3_TIMEOUT_US);
    ++waits;
    i32 rc = case_event(CASE_VBLANK);
    if (rc) return rc;
    if (ctx->hold[head].pending) {
        state.head[head].front = flip_surface; ctx->hold[head].pending = false;
        ++host.base.f.stats.vblank[head];
    }
    assert((u32)(host.base.f.stats.vblank[head] - after) != 0);
    *seen = host.base.f.stats.vblank[head];
    if (wrong_base) state.head[head].front.off = 0;
    return 0;
}
/* Endpoint/midpoint values from the specified integer curve, calculated
 * independently as floor((i/255)^2 * 65535). No production constants used. */
static int gamma_call(struct gf4_ctx *ctx, u8 head, const u16 rgb[3][256])
{
    assert(ctx == &host.ctx && head == 1 && state.head[head].mode.fmt != GF3_INDEX8);
    assert(gammas < EXPECTED_GAMMA_APPLIES);
    /* Observe the callback's complete input independently of serial output. */
    gamma_checksum[gammas] = 0;
    for (u32 c = 0; c < sizeof(display_ramp) / sizeof(display_ramp[0]); ++c)
        for (u32 i = 0; i < sizeof(display_ramp[0]) / sizeof(display_ramp[0][0]); ++i)
            gamma_checksum[gammas] += rgb[c][i];
    static const u16 curve[] = {0,1,16512,65535};
    static const u32 at[] = {0,1,128,255};
    for (u32 c = 0; c < 3U; ++c)
        for (u32 j = 0; j < sizeof(at)/sizeof(at[0]); ++j)
            assert(rgb[c][at[j]] == (gammas == 1U ? curve[j] : at[j]*257U));
    ++gammas;
    i32 rc = case_event(CASE_GAMMA);
    if (revoke_gamma) normal_slot = NULL;
    return rc;
}
static void enter_call(void)
{
    ++enters;
    /* backend_geforce.c:geforce_init needs the CLIENT binding supplied by
     * gfx_select_and_init_backend, not merely the published F4 ops. */
    if (backend_failed) return;
    if (!gfx_backend_geforce.bb_base || !gfx_backend_geforce.selected_mode) {
        backend_failed = true;
        return;
    }
    i32 rc = case_event(CASE_ENTER);
    if (rc) { normal_slot = NULL; return; }
    normal_slot = &normal; management_slot = &management; host.ctx.ready = true;
    state.head[0].enabled = !enter_invalid; state.head[0].mode = state.modes[0];
    state.head[0].front.reservation_id = enters;
    state.head[0].dpms = GF4_ON;
}
static void leave_call(void)
{
    ++leaves;
    i32 rc = case_event(CASE_LEAVE);
    if (rc || leave_failed) return;
    if (!forced_leave) assert(!host.ctx.diagnostic_saved && !display_session.valid);
    normal_slot = NULL; host.ctx.ready = false;
    host.ctx.cleanup_pending = display_session.valid;
    management_slot = host.ctx.cleanup_pending ? &management : NULL;
    state.head[0].enabled = false;
}
GfxBackend gfx_backend_geforce = {.enter=enter_call, .leave=leave_call};
void gfx_init(void)
{
    ++core_inits;
    if (core_reject) return; /* Core admission failed before backend init. */
    gfx_backend_geforce.selected_mode = &core_mode;
    gfx_backend_geforce.bb_base = image;
    enter_call();
}
void gfx_shutdown(void)
{
    ++core_shutdowns;
    leave_call();
}
static int ddc_call(struct gf4_ctx *ctx, u8 bus, u8 block, u8 out[128], u32 dl)
{
    assert(ctx == &host.ctx && bus == expected_ddc_bus && block == ddcs);
    assert(dl == session_now + GF3_TIMEOUT_US);
    ++ddcs;
    i32 rc = case_event(CASE_DDC);
    if (rc) return rc;
    gf2_host_memset(out, block, 128);
    if (!block) out[126] = 1; /* One extension: confirms traversal, not EDID parsing. */
    return 0;
}
static i32 port_call(void *owner, u8 bus, struct gf4_i2c_port *out)
{
    assert(owner == &host && out);
    ++port_reads;
    if (bus == FIRST_DDC_BUS && first_port_rc) return first_port_rc;
    assert(bus == FIRST_DDC_BUS || bus == NEXT_DDC_BUS);
    *out = host.port;
    return 0;
}

const struct gf4_test_ops *geforce_display_ops(void)
{
    ++normal_reads;
    return normal_slot;
}
const struct gf4_restore_ops *geforce_display_restore_ops(void)
{
    ++management_reads;
    return management_slot;
}
static void event(enum session_event e, struct gf4_ctx *ctx,
                  const struct gf4_saved *saved)
{
    assert(ctx == &host.ctx && saved == &display_session.saved);
    assert(nevents < SESSION_EVENTS);
    events[nevents++] = e;
}
static int clock_read(void *owner, u32 *now)
{
    assert(owner == &host);
    *now = session_now;
    if (swap_after_clock) {
        normal_slot = &replacement;
        management_slot = &replacement_management;
    }
    return clock_rc;
}
static i32 save_call(struct gf4_ctx *ctx, struct gf4_saved *saved, u32 dl)
{
    event(SESSION_SAVE, ctx, saved);
    assert(dl == session_now + GF3_TIMEOUT_US);
    if (save_rc) return save_rc;
    for (u8 h = 0; h < GF4_HEADS; ++h) saved->head[h] = state.head[h];
    saved->version = GF4_SAVED_VERSION; saved->size = sizeof(*saved);
    ctx->diagnostic_saved = saved; ctx->saved_closed = true;
    normal_slot = NULL;
    return 0;
}
static i32 restore_call(struct gf4_ctx *ctx, const struct gf4_saved *saved, u32 dl)
{
    event(SESSION_RESTORE, ctx, saved);
    assert(dl == session_now + GF3_TIMEOUT_US);
    assert(saved->version == GF4_SAVED_VERSION && saved->size == sizeof(*saved));
    assert(ctx->diagnostic_saved == saved && display_session.valid);
    if (swap_after_restore) management_slot = &replacement_management;
    if (restore_rc) return restore_rc;
    ctx->saved_closed = false;
    for (u8 h = 0; h < GF4_HEADS; ++h) state.head[h] = saved->head[h];
    ctx->hold[1].pending = false; state.head[1].cursor_valid = false;
    normal_slot = publish ? &normal : NULL;
    return 0;
}
static i32 release_call(struct gf4_ctx *ctx, const struct gf4_saved *saved)
{
    event(SESSION_RELEASE, ctx, saved);
    assert(!ctx->saved_closed && display_session.valid);
    if (release_rc) return release_rc;
    ctx->diagnostic_saved = NULL;
    return 0;
}
/* External service seam: A8/A9's real stop/abandon is covered by c2svc.
 * This fixture observes A3 re-reading the current slot after gfx_shutdown. */
static i32 abandon_call(struct gf4_ctx *ctx,const struct gf4_saved *saved)
{
    ++abandons;
    assert(ctx==&host.ctx && !ctx->ready && ctx->cleanup_pending);
    assert(saved==&display_session.saved);
    if (abandon_rc) return abandon_rc;
    ctx->diagnostic_saved=NULL; ctx->saved_closed=false;
    ctx->cleanup_pending=false; management_slot=NULL;
    return 0;
}
static void reset(void)
{
    session_now = SESSION_NOW; slow_operations = false;
    display_cui_owner = 0; current_owner = 2; forced_leave = false;
    abandon_rc=0; abandons=0;
    gf2_host_memset(&state, 0, sizeof(state));
    gf4_host_init(&host, &state);
    output_fixture = revoke_gamma = enter_invalid = leave_failed = perturb_other = false;
    gammas = enters = leaves = 0;
    core_inits = core_shutdowns = 0; backend_failed = core_reject = false;
    /* Other display cases start during an application's existing gfx session. */
    gfx_backend_geforce.selected_mode = &core_mode;
    gfx_backend_geforce.bb_base = image;
    for (u8 h = 0; h < GF4_HEADS; ++h) prepared_head[h] = discarded_head[h] = 0;
    gfx_backend_geforce.enter = enter_call; gfx_backend_geforce.leave = leave_call;
    gf2_host_memset(&display_session, 0, sizeof(display_session));
    normal = (struct gf4_test_ops){.ctx=&host.ctx, .display_save=save_call};
    normal.gamma = gamma_call;
    normal.modes = modes_call; normal.scan_prepare = prepare_call;
    normal.cursor_image = cursor_image_call; normal.cursor_move = cursor_move_call;
    normal.cursor_show = cursor_show_call; normal.flip = flip_call; normal.vblank_wait = vblank_call;
    normal.scan_discard = discard_call; normal.set_mode = set_call;
    normal.palette_set = palette_call; normal.dpms_set = dpms_call; normal.ddc_read = ddc_call;
    accel_ops = (struct gf3_test_ops){.ctx=&host.base.ctx,
        .upload=upload_call, .download=download_call};
    accel_slot = &accel_ops;
    routes[0] = (struct gf4_route){.connector=0xa5, .head_mask=2, .i2c_index=FIRST_DDC_BUS};
    routes[1] = routes[0];
    host.ctx.routes = routes; host.ctx.nroutes = 2;
    state.nmodes = 3;
    for (u32 i = 0; i < state.nmodes; ++i) {
        u32 cpp = i == 0 ? 1U : i == 1 ? 2U : 4U;
        state.modes[i] = (struct gf4_mode){.id=i+1, .fmt=(enum gf3_fmt)i,
            .t={.hd=IMAGE_WIDTH,.vd=IMAGE_HEIGHT}, .pitch=IMAGE_WIDTH*cpp};
    }
    management = (struct gf4_restore_ops){.ctx=&host.ctx,
        .restore=restore_call, .saved_release=release_call,.abandon=abandon_call};
    replacement = (struct gf4_test_ops){.ctx=&host.ctx};
    replacement_management = (struct gf4_restore_ops){.ctx=&host.ctx};
    normal_slot = &normal; management_slot = &management;
    host.base.f.io.timing.clock_us = clock_read;
    nevents = normal_reads = management_reads = 0;
    save_rc = restore_rc = release_rc = clock_rc = 0;
    publish = true; swap_after_restore = swap_after_clock = false;
    log_bytes = uploads = downloads = prepares = discards = sets = powers = ddcs = 0;
    output[0] = 0; fail_event = CASE_SET; failure_nth = failure_calls = 0;
    corrupt = revoke_after_upload = revoke_after_prepare = revoke_normal_after_prepare = false;
    failure_rc = SESSION_FAILURE; first_port_rc = 0; port_reads = 0;
    fail_restore_after_set = false;
    cursor_images = cursor_moves = cursor_shows = flips = waits = snapshots = 0;
    revoke_cursor = revoke_flip = revoke_snapshot = wrong_base = false; snapshot_rc = 0;
    foundation_ops = (struct gf1_test_ops){.production=&host.base.f, .call=snapshot_call};
    host.base.f.stats.vblank[1] = U32_MAX - 7U; /* Exercise natural wrap in 16 flips. */
    expected_ddc_bus = FIRST_DDC_BUS;
    host.links.i2c_port = port_call;
}
static struct display_request request(void)
{
    /* Literal bit oracle from A3-case: connector 0xa5, head1, phase0. */
    struct display_request q;
    assert(!display_decode(0x1a501U, &q));
    assert(q.test == DISPLAY_MODE && q.connector == 0xa5 && q.head == 1 && !q.phase);
    return q;
}
static void wire_and_admission(void)
{
    reset();
    struct display_request q = request(), before = q;
    for (u32 bit = 18; bit < BITS_PER_U32; ++bit) {
        assert(display_decode(1U << bit, &q) == GF4_EINVAL);
        assert(!gf2_host_memcmp(&q, &before, sizeof(q)));
    }
    for (u32 c = 9; c <= U8_MAX; ++c)
        assert(display_decode(c, &q) == GF4_EINVAL);
    for (u32 c = 0; c <= 8; ++c) {
        assert(!display_decode(0x3ff00U | c, &q));
        assert(q.test == c && q.connector == U8_MAX && q.head == 1 && q.phase);
    }
    assert(display_admit(&q) == 0); /* RESTORE at any time. */
    q = request(); q.phase = true;
    assert(display_admit(&q) == GF4_ESTATE && !nevents);
    q.phase = false; assert(!display_begin(&q));
    assert(nevents == 2 && events[0] == SESSION_SAVE && events[1] == SESSION_RESTORE);
    assert(display_session.valid && display_session.running && normal_slot);
    assert(display_admit(&q) == GF4_EBUSY);
    q.phase = true; assert(!display_admit(&q));
    ++q.test; assert(display_admit(&q) == GF4_EBUSY); --q.test;
    ++q.connector; assert(display_admit(&q) == GF4_EBUSY); --q.connector;
    q.head = 0; assert(display_admit(&q) == GF4_EBUSY);
    q.test = DISPLAY_RESTORE; assert(!display_admit(&q));
    assert(!display_restore(true) && !display_session.valid && !host.ctx.diagnostic_saved);
    assert(nevents == 4 && events[2] == SESSION_RESTORE && events[3] == SESSION_RELEASE);
    assert(!display_restore(true) && nevents == 4);
}
static void start_failures(void)
{
    reset(); struct display_request q = request();
    normal_slot = NULL; assert(display_begin(&q) == GF4_ESTATE && !nevents);
    normal_slot = &normal; host.ctx.saved_closed = true;
    assert(display_begin(&q) == GF4_ESTATE && !nevents);
    host.ctx.saved_closed = false; clock_rc = SESSION_FAILURE;
    assert(display_begin(&q) == SESSION_FAILURE && !nevents);
    clock_rc = 0; swap_after_clock = true;
    assert(display_begin(&q) == GF4_ESTATE && !nevents);
    swap_after_clock = false; normal_slot = &normal; management_slot = &management;
    save_rc = SESSION_FAILURE;
    assert(display_begin(&q) == SESSION_FAILURE && !display_session.valid);
    assert(nevents == 1 && events[0] == SESSION_SAVE);
    reset(); q = request(); restore_rc = SESSION_FAILURE;
    assert(display_begin(&q) == SESSION_FAILURE && display_session.valid);
    assert(!display_session.running && !normal_slot && host.ctx.diagnostic_saved);
    q.phase = true; assert(display_admit(&q) == GF4_ESTATE);
    restore_rc = 0; assert(!display_restore(true) && !display_session.valid);
    reset(); q = request(); publish = false;
    assert(display_begin(&q) == GF4_ESTATE && display_session.valid);
    assert(!display_session.running && nevents == 2);
    publish = true; assert(!display_restore(true));
}
static void restore_retries(void)
{
    reset(); struct display_request q = request(); assert(!display_begin(&q));
    static struct gf4_saved before;
    gf2_host_memcpy(&before, &display_session.saved, sizeof(before));
    management_slot = NULL;
    assert(display_restore(true) == GF4_ESTATE && nevents == 2);
    management_slot = &management; clock_rc = SESSION_FAILURE;
    assert(display_restore(true) == SESSION_FAILURE && nevents == 2);
    clock_rc = 0; swap_after_clock = true;
    assert(display_restore(true) == GF4_ESTATE && nevents == 2);
    swap_after_clock = false; management_slot = &management; restore_rc = SESSION_FAILURE;
    assert(display_restore(true) == SESSION_FAILURE && nevents == 3);
    assert(events[2] == SESSION_RESTORE && display_session.valid);
    restore_rc = 0; swap_after_restore = true;
    assert(display_restore(true) == GF4_ESTATE && nevents == 4);
    assert(display_session.valid && host.ctx.diagnostic_saved);
    swap_after_restore = false; management_slot = &management; release_rc = SESSION_FAILURE;
    assert(display_restore(true) == SESSION_FAILURE && nevents == 6);
    assert(events[5] == SESSION_RELEASE && display_session.valid);
    assert(!gf2_host_memcmp(&before, &display_session.saved, sizeof(before)));
    release_rc = 0; assert(!display_restore(true));
    assert(!display_session.valid && !display_session.running && !host.ctx.diagnostic_saved);
}
/* Independent bit-at-a-time CRC oracle (ZIP/PNG reflected polynomial),
 * rather than the product's nibble table implementation. */
static u32 buffer_crc(const u8 *data, u32 bytes)
{
    u32 crc = ~0U;
    for (u32 i = 0; i < bytes; ++i) {
        crc ^= data[i];
        for (u32 bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}
static u32 image_crc(u32 bytes) { return buffer_crc(image, bytes); }
static u32 occurrences(const char *needle)
{
    u32 n = 0;
    const char *p = output;
    while ((p = gf2_host_strstr(p, needle))) { ++n; ++p; }
    return n;
}
/* Wire literals are independent of display_decode: case1/2, connector a5,
 * head1, with phase1 adding bit17, from the final A3-case wire card. */
#define MODE_WIRE 0x1a501U
#define DPMS_WIRE 0x1a502U
#define NEXT_PHASE 0x20000U
static void mode_steps(void)
{
    reset(); assert(!gf_display_test(MODE_WIRE));
    assert(display_session.valid && prepares == 1 && sets == 1 && !discards);
    assert(events[0] == SESSION_SAVE && events[1] == SESSION_RESTORE &&
           events[2] == CASE_PREPARE && events[3] == CASE_UPLOAD &&
           events[4] == CASE_DOWNLOAD);
    assert(occurrences("READY") == 1 && gf2_host_strstr(output, "case=1 step=0"));
    /* At y=1, x=130 is blue, x=259 green; x=32 and y=0 are white.
     * Width1032 / eight bars =129 pixels. Grid lines are one pixel. */
    assert(image[IMAGE_WIDTH + 130] == 1 && image[IMAGE_WIDTH + 259] == 2);
    assert(image[IMAGE_WIDTH + 32] == 7 && image[130] == 7);
    assert(image[IMAGE_WIDTH + 33] == 0);
    /* Independent spacing oracle: kdebug/gf_display_test.c:125-129 fixes
     * 32px lines/eight bars. Halfway to the first line is black at y=1;
     * changing only the product's spacing to 16 makes this pixel white. */
    enum { EXPECTED_GRID_PIXELS = 32, EXPECTED_BAR_COUNT = 8 };
    const u32 between_lines = EXPECTED_GRID_PIXELS / 2U;
    assert(image[IMAGE_WIDTH + between_lines] ==
           between_lines * EXPECTED_BAR_COUNT / IMAGE_WIDTH);
    assert(display_session.crc == image_crc(IMAGE_WIDTH * IMAGE_HEIGHT));
    u32 before = nevents;
    assert(gf_display_test(DPMS_WIRE) == GF4_EBUSY && nevents == before);
    assert(gf_display_test(NEXT_PHASE | 0x1a401U) == GF4_EBUSY && nevents == before);
    assert(!gf_display_test(MODE_WIRE | NEXT_PHASE));
    assert(events[before] == SESSION_RESTORE && events[before+1] == CASE_DISCARD &&
           events[before+2] == CASE_PREPARE);
    assert(display_session.mode == 2 && prepares == 2 && discards == 1);
    u32 blue = IMAGE_WIDTH * 2U + 130U * 2U;
    u32 green = IMAGE_WIDTH * 2U + 259U * 2U;
    assert(image[blue] == 0x1f && image[blue+1] == 0);
    assert(image[green] == 0xe0 && image[green+1] == 0x07);
    assert(display_session.crc == image_crc(IMAGE_WIDTH * IMAGE_HEIGHT * 2U));
    assert(!gf_display_test(MODE_WIRE | NEXT_PHASE));
    assert(display_session.mode == 3 && prepares == 3 && discards == 2);
    blue = IMAGE_WIDTH * 4U + 130U * 4U;
    assert(image[blue] == 255 && !image[blue+1] && !image[blue+2] && !image[blue+3]);
    assert(display_session.crc == image_crc(sizeof(image)));
    /* XRGB8888 needs two fragments per row; INDEX8/565 need one. */
    assert(uploads == IMAGE_HEIGHT * (1U + 1U + 2U) && downloads == uploads);
    before = nevents;
    assert(!gf_display_test(MODE_WIRE | NEXT_PHASE));
    assert(!display_session.valid && !host.ctx.diagnostic_saved);
    assert(events[before] == SESSION_RESTORE && events[before+1] == CASE_DISCARD &&
           events[before+2] == SESSION_RELEASE && nevents == before+3);
    assert(occurrences("READY") == state.nmodes);
}
static void dpms_steps(void)
{
    /* Direct acceptance oracle from the fixed card, independent of the
     * product display_power table (including each intervening ON). */
    static const enum gf4_dpms expected[] = {
        GF4_ON, GF4_STANDBY, GF4_ON, GF4_SUSPEND, GF4_ON, GF4_OFF, GF4_ON
    };
    reset(); assert(!gf_display_test(DPMS_WIRE));
    for (u32 i = 1; i < sizeof(expected)/sizeof(expected[0]); ++i) {
        assert(!gf_display_test(DPMS_WIRE | NEXT_PHASE));
        assert(display_session.step == i && prepares == 1 && !discards);
    }
    assert(powers == sizeof(expected)/sizeof(expected[0]));
    for (u32 i = 0; i < powers; ++i) assert(power_seen[i] == expected[i]);
    assert(occurrences("READY") == powers);
    assert(!gf_display_test(DPMS_WIRE | NEXT_PHASE));
    assert(!display_session.valid && discards == 1 && sets == 1);
    assert(gf_display_test(DPMS_WIRE | NEXT_PHASE) == GF4_ESTATE);
}
static void enumeration(void)
{
    reset(); assert(!gf_display_test(0));
    assert(ddcs == 2 && occurrences("GF4 route") == 2);
    assert(occurrences("GF4 MODE") == state.nmodes && occurrences("GF4 DDC") == 2);
    assert(!display_session.valid && !prepares && !sets && !powers);
    assert(nevents == ddcs); /* Neither save nor any display-changing call. */
    assert(gf_display_test(NEXT_PHASE) == GF4_ESTATE);
    reset(); fail_event = CASE_DDC; failure_nth = 1;
    assert(gf_display_test(0) == SESSION_FAILURE && ddcs == 1);
    assert(gf2_host_strstr(output, "SKIP") && !gf2_host_strstr(output, "READY"));
    reset(); routes[0].i2c_index = routes[1].i2c_index = U8_MAX;
    assert(gf_display_test(0) == GF4_ENODEV && !ddcs && !display_session.valid);
    reset(); host.ctx.nroutes = 0;
    assert(gf_display_test(0) == GF4_ENODEV && !nevents);
    reset(); normal_slot = NULL;
    assert(gf_display_test(0) == GF4_ESTATE && !nevents);
}
static void enumeration_ports(void)
{
    static const i32 unavailable[] = { GF4_ENODEV, GF4_EUNSUPPORTED };
    for (u32 i = 0; i < sizeof(unavailable) / sizeof(unavailable[0]); ++i) {
        reset(); first_port_rc = unavailable[i];
        routes[1].i2c_index = expected_ddc_bus = NEXT_DDC_BUS;
        assert(!gf_display_test(0));
        assert(port_reads == host.ctx.nroutes);
        assert(ddcs == occurrences("GF4 DDC") && ddcs && !occurrences("SKIP"));
        assert(!display_session.valid && !prepares && !sets && !powers);
        reset(); first_port_rc = unavailable[i];
        assert(gf_display_test(0) == GF4_ENODEV && !ddcs);
        assert(port_reads == host.ctx.nroutes && !display_session.valid);
    }
    reset(); routes[0].i2c_index = U8_MAX;
    assert(!gf_display_test(0) && port_reads == host.ctx.nroutes - 1U && ddcs);
    reset(); routes[1].i2c_index = NEXT_DDC_BUS;
    assert(!gf_display_test(0) && port_reads == 1U && ddcs);
}
static void skipped_modes(void)
{
    static const i32 unsupported[] = { GF4_EPLL, GF4_ERANGE };
    for (u32 i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        reset(); fail_event = CASE_SET; failure_nth = 1; failure_rc = unsupported[i];
        assert(gf_display_test(MODE_WIRE) == unsupported[i]);
        assert(occurrences("SKIP") == 1U && !occurrences("READY"));
        assert(events[nevents - 1U] == SESSION_RESTORE);
        assert(display_session.valid && display_session.running &&
               display_session.prepared && host.ctx.diagnostic_saved);
        assert(!host.ctx.saved_closed && !discards);
        const u32 skipped = display_session.step, before = nevents;
        assert(!gf_display_test(MODE_WIRE | NEXT_PHASE));
        assert(display_session.step == skipped + 1U &&
               display_session.mode == state.modes[skipped + 1U].id);
        static const enum session_event advance[] = {
            SESSION_RESTORE, CASE_DISCARD, CASE_PREPARE
        };
        for (u32 j = 0; j < sizeof(advance) / sizeof(advance[0]); ++j)
            assert(events[before + j] == advance[j]);
        assert(occurrences("READY") == 1U && display_session.running);
        while (display_session.valid) assert(!gf_display_test(MODE_WIRE | NEXT_PHASE));
        assert(!host.ctx.diagnostic_saved && discards == state.nmodes);

        reset(); state.nmodes = 1; fail_event = CASE_SET;
        failure_nth = 1; failure_rc = unsupported[i];
        assert(gf_display_test(MODE_WIRE) == unsupported[i]);
        assert(!gf_display_test(MODE_WIRE | NEXT_PHASE));
        assert(!display_session.valid && !host.ctx.diagnostic_saved && !occurrences("READY"));
    }
    reset(); fail_event = CASE_SET; failure_nth = 1; failure_rc = GF4_EPLL;
    fail_restore_after_set = true;
    assert(gf_display_test(MODE_WIRE) == SESSION_FAILURE);
    assert(occurrences("SKIP") == 1U && occurrences("ERROR") == 1U);
    assert(display_session.valid && !display_session.running && host.ctx.diagnostic_saved);
    u32 before = nevents;
    assert(gf_display_test(MODE_WIRE | NEXT_PHASE) == GF4_ESTATE && nevents == before);
    restore_rc = 0; assert(!gf_display_test(DISPLAY_RESTORE));
}
static void case_failures(void)
{
    static const enum session_event failures[] = {
        CASE_PREPARE, CASE_UPLOAD, CASE_DOWNLOAD, CASE_SET, CASE_PALETTE, CASE_DPMS
    };
    for (u32 i = 0; i < sizeof(failures)/sizeof(failures[0]); ++i) {
        reset(); fail_event = failures[i]; failure_nth = 1;
        u32 wire = failures[i] == CASE_DPMS ? DPMS_WIRE : MODE_WIRE;
        assert(gf_display_test(wire) == SESSION_FAILURE);
        assert(display_session.valid && !display_session.running && host.ctx.diagnostic_saved);
        assert(events[nevents-1] == failures[i]); /* No follow-on call after failure. */
        assert(!occurrences("READY"));
        u32 before = nevents;
        assert(gf_display_test(wire | NEXT_PHASE) == GF4_ESTATE && nevents == before);
        failure_nth = 0; assert(!gf_display_test(DISPLAY_RESTORE));
        assert(!display_session.valid && !host.ctx.diagnostic_saved && discards == 1);
    }
    reset(); corrupt = true;
    assert(gf_display_test(MODE_WIRE) == GF4_ESTATE && !sets && !occurrences("READY"));
    corrupt = false; assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); revoke_after_upload = true;
    assert(gf_display_test(MODE_WIRE) == GF4_ESTATE && uploads == 1 && !downloads && !sets);
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); revoke_after_prepare = true;
    assert(gf_display_test(MODE_WIRE) == GF4_ESTATE && !uploads && !sets);
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); revoke_normal_after_prepare = true;
    assert(gf_display_test(MODE_WIRE) == GF4_ESTATE && uploads && downloads && !sets);
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); assert(!gf_display_test(MODE_WIRE));
    fail_event = CASE_DISCARD; failure_nth = 1;
    assert(gf_display_test(MODE_WIRE | NEXT_PHASE) == SESSION_FAILURE);
    assert(prepares == 1 && display_session.valid && !display_session.running);
    assert(events[nevents-1] == CASE_DISCARD);
    failure_nth = 0; assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); assert(!gf_display_test(MODE_WIRE));
    fail_event = CASE_DISCARD; failure_nth = 1;
    assert(gf_display_test(DISPLAY_RESTORE) == SESSION_FAILURE);
    assert(display_session.valid && host.ctx.diagnostic_saved && display_session.prepared);
    assert(events[nevents-1] == CASE_DISCARD);
    failure_nth = 0; assert(!gf_display_test(DISPLAY_RESTORE) && !display_session.valid);
    reset(); routes[0].head_mask = routes[1].head_mask = 1;
    assert(gf_display_test(MODE_WIRE) == GF4_ENODEV && !prepares && !sets);
    assert(occurrences("SKIP") == 1 && !occurrences("READY"));
    assert(!gf_display_test(DISPLAY_RESTORE));
}
#define CURSOR_WIRE 0x1a503U
#define FLIP_WIRE 0x1a504U
static void cursor_steps(void)
{
    reset(); assert(!gf_display_test(CURSOR_WIRE));
    assert(cursor_images == 1 && cursor_sides[0] == 32 && sets == 1);
    assert(display_session.crc == buffer_crc((const u8 *)cursor_copy, 32U*32U*sizeof(u32)));
    assert(!gf_display_test(CURSOR_WIRE | NEXT_PHASE));
    assert(cursor_images == 2 && cursor_sides[1] == 64 && sets == 1);
    assert(display_session.crc == buffer_crc((const u8 *)cursor_copy, sizeof(cursor_copy)));
    /* Four corners, then a negative hotspot position. */
    static const i32 xs[] = {0,IMAGE_WIDTH-1,0,IMAGE_WIDTH-1,-8};
    static const i32 ys[] = {0,0,IMAGE_HEIGHT-1,IMAGE_HEIGHT-1,-8};
    for (u32 i = 0; i < 5; ++i) {
        assert(!gf_display_test(CURSOR_WIRE | NEXT_PHASE));
        assert(move_x[i+2U] == xs[i] && move_y[i+2U] == ys[i]);
    }
    assert(move_x[0] == IMAGE_WIDTH/2 && move_y[0] == IMAGE_HEIGHT/2);
    u32 moves = cursor_moves, shows = cursor_shows;
    assert(!gf_display_test(CURSOR_WIRE | NEXT_PHASE));
    assert(cursor_moves == moves && !show_seen[shows]);
    assert(!gf_display_test(CURSOR_WIRE | NEXT_PHASE));
    assert(cursor_moves == moves && show_seen[shows+1U]);
    u32 before = nevents;
    assert(!gf_display_test(CURSOR_WIRE | NEXT_PHASE));
    assert(events[before] == SESSION_RESTORE && events[before+1] == CASE_DISCARD &&
           events[before+2] == CASE_PREPARE);
    assert(cursor_images == EXPECTED_CURSOR_IMAGES && cursor_sides[2] == 64 && sets == 2 && discards == 1);
    assert(display_session.mode == 2 && occurrences("READY") == EXPECTED_CURSOR_READY);
    assert(!gf_display_test(CURSOR_WIRE | NEXT_PHASE));
    assert(!display_session.valid && discards == 2 && !host.ctx.diagnostic_saved);
}
static void flip_steps(void)
{
    reset(); assert(!gf_display_test(FLIP_WIRE));
    assert(flips == EXPECTED_FLIP_FRAMES && waits == flips && snapshots == flips && prepares == 1);
    assert(occurrences("GF4 FLIP") == EXPECTED_FLIP_FRAMES && occurrences("READY") == 1);
    assert(!host.ctx.hold[1].pending && host.base.f.stats.vblank[1] == (u32)(U32_MAX - 7U + EXPECTED_FLIP_FRAMES));
    /* At x=1,y=0 the front is blue and the back red; diagonals at
     * x=32,y=0 and x=30,y=0 run in opposite directions (height=3). */
    assert(image[1] == 1 && back_image[1] == 4);
    assert(image[32] == 7 && back_image[30] == 7);
    /* Left cell in the glyph's second row is lit for 1, dark for 2. */
    assert(image[IMAGE_WIDTH + IMAGE_WIDTH/3U] == 7);
    assert(back_image[IMAGE_WIDTH + IMAGE_WIDTH/3U] == 0);
    assert(display_session.crc == image_crc(IMAGE_WIDTH*IMAGE_HEIGHT));
    assert(!gf_display_test(FLIP_WIRE | NEXT_PHASE));
    assert(flips == EXPECTED_FLIP_TOTAL && waits == flips && snapshots == flips && prepares == 1);
    for (u32 i = 0; i < EXPECTED_FLIP_TOTAL; ++i) assert(sync_seen[i] == (i < EXPECTED_FLIP_FRAMES));
    assert(occurrences("GF4 FLIP") == EXPECTED_FLIP_TOTAL && occurrences("READY") == EXPECTED_FLIP_STEPS);
    assert(gf2_host_strstr(output, "counter=0") && gf2_host_strstr(output, " base="));
    assert(!gf_display_test(FLIP_WIRE | NEXT_PHASE));
    assert(!display_session.valid && discards == 1 && !host.ctx.hold[1].pending);
}
static void operation_budgets(void)
{
    reset(); slow_operations = true;
    assert(!gf_display_test(FLIP_WIRE));
    assert(!gf_display_test(FLIP_WIRE | NEXT_PHASE));
    assert(flips == EXPECTED_FLIP_TOTAL && uploads == downloads);
    assert(session_now - SESSION_NOW > GF3_TIMEOUT_US);
    assert(!gf_display_test(DISPLAY_RESTORE));
}
static void cursor_flip_failures(void)
{
    static const enum session_event failed[] = {
        CASE_CURSOR_IMAGE, CASE_CURSOR_MOVE, CASE_CURSOR_SHOW, CASE_FLIP, CASE_VBLANK
    };
    for (u32 i = 0; i < sizeof(failed)/sizeof(failed[0]); ++i) {
        reset(); fail_event = failed[i]; failure_nth = 1;
        u32 wire = i < 3 ? CURSOR_WIRE : FLIP_WIRE;
        assert(gf_display_test(wire) == SESSION_FAILURE);
        assert(events[nevents-1] == failed[i] && !occurrences("READY"));
        assert(display_session.valid && !display_session.running && display_session.prepared);
        assert(host.ctx.diagnostic_saved && !discards);
        u32 before = nevents;
        assert(gf_display_test(wire | NEXT_PHASE) == GF4_ESTATE && nevents == before);
        failure_nth = 0; assert(!gf_display_test(DISPLAY_RESTORE));
        assert(!display_session.valid && !host.ctx.diagnostic_saved && discards == 1);
    }
    reset(); revoke_cursor = true;
    assert(gf_display_test(CURSOR_WIRE) == GF4_ESTATE && cursor_images == 1 && !cursor_moves);
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); revoke_flip = true;
    assert(gf_display_test(FLIP_WIRE) == GF4_ESTATE && flips == 1 && !waits);
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); wrong_base = true;
    assert(gf_display_test(FLIP_WIRE) == GF4_ESTATE && flips == 1 && waits == 1);
    assert(display_session.valid && !discards && !occurrences("READY"));
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); snapshot_rc = SESSION_FAILURE;
    assert(gf_display_test(FLIP_WIRE) == SESSION_FAILURE && !flips && !waits);
    assert(display_session.valid && !occurrences("READY") && !discards);
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); revoke_snapshot = true;
    assert(gf_display_test(FLIP_WIRE) == GF4_ESTATE && snapshots == 1 && !flips);
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); fail_event = CASE_FLIP; failure_nth = 5;
    assert(gf_display_test(FLIP_WIRE) == SESSION_FAILURE && flips == 5 && waits == 4);
    assert(occurrences("GF4 FLIP") == 5 && !occurrences("READY"));
    assert(events[nevents-1] == CASE_FLIP && !discards && display_session.valid);
    assert(!gf_display_test(DISPLAY_RESTORE));
    /* A back upload failure stops before any mode switch or flip. */
    reset(); fail_event = CASE_UPLOAD; failure_nth = IMAGE_HEIGHT + 1U;
    assert(gf_display_test(FLIP_WIRE) == SESSION_FAILURE && !sets && !flips);
    assert(events[nevents-1] == CASE_UPLOAD && !discards && display_session.valid);
    assert(!gf_display_test(DISPLAY_RESTORE));
    /* Async completion failure retains a pending pair until management restore. */
    reset(); assert(!gf_display_test(FLIP_WIRE));
    fail_event = CASE_VBLANK; failure_nth = 1;
    assert(gf_display_test(FLIP_WIRE | NEXT_PHASE) == SESSION_FAILURE);
    assert(host.ctx.hold[1].pending && flips == EXPECTED_FLIP_FRAMES + 1U && !discards && display_session.valid);
    restore_rc = SESSION_FAILURE;
    assert(gf_display_test(DISPLAY_RESTORE) == SESSION_FAILURE && host.ctx.hold[1].pending);
    restore_rc = 0; failure_nth = 0;
    assert(!gf_display_test(DISPLAY_RESTORE) && !host.ctx.hold[1].pending);
    /* A missing second mode is a reasoned SKIP after restore/discard. */
    reset(); state.nmodes = 1; assert(!gf_display_test(CURSOR_WIRE));
    for (u32 i = 1; i < 9; ++i) assert(!gf_display_test(CURSOR_WIRE | NEXT_PHASE));
    assert(gf_display_test(CURSOR_WIRE | NEXT_PHASE) == GF4_ENODEV);
    assert(occurrences("SKIP") == 1 && sets == 1 && discards == 1);
    assert(!gf_display_test(DISPLAY_RESTORE) && !display_session.valid);
}
/* Independent wire/camera oracles from the final A3 card: four gamma
 * pictures, two routes then a pair, OFF and ON/flip, three lifetime calls. */
#define GAMMA_WIRE 0x1a505U
#define OUTPUT_WIRE 0x1a506U
#define LIFETIME_WIRE 0x1a507U
enum { EXPECTED_GAMMA_READY = 4, EXPECTED_OUTPUT_READY = 6,
       EXPECTED_PAIR_READY = 4, EXPECTED_LIFETIME_READY = 3 };
static void gamma_log(u32 from, u32 expected)
{
    static const char key[] = " LUT checksum=";
    const char *p = gf2_host_strstr(output + from, key);
    assert(p);
    p += sizeof(key) - 1U;
    u32 value = 0;
    assert(*p >= '0' && *p <= '9');
    while (*p >= '0' && *p <= '9')
        value = value * (sizeof("0123456789") - 1U) + (*p++ - '0');
    assert(*p == '\r' && value == expected);
}
static void gamma_steps(void)
{
    reset(); assert(!gf_display_test(GAMMA_WIRE));
    gamma_log(0, gamma_checksum[0]);
    assert(gammas == 1 && prepares == 1 && sets == 1 && display_session.mode == 2);
    /* Halfway across the 565 card must contain a mid grey, so changing
     * the curve has a visible effect; RGB-cube endpoint bars would not. */
    u32 middle = IMAGE_WIDTH * 2U + (IMAGE_WIDTH / 2U) * 2U;
    assert(image[middle] == 0xef && image[middle+1U] == 0x7b);
    assert(display_session.crc == image_crc(IMAGE_WIDTH*IMAGE_HEIGHT*2U));
    u32 log_start = log_bytes, frame_crc = display_session.crc;
    assert(!gf_display_test(GAMMA_WIRE | NEXT_PHASE));
    gamma_log(log_start, gamma_checksum[1]);
    assert(gamma_checksum[0] != gamma_checksum[1] && display_session.crc == frame_crc);
    log_start = log_bytes;
    assert(!gf_display_test(GAMMA_WIRE | NEXT_PHASE));
    gamma_log(log_start, gamma_checksum[2]);
    assert(gamma_checksum[0] == gamma_checksum[2] && display_session.crc == frame_crc);
    assert(gammas == 3 && prepares == 1 && sets == 1 && !discards);
    u32 before = nevents;
    assert(!gf_display_test(GAMMA_WIRE | NEXT_PHASE));
    assert(events[before] == SESSION_RESTORE && events[before+1U] == CASE_DISCARD &&
           events[before+2U] == CASE_PREPARE);
    assert(display_session.mode == 1 && events[nevents-1U] == CASE_PALETTE);
    assert(gammas == 3 && prepares == 2 && sets == 2 && discards == 1);
    assert(occurrences(" LUT checksum=") == EXPECTED_GAMMA_APPLIES);
    assert(occurrences("READY") == EXPECTED_GAMMA_READY && image[IMAGE_WIDTH+130U] == 1);
    assert(!gf_display_test(GAMMA_WIRE | NEXT_PHASE));
    assert(!display_session.valid && !host.ctx.diagnostic_saved && discards == 2);

    reset(); state.modes[1].fmt = GF3_XRGB8888;
    state.modes[1].pitch = IMAGE_WIDTH * 4U;
    assert(!gf_display_test(GAMMA_WIRE) && gammas == 1);
    middle = IMAGE_WIDTH * 4U + (IMAGE_WIDTH / 2U) * 4U;
    assert(image[middle] == 127 && image[middle+1U] == 127 && image[middle+2U] == 127);
    assert(display_session.crc == image_crc(sizeof(image)));
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); state.nmodes = 1;
    assert(gf_display_test(GAMMA_WIRE) == GF4_EFORMAT && !gammas && !prepares);
    assert(occurrences("SKIP") == 1 && !occurrences("READY"));
    assert(!gf_display_test(DISPLAY_RESTORE));
    for (u32 nth = 1; nth <= 3; ++nth) {
        reset(); fail_event = CASE_GAMMA; failure_nth = nth;
        for (u32 i = 1; i < nth; ++i)
            assert(!gf_display_test(GAMMA_WIRE | (i == 1 ? 0 : NEXT_PHASE)));
        assert(gf_display_test(GAMMA_WIRE | (nth == 1 ? 0 : NEXT_PHASE)) == SESSION_FAILURE);
        assert(events[nevents-1U] == CASE_GAMMA && gammas == nth);
        assert(display_session.valid && !display_session.running && host.ctx.diagnostic_saved);
        assert(occurrences("READY") == nth-1U && !discards);
        assert(occurrences(" LUT checksum=") == nth-1U);
        assert(!gf_display_test(DISPLAY_RESTORE));
    }
    reset(); revoke_gamma = true;
    assert(!gf_display_test(GAMMA_WIRE) && gammas == 1);
    assert(gf_display_test(GAMMA_WIRE | NEXT_PHASE) == GF4_ESTATE && gammas == 1);
    assert(!gf_display_test(DISPLAY_RESTORE));
    reset(); assert(!gf_display_test(GAMMA_WIRE));
    assert(!gf_display_test(GAMMA_WIRE | NEXT_PHASE));
    assert(!gf_display_test(GAMMA_WIRE | NEXT_PHASE));
    fail_event = CASE_PALETTE; failure_nth = 1;
    assert(gf_display_test(GAMMA_WIRE | NEXT_PHASE) == SESSION_FAILURE);
    assert(events[nevents-1U] == CASE_PALETTE && occurrences("READY") == 3);
    assert(display_session.valid && display_session.prepared && !display_session.running);
    assert(!gf_display_test(DISPLAY_RESTORE));
}
static void outputs_reset(void)
{
    reset(); output_fixture = true;
    routes[0].head_mask = 1; routes[0].or_mask = 1;
    routes[1].connector = routes[0].connector + 1U;
    routes[1].head_mask = 2; routes[1].or_mask = 2;
}
static void outputs_pair_ready(void)
{
    assert(!gf_display_test(OUTPUT_WIRE));
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
}
static void output_budgets(void)
{
    outputs_reset(); slow_operations = true;
    outputs_pair_ready();
    assert(display_session.output_pair_ready && prepared_head[0] && prepared_head[1]);
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(!gf_display_test(DISPLAY_RESTORE));
}
static void outputs_steps(void)
{
    outputs_reset(); assert(!gf_display_test(OUTPUT_WIRE));
    assert(sets == 1 && prepared_head[0] == 1 && !prepared_head[1]);
    assert(gf2_host_strstr(output, "connector=165 mode=1 head=0"));
    u32 before = nevents;
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(events[before] == SESSION_RESTORE && events[before+1U] == CASE_DISCARD);
    assert(sets == 2 && discarded_head[0] == 1 && prepared_head[1] == 1);
    assert(gf2_host_strstr(output, "connector=166 mode=1 head=1"));
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(sets == 4 && prepared_head[0] == 2 && prepared_head[1] == 2);
    assert(discarded_head[0] == 1 && discarded_head[1] == 1);
    assert(state.head[0].mode.id == 1 && state.head[1].mode.id == 2);
    assert(head0_image[0][1] == 1); /* A blue card; B red in RGB565. */
    assert(image[2] == 0 && image[3] == 0xf8);
    assert(state.head[0].front.off != state.head[1].front.off);
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(powers == 1 && power_seen[0] == GF4_OFF && state.head[0].dpms == GF4_ON);
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(powers == 2 && power_seen[1] == GF4_ON && flips == 1 && !host.ctx.hold[1].pending);
    assert(state.head[0].front.off == GF3_STAGING_BYTES + 2U*IMAGE_BYTES);
    assert(occurrences("READY") == EXPECTED_OUTPUT_READY); /* Two singles, A/B, OFF, ON+flip. */
    before = nevents;
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(events[before] == SESSION_RESTORE && events[before+1U] == CASE_DISCARD &&
           events[before+2U] == CASE_DISCARD && events[before+3U] == SESSION_RELEASE);
    assert(!display_session.valid && !display_session.prepared_heads);
    assert(discarded_head[0] == 2 && discarded_head[1] == 2);
    /* An active snapshot on the other head is powered off for a single. */
    outputs_reset(); state.head[1].enabled = true; state.head[1].dpms = GF4_ON;
    assert(!gf_display_test(OUTPUT_WIRE));
    assert(powers == 1 && state.head[1].dpms == GF4_OFF);
    assert(!gf_display_test(DISPLAY_RESTORE) && state.head[1].dpms == GF4_ON);
}
static void outputs_failures(void)
{
    /* Every recoverable pair rejection, on either head and at either
     * allocation or modeset, must skip DPMS/flip and release only after
     * restoring. Derive counts from the configured route/head arrays. */
    static const i32 skipped[] = {GF4_ENODEV, GF4_EUNSUPPORTED, GF4_EPLL,
        GF4_ERANGE, GF4_EBUSY, GF4_ENOSPC};
    static const enum session_event pair_ops[] = {CASE_PREPARE, CASE_SET};
    for (u32 e = 0; e < sizeof(skipped) / sizeof(skipped[0]); ++e)
        for (u32 op = 0; op < sizeof(pair_ops) / sizeof(pair_ops[0]); ++op)
            for (u32 head = 0; head < GF4_HEADS; ++head) {
                outputs_reset();
                for (u32 route = 0; route < host.ctx.nroutes; ++route)
                    assert(!gf_display_test(OUTPUT_WIRE | (route ? NEXT_PHASE : 0)));
                u32 old_powers=powers, old_flips=flips;
                fail_event = pair_ops[op]; failure_nth = head + 1U; failure_rc = skipped[e];
                assert(gf_display_test(OUTPUT_WIRE | NEXT_PHASE) == skipped[e]);
                assert(occurrences("SKIP") == 1 && occurrences("READY") == host.ctx.nroutes);
                assert(!display_session.output_pair_ready && display_session.valid &&
                       display_session.running && host.ctx.diagnostic_saved);
                u32 held = display_session.prepared_heads, before = nevents;
                u32 banks = 0, old_discards = discards;
                for (u32 h = 0; h < GF4_HEADS; ++h) banks += (held >> h) & 1U;
                /* A failed prepare may retain nodes for final discard.
                 * Earlier single-route DPMS belongs to the preceding step. */
                assert(held && powers==old_powers && flips==old_flips);
                failure_nth = 0;
                assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
                assert(events[before] == SESSION_RESTORE && events[nevents-1U] == SESSION_RELEASE);
                assert(nevents - before == banks + 2U); /* restore, banks, release */
                assert(discards == old_discards + banks && powers==old_powers && flips==old_flips);
                assert(!display_session.valid && !display_session.prepared_heads &&
                       !host.ctx.diagnostic_saved && occurrences("READY") == host.ctx.nroutes);
            }
    /* A second-head failure must retain both staged banks and emit no
     * pair READY. Restore failure retains them for explicit case8 retry. */
    outputs_reset(); assert(!gf_display_test(OUTPUT_WIRE));
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    fail_event = CASE_PREPARE; failure_nth = 2;
    assert(gf_display_test(OUTPUT_WIRE | NEXT_PHASE) == SESSION_FAILURE);
    assert(events[nevents-1U] == CASE_PREPARE && occurrences("READY") == 2);
    assert(display_session.prepared_heads == 3 && host.ctx.diagnostic_saved);
    u32 discarded = discards;
    restore_rc = SESSION_FAILURE;
    assert(gf_display_test(DISPLAY_RESTORE) == SESSION_FAILURE && discards == discarded);
    assert(display_session.prepared_heads == 3 && display_session.valid);
    restore_rc = 0; assert(!gf_display_test(DISPLAY_RESTORE));
    assert(discards == discarded+2U && !display_session.valid);

    static const enum session_event failed[] = {CASE_DPMS,CASE_FLIP,CASE_DISCARD};
    for (u32 i = 0; i < sizeof(failed)/sizeof(failed[0]); ++i) {
        outputs_reset(); outputs_pair_ready();
        if (failed[i] != CASE_DPMS) assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
        fail_event = failed[i]; failure_nth = 1;
        u32 wire = failed[i] == CASE_DISCARD ? DISPLAY_RESTORE : OUTPUT_WIRE | NEXT_PHASE;
        assert(gf_display_test(wire) == SESSION_FAILURE);
        assert(events[nevents-1U] == failed[i] && display_session.valid);
        assert(host.ctx.diagnostic_saved && display_session.prepared_heads);
        failure_nth = 0; assert(!gf_display_test(DISPLAY_RESTORE));
        assert(!display_session.valid && !display_session.prepared_heads);
    }
    outputs_reset(); outputs_pair_ready(); perturb_other = true;
    assert(gf_display_test(OUTPUT_WIRE | NEXT_PHASE) == GF4_ESTATE);
    assert(occurrences("READY") == EXPECTED_PAIR_READY && display_session.valid && !display_session.running);
    assert(!gf_display_test(DISPLAY_RESTORE));
    outputs_reset(); outputs_pair_ready();
    fail_event = CASE_DISCARD; failure_nth = 2;
    assert(gf_display_test(DISPLAY_RESTORE) == SESSION_FAILURE);
    assert(display_session.prepared_heads == 2 && host.ctx.diagnostic_saved);
    u32 first_discarded = discarded_head[0];
    failure_nth = 0; assert(!gf_display_test(DISPLAY_RESTORE));
    assert(discarded_head[0] == first_discarded && !display_session.prepared_heads);
    outputs_reset(); routes[1].connector = routes[0].connector;
    assert(!gf_display_test(OUTPUT_WIRE)); assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(gf_display_test(OUTPUT_WIRE | NEXT_PHASE) == GF4_ENODEV);
    assert(occurrences("SKIP") == 1 && occurrences("READY") == 2);
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE) && !display_session.valid);
    outputs_reset(); routes[1].or_mask = routes[0].or_mask;
    assert(!gf_display_test(OUTPUT_WIRE)); assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(gf_display_test(OUTPUT_WIRE | NEXT_PHASE) == GF4_ENODEV);
    assert(!gf_display_test(DISPLAY_RESTORE));
    outputs_reset(); fail_event = CASE_SET; failure_nth = 1; failure_rc = GF4_EUNSUPPORTED;
    assert(gf_display_test(OUTPUT_WIRE) == GF4_EUNSUPPORTED);
    assert(!occurrences("READY") && occurrences("SKIP") == 1 && display_session.running);
    assert(!gf_display_test(OUTPUT_WIRE | NEXT_PHASE));
    assert(!gf_display_test(DISPLAY_RESTORE));
}
static void lifecycle_steps(void)
{
    reset(); assert(!gf_display_test(LIFETIME_WIRE));
    assert(enters == 1 && !leaves && !display_session.valid && !host.ctx.diagnostic_saved);
    assert(display_session.lifecycle && nevents == 1 && events[0] == CASE_ENTER);
    u32 before = nevents;
    assert(!gf_display_test(DISPLAY_RESTORE) && nevents == before && display_session.lifecycle);
    assert(gf_display_test(MODE_WIRE) == GF4_EBUSY);
    assert(!gf_display_test(LIFETIME_WIRE | NEXT_PHASE));
    assert(leaves == 1 && events[1] == CASE_LEAVE && !normal_slot && !host.ctx.ready);
    assert(!gf_display_test(DISPLAY_RESTORE) && !normal_slot && nevents == 2);
    assert(!gf_display_test(LIFETIME_WIRE | NEXT_PHASE));
    assert(events[2] == CASE_ENTER && nevents == 3 && enters == 2 && normal_slot);
    assert(state.head[0].enabled && state.head[0].front.reservation_id == 2);
    assert(!display_session.lifecycle && !display_session.valid && occurrences("READY") == EXPECTED_LIFETIME_READY);
    assert(!gf_display_test(DISPLAY_RESTORE) && nevents == 3);
    assert(gf_display_test(LIFETIME_WIRE | NEXT_PHASE) == GF4_ESTATE);

    reset(); host.ctx.saved_closed = true;
    assert(gf_display_test(LIFETIME_WIRE) == GF4_ESTATE && !nevents);
    reset(); enter_invalid = true;
    assert(gf_display_test(LIFETIME_WIRE) == GF4_ESTATE && enters == 1 && !leaves);
    assert(!occurrences("READY") && !display_session.lifecycle);
    reset(); assert(!gf_display_test(LIFETIME_WIRE)); leave_failed = true;
    assert(gf_display_test(LIFETIME_WIRE | NEXT_PHASE) == GF4_ESTATE);
    assert(enters == 1 && leaves == 1 && normal_slot && !display_session.lifecycle);
    reset(); assert(!gf_display_test(LIFETIME_WIRE));
    assert(!gf_display_test(LIFETIME_WIRE | NEXT_PHASE)); enter_invalid = true;
    assert(gf_display_test(LIFETIME_WIRE | NEXT_PHASE) == GF4_ESTATE);
    assert(enters == 2 && !display_session.valid && !display_session.lifecycle);
    assert(occurrences("READY") == 2 && !host.ctx.diagnostic_saved);
    reset(); gfx_backend_geforce.enter = NULL;
    assert(gf_display_test(LIFETIME_WIRE) == GF4_EUNSUPPORTED && !enters);
    assert(occurrences("SKIP") == 1 && !occurrences("READY"));
}

static void reset_cold_cui(void)
{
    reset();
    host.ctx.ready = false;
    /* Production idle/complete stop has neither display dispatch. */
    normal_slot = NULL; management_slot = NULL;
    gfx_backend_geforce.bb_base = NULL;
    gfx_backend_geforce.selected_mode = NULL;
}
static void cui_controls(void)
{
    reset_cold_cui();
    assert(gf_display_test(GF4_CUI_LEAVE) == GF4_ESTATE && !leaves);
    heat_clear_calls=0; heat_clear_rc=GF4_EBUSY;
    assert(gf_display_test(GF4_CUI_ENTER)==GF4_EBUSY && !enters && !display_cui_owner);
    heat_clear_rc=0;
    assert(!gf_display_test(GF4_CUI_ENTER) && enters == 1 && display_cui_owner == 2);
    assert(heat_clear_calls==2 && core_inits == 1 && !backend_failed);
    assert(gf_display_test(GF4_CUI_ENTER) == GF4_EBUSY && enters == 1);
    current_owner = 3;
    assert(gf_display_test(GF4_CUI_LEAVE) == GF4_ESTATE && !leaves);
    gf_display_owner_exit(3); assert(!leaves && display_cui_owner == 2);
    current_owner = 2;
    assert(!gf_display_test(MODE_WIRE) && display_session.valid);
    assert(!gf_display_test(GF4_CUI_LEAVE) && leaves == 1 && !normal_slot);
    assert(core_shutdowns == 1);
    assert(!display_cui_owner && !display_session.valid);
    assert(gf_display_test(GF4_CUI_LEAVE) == GF4_ESTATE && leaves == 1);
    assert(!gf_display_test(GF4_CUI_ENTER) && enters == 2);
    gf_display_owner_exit(2);
    assert(leaves == 2 && !display_cui_owner && !host.ctx.ready);
    assert(core_shutdowns == 2);
    gf_display_owner_exit(2); assert(leaves == 2);
    reset_cold_cui();
    assert(!gf_display_test(GF4_CUI_ENTER));
    /* hello_gfx shuts down the core while the shell owner survives. */
    gfx_shutdown();
    assert(!normal_slot); /* Full production stop closes the normal slot. */
    assert(!host.ctx.ready && display_cui_owner == 2);
    assert(!gf_display_test(GF4_CUI_ENTER) && enters == 2 && display_cui_owner == 2);
    assert(!gf_display_test(GF4_CUI_LEAVE) && leaves == 2);
    reset_cold_cui();
    fail_event = CASE_ENTER; failure_nth = 1;
    assert(gf_display_test(GF4_CUI_ENTER) == GF4_ESTATE && !display_cui_owner && leaves == 1);
    assert(!backend_failed && core_shutdowns == 1);
    /* A failure before backend init leaves it usable by the next gfx app. */
    reset_cold_cui(); core_reject = true;
    assert(gf_display_test(GF4_CUI_ENTER) == GF4_ESTATE && !display_cui_owner);
    assert(!enters && !backend_failed && core_shutdowns == 1);
    core_reject = false; gfx_init();
    assert(host.ctx.ready && !backend_failed && enters == 1);
    gfx_shutdown();
    reset(); assert(gf_display_test(GF4_CUI_ENTER) == GF4_EBUSY && !enters);
    reset_cold_cui();
    assert(!gf_display_test(GF4_CUI_ENTER)); leave_failed = true;
    assert(gf_display_test(GF4_CUI_LEAVE) == GF4_ESTATE && display_cui_owner == 2);
    leave_failed = false; assert(!gf_display_test(GF4_CUI_LEAVE));
    reset_cold_cui();
    assert(!gf_display_test(GF4_CUI_ENTER));
    restore_rc = SESSION_FAILURE;
    assert(gf_display_test(MODE_WIRE) == SESSION_FAILURE && display_session.valid);
    forced_leave = true;
    assert(gf_display_test(GF4_CUI_LEAVE) == SESSION_FAILURE);
    assert(leaves == 1 && core_shutdowns == 1 && !host.ctx.ready);
    reset_cold_cui();
    assert(!gf_display_test(GF4_CUI_ENTER));
    assert(!gf_display_test(MODE_WIRE));
    restore_rc = SESSION_FAILURE; forced_leave = true;
    gf_display_owner_exit(2);
    assert(!display_cui_owner && !host.ctx.ready && leaves == 1);
    assert(!display_session.valid && !host.ctx.diagnostic_saved && abandons==1);
    assert(!gf_display_test(GF4_CUI_ENTER));
    assert(!gf_display_test(GF4_CUI_LEAVE));
    /* Terminal release failure preserves CUI owner and session for retry. */
    reset_cold_cui();
    assert(!gf_display_test(GF4_CUI_ENTER));
    assert(!gf_display_test(MODE_WIRE));
    restore_rc=SESSION_FAILURE; forced_leave=true; abandon_rc=GF3_EBUSY;
    assert(gf_display_test(GF4_CUI_LEAVE)==SESSION_FAILURE);
    assert(display_cui_owner==2 && display_session.valid && host.ctx.cleanup_pending);
    assert(!display_session.running && !display_session.lifecycle && abandons==1);
    assert(gf2_host_strstr(output,"stage=leave_cleanup rc=-306"));
    assert(gf_display_test(GF4_CUI_ENTER)==GF4_EBUSY);
    /* Stopped pending cleanup bypasses restore, then retries current slot. */
    u32 restores_before=nevents;
    abandon_rc=0;
    assert(!gf_display_test(GF4_CUI_LEAVE));
    assert(nevents==restores_before+1U && events[restores_before]==CASE_LEAVE);
    assert(abandons==2 && !display_cui_owner);
    assert(!display_session.valid && !host.ctx.cleanup_pending);
    assert(!gf_display_test(GF4_CUI_ENTER));
    assert(!gf_display_test(GF4_CUI_LEAVE));
    reset();
    assert(gf_display_test(MODE_WIRE | NEXT_PHASE) == GF4_ESTATE);
    assert(gf2_host_strstr(output, "GF4 ERROR stage=display_admit rc=-408"));
    reset(); normal_slot = NULL;
    assert(gf_display_test(MODE_WIRE) == GF4_ESTATE);
    assert(gf2_host_strstr(output, "GF4 ERROR stage=display_begin rc=-408"));
}

void _start(void)
{
    cui_controls(); wire_and_admission(); start_failures(); restore_retries();
    mode_steps(); dpms_steps(); enumeration(); enumeration_ports();
    skipped_modes(); case_failures();
    cursor_steps(); flip_steps(); operation_budgets(); cursor_flip_failures();
    gamma_steps(); output_budgets(); outputs_steps(); outputs_failures(); lifecycle_steps();
    gf2_host_exit(0);
}
