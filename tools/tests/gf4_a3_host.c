/* A3 wire/session/case0..2: UNIT 14 supplies helpers, no dependency stubs.
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
    CASE_PALETTE, CASE_DPMS, CASE_DDC };
/* One start + initial restore + final restore/release, with retry room. */
#define SESSION_EVENTS (GF4_MODES * 16U)
#define SESSION_NOW 123U
#define SESSION_FAILURE (-777)
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
static u8 image[IMAGE_BYTES];
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
    if (e == fail_event && ++failure_calls == failure_nth) return failure_rc;
    return 0;
}
static int modes_call(struct gf4_ctx *ctx, u8 connector,
                      const struct gf4_mode **m, u32 *n)
{
    assert(ctx == &host.ctx && connector == routes[0].connector);
    *m = state.modes; *n = state.nmodes; return 0;
}
static i32 prepare_call(struct gf4_ctx *ctx, u8 head, u32 id,
                        struct gf3_surface out[2], u32 dl)
{
    assert(ctx == &host.ctx && head == 1 && id >= 1 && id <= state.nmodes);
    assert(dl == SESSION_NOW + GF3_TIMEOUT_US);
    ++prepares;
    i32 rc = case_event(CASE_PREPARE);
    if (rc) return rc;
    const struct gf4_mode *m = &state.modes[id - 1U];
    out[0] = (struct gf3_surface){.off=GF3_STAGING_BYTES, .bytes=m->pitch*m->t.vd,
        .pitch=m->pitch, .width=m->t.hd, .height=m->t.vd, .fmt=m->fmt,
        .reservation_id=id, .generation=1};
    out[1] = out[0]; out[1].off += IMAGE_BYTES; ++out[1].reservation_id;
    if (revoke_after_prepare) accel_slot = NULL;
    if (revoke_normal_after_prepare) normal_slot = NULL;
    return 0;
}
static i32 discard_call(struct gf4_ctx *ctx, u8 head, u32 dl)
{
    assert(ctx == &host.ctx && head == 1 && !ctx->saved_closed);
    assert(dl == SESSION_NOW + GF3_TIMEOUT_US);
    ++discards; return case_event(CASE_DISCARD);
}
static int upload_call(struct gf3_ctx *ctx, const struct gf3_surface *s,
                       struct gf3_rect r, const struct gf3_ram *ram, bool force, u32 dl)
{
    assert(ctx == &host.base.ctx && force && r.h == 1 && s->reservation_id);
    assert(dl == SESSION_NOW + GF3_TIMEOUT_US);
    u32 cpp = s->fmt == GF3_INDEX8 ? 1U : s->fmt == GF3_RGB565 ? 2U : 4U;
    assert(ram->bytes == r.w * cpp && ram->pitch == ram->bytes);
    assert(r.x + r.w <= s->width && r.y < s->height);
    u32 off = r.y * s->pitch + r.x * cpp;
    assert(off + ram->bytes <= sizeof(image));
    ++uploads;
    i32 rc = case_event(CASE_UPLOAD);
    if (rc) return rc;
    gf2_host_memcpy(image + off, ram->ptr, ram->bytes);
    if (revoke_after_upload) accel_slot = NULL;
    return 0;
}
static int download_call(struct gf3_ctx *ctx, const struct gf3_surface *s,
                         struct gf3_rect r, struct gf3_ram *ram, u32 dl)
{
    assert(ctx == &host.base.ctx && dl == SESSION_NOW + GF3_TIMEOUT_US);
    ++downloads;
    i32 rc = case_event(CASE_DOWNLOAD);
    if (rc) return rc;
    u32 cpp = s->fmt == GF3_INDEX8 ? 1U : s->fmt == GF3_RGB565 ? 2U : 4U;
    gf2_host_memcpy(ram->ptr, image + r.y * s->pitch + r.x * cpp, ram->bytes);
    if (corrupt) ((u8 *)ram->ptr)[0] ^= 1U;
    if (cpp == 4) /* Hardware may return any unused X byte. */
        for (u32 x = 0; x < r.w; ++x) ((u8 *)ram->ptr)[x * cpp + 3U] = U8_MAX;
    return 0;
}
static int set_call(struct gf4_ctx *ctx, const struct gf4_route *r, u32 id,
                    const struct gf3_surface *s, u32 dl)
{
    assert(ctx == &host.ctx && r->connector == routes[0].connector && r->head == 1);
    assert(s->reservation_id == id && dl == SESSION_NOW + GF3_TIMEOUT_US);
    assert(uploads == downloads && uploads);
    ++sets;
    i32 rc = case_event(CASE_SET);
    if (rc && fail_restore_after_set) restore_rc = SESSION_FAILURE;
    return rc;
}
static i32 palette_call(struct gf4_ctx *ctx, u8 head, u32 first, u32 n, const u8 *rgb)
{
    assert(ctx == &host.ctx && head == 1 && first == 0 && n == 8);
    /* RGB cube endpoint oracle, independent of the product palette. */
    for (u32 i = 0; i < n; ++i)
        for (u32 c = 0; c < 3; ++c)
            assert(rgb[i * 3 + c] == ((i & (4U >> c)) ? 255 : 0));
    return case_event(CASE_PALETTE);
}
static int dpms_call(struct gf4_ctx *ctx, u8 head, enum gf4_dpms power, u32 dl)
{
    assert(ctx == &host.ctx && head == 1 && dl == SESSION_NOW + GF3_TIMEOUT_US);
    assert(powers < sizeof(power_seen) / sizeof(power_seen[0]));
    power_seen[powers++] = power; return case_event(CASE_DPMS);
}
static int ddc_call(struct gf4_ctx *ctx, u8 bus, u8 block, u8 out[128], u32 dl)
{
    assert(ctx == &host.ctx && bus == expected_ddc_bus && block == ddcs);
    assert(dl == SESSION_NOW + GF3_TIMEOUT_US);
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
    *now = SESSION_NOW;
    if (swap_after_clock) {
        normal_slot = &replacement;
        management_slot = &replacement_management;
    }
    return clock_rc;
}
static i32 save_call(struct gf4_ctx *ctx, struct gf4_saved *saved, u32 dl)
{
    event(SESSION_SAVE, ctx, saved);
    assert(dl == SESSION_NOW + GF3_TIMEOUT_US);
    if (save_rc) return save_rc;
    saved->version = GF4_SAVED_VERSION; saved->size = sizeof(*saved);
    ctx->diagnostic_saved = saved; ctx->saved_closed = true;
    normal_slot = NULL;
    return 0;
}
static i32 restore_call(struct gf4_ctx *ctx, const struct gf4_saved *saved, u32 dl)
{
    event(SESSION_RESTORE, ctx, saved);
    assert(dl == SESSION_NOW + GF3_TIMEOUT_US);
    assert(saved->version == GF4_SAVED_VERSION && saved->size == sizeof(*saved));
    assert(ctx->diagnostic_saved == saved && display_session.valid);
    if (swap_after_restore) management_slot = &replacement_management;
    if (restore_rc) return restore_rc;
    ctx->saved_closed = false;
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
static void reset(void)
{
    gf4_host_init(&host, &state);
    gf2_host_memset(&display_session, 0, sizeof(display_session));
    normal = (struct gf4_test_ops){.ctx=&host.ctx, .display_save=save_call};
    normal.modes = modes_call; normal.scan_prepare = prepare_call;
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
        .restore=restore_call, .saved_release=release_call};
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
static u32 image_crc(u32 bytes)
{
    u32 crc = ~0U;
    for (u32 i = 0; i < bytes; ++i) {
        crc ^= image[i];
        for (u32 bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}
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
    reset(); assert(gf_display_test(3) == GF4_EUNSUPPORTED && !nevents);
    assert(gf2_host_strstr(output, "SKIP") && !display_session.valid);
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
void _start(void)
{
    wire_and_admission(); start_failures(); restore_retries();
    mode_steps(); dpms_steps(); enumeration(); enumeration_ports();
    skipped_modes(); case_failures();
    gf2_host_exit(0);
}
