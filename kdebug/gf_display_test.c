/* GF_FEAT4_DESIGN final 2026-11-04 addendum, A3 wire/session/case0..7.
 * OS32's staged camera protocol has no nouveau equivalent. Hardware work
 * stays behind the borrowed module ops; C2 owns all scan reservations. */
#include "gf_display_test.h"
#include "gf_command_wire.h"
#include "fs/fd_redirect.h"
#include "../hwdev/gf/display/api.h"
#include "../hwdev/gf/gf1_test.h"
#include "../drivers/video/geforce/geforce_module.h"
#include "../lib/serial_diag.h"
#include "../lib/crc32_core.inc"

/* Frozen addendum declarations; C3 owns their eventual shared-header lines. */
extern const struct gf4_test_ops *geforce_display_ops(void);
extern const struct gf4_restore_ops *geforce_display_restore_ops(void);

/* Wire bits are fixed by A3-case, rather than hardware fields. */
#define DISPLAY_BYTE_MASK 0xffU
#define DISPLAY_CONNECTOR_SHIFT 8U
#define DISPLAY_HEAD_SHIFT 16U
#define DISPLAY_PHASE_SHIFT 17U
#define DISPLAY_SESSION_MASK 0x3ffffU
enum display_case { DISPLAY_ENUM, DISPLAY_MODE, DISPLAY_DPMS, DISPLAY_CURSOR,
    DISPLAY_FLIP, DISPLAY_GAMMA, DISPLAY_OUTPUTS, DISPLAY_LIFECYCLE,
    DISPLAY_RESTORE };
struct display_request { u8 test, connector, head; bool phase; };
static struct {
    struct gf4_saved saved;
    struct display_request request;
    u32 step, mode, crc;
    struct gf3_surface scan[2];
    u32 scan_crc[2];
    u32 prepared_heads;
    bool valid, running, prepared, lifecycle, output_pair_ready;
    struct gf3_surface output_scan[GF4_HEADS][2];
    u32 output_crc[GF4_HEADS][2];
    struct display_request output_request[GF4_HEADS];
} display_session;

static inline i32 display_decode(u32 wire, struct display_request *out)
{
    if ((wire & ~DISPLAY_SESSION_MASK) ||
        (wire & DISPLAY_BYTE_MASK) > DISPLAY_RESTORE) return GF4_EINVAL;
    *out = (struct display_request){
        .test = wire & DISPLAY_BYTE_MASK,
        .connector = (wire >> DISPLAY_CONNECTOR_SHIFT) & DISPLAY_BYTE_MASK,
        .head = (wire >> DISPLAY_HEAD_SHIFT) & 1U,
        .phase = (wire >> DISPLAY_PHASE_SHIFT) & 1U
    };
    return 0;
}
/* A saved image survives both restore and saved_release failures. A session
 * cannot advance after a failed initial restore/normal-slot publication. */
static inline i32 display_admit(const struct display_request *q)
{
    if (q->test == DISPLAY_RESTORE) return 0;
    if (!display_session.valid && !display_session.lifecycle)
        return q->phase ? GF4_ESTATE : 0;
    const struct display_request *old = &display_session.request;
    if (!q->phase || q->test != old->test || q->connector != old->connector ||
        q->head != old->head) return GF4_EBUSY;
    return display_session.running ? 0 : GF4_ESTATE;
}
static inline i32 display_deadline(struct gf4_ctx *ctx, u32 *dl)
{
    if (!ctx || !ctx->ch || !ctx->ch->f) return GF4_ESTATE;
    struct gf_foundation *f = ctx->ch->f;
    u32 now;
    if (!f->io.timing.clock_us) return GF4_ESTATE;
    i32 rc = f->io.timing.clock_us(f->io.opaque, &now);
    if (rc) return rc;
    /* F2/F3's existing bounded foreground operation budget. */
    *dl = now + GF3_TIMEOUT_US;
    return 0;
}
static i32 display_discard(u8 head, u32 dl);
static inline i32 display_restore(bool finish)
{
    if (!display_session.valid) return 0;
    display_session.running = false;
    const struct gf4_restore_ops *o = geforce_display_restore_ops();
    u32 dl;
    if (!o || !o->restore) return GF4_ESTATE;
    i32 rc = display_deadline(o->ctx, &dl);
    if (rc) return rc;
    /* clock and each module call may invalidate a borrowed slot. */
    o = geforce_display_restore_ops();
    if (!o || !o->ctx || !o->restore) return GF4_ESTATE;
    rc = o->restore(o->ctx, &display_session.saved, dl);
    if (rc) return rc;
    if (finish) {
        if (display_session.prepared) {
            for (u8 head = 0; head < GF4_HEADS; ++head) {
                if (!(display_session.prepared_heads & (1U << head))) continue;
                rc = display_discard(head, dl);
                if (rc) return rc;
                display_session.prepared_heads &= ~(1U << head);
            }
            display_session.prepared = false;
        }
        o = geforce_display_restore_ops();
        if (!o || !o->ctx || !o->saved_release) return GF4_ESTATE;
        rc = o->saved_release(o->ctx, &display_session.saved);
        if (rc) return rc;
        display_session.valid = false;
    } else {
        const struct gf4_test_ops *normal = geforce_display_ops();
        if (!normal || !normal->ctx) return GF4_ESTATE;
        display_session.running = true;
    }
    return 0;
}
static inline i32 display_begin(const struct display_request *q)
{
    i32 rc = display_admit(q);
    if (rc) return rc;
    if (q->test == DISPLAY_ENUM || q->test == DISPLAY_RESTORE || q->phase)
        return GF4_EINVAL; /* Only image-case phase0 starts a snapshot. */
    const struct gf4_test_ops *o = geforce_display_ops();
    u32 dl;
    if (!o || !o->ctx || !o->display_save) return GF4_ESTATE;
    /* Final addendum (b): F5 owns a closed save without diagnostic_saved. */
    if (o->ctx->saved_closed && !o->ctx->diagnostic_saved) return GF4_ESTATE;
    rc = display_deadline(o->ctx, &dl);
    if (rc) return rc;
    o = geforce_display_ops();
    if (!o || !o->ctx || !o->display_save) return GF4_ESTATE;
    if (o->ctx->saved_closed && !o->ctx->diagnostic_saved) return GF4_ESTATE;
    rc = o->display_save(o->ctx, &display_session.saved, dl);
    if (rc) return rc;
    display_session.request = *q;
    display_session.step = display_session.mode = display_session.crc = 0;
    display_session.prepared = false;
    display_session.prepared_heads = 0;
    display_session.output_pair_ready = false;
    display_session.valid = true;
    return display_restore(false);
}

/* Diagnostic image choices, not register values: eight RGB-cube bars and
 * a white grid of one-pixel lines, spaced every 32 pixels. One staging-size
 * row fragment also covers modes wider than the static buffer. */
#define DISPLAY_COLORS 8U
#define DISPLAY_GRID 32U
#define DISPLAY_EDID_BYTES 128U
static u8 display_row[GF3_STAGING_BYTES];
static const u8 display_palette[DISPLAY_COLORS * 3U] = {
    0,0,0, 0,0,255, 0,255,0, 0,255,255,
    255,0,0, 255,0,255, 255,255,0, 255,255,255
};
static const enum gf4_dpms display_power[] = {
    GF4_ON, GF4_STANDBY, GF4_ON, GF4_SUSPEND, GF4_ON, GF4_OFF, GF4_ON
};
static void display_number(const char *key, u32 n)
{
    serial_puts_polled(key); serial_diag_u32(n);
}
static void display_record(const struct display_request *q, const char *status,
                           u32 step, u32 mode, u32 crc, i32 rc)
{
    if (serial_gate_active()) return;
    serial_puts_polled("GF4 "); serial_puts_polled(status);
    display_number(" case=", q->test); display_number(" step=", step);
    display_number(" connector=", q->connector); display_number(" mode=", mode);
    display_number(" head=", q->head); serial_puts_polled(" CRC=");
    serial_diag_hex(crc, 8); serial_puts_polled(" rc="); serial_diag_i32(rc);
    serial_puts_polled("\r\n");
}
/* Copy values before another module call. A connector's mode table has no
 * head; the selected route/head is checked by set_mode (final B0 decision).
 * nouveau dispnv04/crtc.c:129-151 owns the corresponding PLL selection. */
static i32 display_mode_at(u8 connector, u32 index, struct gf4_mode *mode,
                           u32 *count)
{
    const struct gf4_test_ops *o = geforce_display_ops();
    const struct gf4_mode *m;
    u32 n;
    if (!o || !o->ctx || !o->modes) return GF4_ESTATE;
    i32 rc = o->modes(o->ctx, connector, &m, &n);
    if (rc) return rc;
    *count = n;
    if (index >= n) return 0;
    if (!m) return GF4_ESTATE;
    *mode = m[index];
    return 0;
}
static i32 display_route(const struct display_request *q, struct gf4_route *out)
{
    const struct gf4_test_ops *o = geforce_display_ops();
    if (!o || !o->ctx) return GF4_ESTATE;
    for (u32 i = 0; i < o->ctx->nroutes; ++i) {
        const struct gf4_route *r = &o->ctx->routes[i];
        if (r->connector == q->connector && (r->head_mask & (1U << q->head))) {
            *out = *r; out->head = q->head;
            return 0;
        }
    }
    return GF4_ENODEV;
}
/* Non-destructive case0: enumerate each connector once, with all its modes
 * and base/extension EDID CRCs from the first valid route port. No detect:
 * that rebuilds the mode table and is forbidden on an active display. */
static i32 display_enumerate(const struct display_request *q)
{
    const struct gf4_test_ops *o = geforce_display_ops();
    if (!o || !o->ctx) return GF4_ESTATE;
    u32 nroutes = o->ctx->nroutes;
    if (!nroutes) {
        display_record(q, "SKIP", 0, 0, 0, GF4_ENODEV);
        return GF4_ENODEV;
    }
    i32 result = 0;
    for (u32 i = 0; i < nroutes; ++i) {
        o = geforce_display_ops();
        if (!o || !o->ctx || i >= o->ctx->nroutes) return GF4_ESTATE;
        struct display_request item = *q;
        item.connector = o->ctx->routes[i].connector;
        if (!serial_gate_active()) {
            const struct gf4_route r = o->ctx->routes[i];
            serial_puts_polled("GF4 route");
            display_number(" connector=", r.connector); display_number(" heads=", r.head_mask);
            display_number(" type=", r.type); display_number(" bus=", r.i2c_index);
            serial_puts_polled("\r\n");
        }
        bool seen = false;
        for (u32 j = 0; j < i; ++j)
            if (o->ctx->routes[j].connector == item.connector) seen = true;
        if (seen) continue;
        u8 bus = U8_MAX;
        for (u32 j = i; j < nroutes; ++j) {
            o = geforce_display_ops();
            if (!o || !o->ctx || j >= o->ctx->nroutes) return GF4_ESTATE;
            const struct gf4_route r = o->ctx->routes[j];
            if (r.connector != item.connector || r.i2c_index == U8_MAX) continue;
            const struct gf4_links *links = o->ctx->links;
            if (!links || !links->i2c_port) return GF4_ESTATE;
            struct gf4_i2c_port port;
            i32 rc = links->i2c_port(links->owner, r.i2c_index, &port);
            if (!rc) { bus = r.i2c_index; break; }
            if (rc != GF4_ENODEV && rc != GF4_EUNSUPPORTED) return rc;
        }
        u32 n = 0;
        for (u32 j = 0;; ++j) {
            struct gf4_mode m;
            i32 rc = display_mode_at(item.connector, j, &m, &n);
            if (rc) return rc;
            if (j >= n) break;
            display_record(&item, "MODE", j, m.id, 0, 0);
            if (!serial_gate_active()) {
                serial_puts_polled("GF4 timing");
                display_number(" width=", m.t.hd); display_number(" height=", m.t.vd);
                display_number(" khz=", m.t.khz); display_number(" fmt=", m.fmt);
                display_number(" pitch=", m.pitch); serial_puts_polled("\r\n");
            }
        }
        if (!n) {
            display_record(&item, "SKIP", 0, 0, 0, GF4_ENODEV);
            if (!result) result = GF4_ENODEV;
        }
        u32 blocks = 1, dl = 0;
        o = geforce_display_ops();
        if (!o || !o->ctx || !o->ddc_read) return GF4_ESTATE;
        /* One connector probe shares one absolute deadline across blocks. */
        i32 probe_rc = bus == U8_MAX ? GF4_ENODEV : display_deadline(o->ctx, &dl);
        for (u32 block = 0; block < blocks; ++block) {
            i32 rc = probe_rc;
            if (!rc) {
                o = geforce_display_ops();
                if (!o || !o->ctx || !o->ddc_read) return GF4_ESTATE;
                rc = o->ddc_read(o->ctx, bus, (u8)block, display_row, dl);
            }
            u32 crc = rc ? 0 : crc32_core_final(crc32_core_update(
                CRC32_INIT, display_row, DISPLAY_EDID_BYTES));
            display_record(&item, rc ? "SKIP" : "DDC", block, 0, crc, rc);
            if (rc) { if (!result) result = rc; break; }
            /* EDID base byte126 is the extension count (0..255). */
            if (!block) blocks += display_row[126];
        }
    }
    return result;
}
static i32 display_discard(u8 head, u32 dl)
{
    const struct gf4_test_ops *o = geforce_display_ops();
    if (!o || !o->ctx || !o->scan_discard) return GF4_ESTATE;
    return o->scan_discard(o->ctx, head, dl);
}
/* Pattern 0 is the mode card. Patterns 1/2 are numbered flip cards;
 * the 3x5 glyphs below encode decimal 1 and 2, with opposite diagonals.
 * Pattern 3 is a horizontal grey ramp for the gamma camera steps. */
enum display_pattern { DISPLAY_BARS, DISPLAY_NUMBER_ONE, DISPLAY_NUMBER_TWO, DISPLAY_GREY };
static u32 display_flip_color(u32 x, u32 y, u32 w, u32 h, u32 pattern)
{
    static const u8 glyph[2][5] = {{2,6,2,2,7}, {7,1,7,4,7}};
    u32 gx = x * 9U / w, gy = y * 9U / h;
    if (gx >= 3U && gx < 6U && gy >= 2U && gy < 7U)
        return (glyph[pattern - 1U][gy - 2U] & (4U >> (gx - 3U))) ? 7U : 0U;
    u32 diagonal = pattern == 1U ? x + y : x + h - 1U - y;
    return diagonal % DISPLAY_GRID == 0 ? 7U : pattern == 1U ? 1U : 4U;
}
/* No MMIO here. F3 force_m2mf drains each fragment before reuse/set_mode.
 * MIT model: nouveau_exa.c:242-288,305-371, via accel_transfer.c:85-116. */
static i32 display_draw(const struct gf3_surface *s, u32 dl, u32 *crc, u32 pattern)
{
    u32 cpp;
    if (s->fmt == GF3_INDEX8) cpp = 1;
    else if (s->fmt == GF3_RGB565) cpp = 2;
    else if (s->fmt == GF3_XRGB8888) cpp = 4;
    else return GF4_EFORMAT;
    if (!s->width || !s->height) return GF4_ERANGE;
    u32 expected = CRC32_INIT, actual = CRC32_INIT;
    for (u32 y = 0; y < s->height; ++y) {
        for (u32 x = 0; x < s->width;) {
            u32 w = s->width - x;
            if (w > sizeof(display_row) / cpp) w = sizeof(display_row) / cpp;
            for (u32 j = 0; j < w; ++j) {
                u32 color = ((x + j) % DISPLAY_GRID == 0 || y % DISPLAY_GRID == 0)
                    ? DISPLAY_COLORS - 1U : (x + j) * DISPLAY_COLORS / s->width;
                if (pattern == DISPLAY_NUMBER_ONE || pattern == DISPLAY_NUMBER_TWO)
                    color = display_flip_color(x + j, y, s->width, s->height, pattern);
                u8 shade[3];
                const u8 *rgb = &display_palette[color * 3U];
                if (pattern == DISPLAY_GREY) {
                    shade[0] = shade[1] = shade[2] = (x + j) * U8_MAX / s->width;
                    rgb = shade;
                }
                u32 pixel = cpp == 1 ? color : cpp == 2 ?
                    ((u32)(rgb[0] >> 3) << 11) | ((u32)(rgb[1] >> 2) << 5) |
                    (rgb[2] >> 3) : ((u32)rgb[0] << 16) | ((u32)rgb[1] << 8) | rgb[2];
                for (u32 b = 0; b < cpp; ++b)
                    display_row[j * cpp + b] = pixel >> (b * 8U);
            }
            struct gf3_rect r = {x, y, w, 1};
            struct gf3_ram ram = {display_row, w * cpp, w * cpp};
            expected = crc32_core_update(expected, display_row, ram.bytes);
            const struct gf3_test_ops *a = geforce_accel_ops();
            if (!a || !a->ctx || !a->upload) return GF4_ESTATE;
            i32 rc = a->upload(a->ctx, s, r, &ram, true, dl);
            if (rc) return rc;
            a = geforce_accel_ops();
            if (!a || !a->ctx || !a->download) return GF4_ESTATE;
            rc = a->download(a->ctx, s, r, &ram, dl);
            if (rc) return rc;
            /* XRGB's unused byte is outside the visual comparison. */
            if (cpp == 4)
                for (u32 j = 0; j < w; ++j) display_row[j * cpp + 3U] = 0;
            actual = crc32_core_update(actual, display_row, ram.bytes);
            x += w;
        }
    }
    if (actual != expected) return GF4_ESTATE;
    *crc = crc32_core_final(actual);
    return 0;
}
static i32 display_image(const struct display_request *q, u32 index, u32 dl,
                         const struct gf4_route *selected)
{
    struct gf4_mode m;
    struct gf4_route route;
    struct gf3_surface scan[2];
    u32 n;
    i32 rc = display_mode_at(q->connector, index, &m, &n);
    if (rc) return rc;
    if (index >= n) return GF4_ENODEV;
    display_session.mode = m.id;
    if (selected) route = *selected;
    else {
        rc = display_route(q, &route);
        if (rc) return rc;
    }
    const struct gf4_test_ops *o = geforce_display_ops();
    if (!o || !o->ctx || !o->scan_prepare) return GF4_ESTATE;
    display_session.prepared = true; /* A failed prepare may retain nodes. */
    display_session.prepared_heads |= 1U << q->head;
    rc = o->scan_prepare(o->ctx, q->head, m.id, scan, dl);
    if (rc) return rc;
    display_session.scan[0] = scan[0]; display_session.scan[1] = scan[1];
    rc = display_draw(&scan[0], dl, &display_session.crc,
                      q->test == DISPLAY_FLIP ? 1U :
                      q->test == DISPLAY_OUTPUTS ? q->head + 1U :
                      q->test == DISPLAY_GAMMA && m.fmt != GF3_INDEX8 ? DISPLAY_GREY : DISPLAY_BARS);
    if (rc) return rc;
    display_session.scan_crc[0] = display_session.crc;
    if (q->test == DISPLAY_FLIP || q->test == DISPLAY_OUTPUTS) {
        rc = display_draw(&scan[1], dl, &display_session.scan_crc[1],
                          q->test == DISPLAY_OUTPUTS ? 2U - q->head : 2U);
        if (rc) return rc;
    }
    o = geforce_display_ops();
    if (!o || !o->ctx || !o->set_mode) return GF4_ESTATE;
    rc = o->set_mode(o->ctx, &route, m.id, &scan[0], dl);
    if (rc || m.fmt != GF3_INDEX8) return rc;
    o = geforce_display_ops();
    if (!o || !o->ctx || !o->palette_set) return GF4_ESTATE;
    return o->palette_set(o->ctx, q->head, 0, DISPLAY_COLORS, display_palette);
}
/* Cursor card: two image sizes, four corners, negative position, hide,
 * show, then a new mode. CPU input remains PM; B6 alone applies alpha+1.
 * MIT nouveau dispnv04/crtc.c:938-1015, cursor.c:21-28,39-62. */
#define DISPLAY_CURSOR_SIDE 64U
#define DISPLAY_CURSOR_SMALL 32U
/* Ordered camera steps from the A3 card; geometry choices are diagnostic. */
enum display_cursor_stage {
    CURSOR_SMALL, CURSOR_LARGE, CURSOR_TOP_LEFT, CURSOR_TOP_RIGHT,
    CURSOR_BOTTOM_LEFT, CURSOR_BOTTOM_RIGHT, CURSOR_NEGATIVE,
    CURSOR_HIDE, CURSOR_SHOW, CURSOR_MODE, DISPLAY_CURSOR_STEPS
};
#define DISPLAY_CURSOR_HOT_X 3
#define DISPLAY_CURSOR_HOT_Y 5
#define DISPLAY_CURSOR_NEGATIVE_POS (-8)
#define DISPLAY_FLIP_REPEATS 16U
static u32 display_cursor_pixels[GF4_CURSOR_BYTES / sizeof(u32)];
static i32 display_cursor_step(const struct display_request *q, u32 step, u32 dl)
{
    const struct gf4_test_ops *o;
    i32 rc;
    if (step <= CURSOR_LARGE || step == CURSOR_MODE) {
        u32 side = step == CURSOR_SMALL ? DISPLAY_CURSOR_SMALL : DISPLAY_CURSOR_SIDE;
        static const u8 alpha[] = {0,1,254,255};
        for (u32 y = 0; y < side; ++y)
            for (u32 x = 0; x < side; ++x) {
                bool arrow = (y < side * 3U / 4U && x <= y / 2U) ||
                    (y >= side / 2U && x >= side / 4U && x < side / 2U);
                u32 a = arrow ? alpha[y % (sizeof(alpha) / sizeof(alpha[0]))] : 0U;
                display_cursor_pixels[y * side + x] =
                    (a << 24) | (a << 16) | ((a / 2U) << 8);
            }
        struct gf4_cursor image = {.argb_pm=display_cursor_pixels,
            .bytes=side*side*sizeof(u32), .pitch=side*sizeof(u32), .w=side,.h=side,
            .hot_x=DISPLAY_CURSOR_HOT_X,.hot_y=DISPLAY_CURSOR_HOT_Y};
        o = geforce_display_ops();
        if (!o || !o->ctx || !o->cursor_image) return GF4_ESTATE;
        rc = o->cursor_image(o->ctx, q->head, &image, dl);
        if (rc) return rc;
        display_session.crc = crc32_core_final(crc32_core_update(
            CRC32_INIT, display_cursor_pixels, image.bytes));
    }
    if (step == CURSOR_HIDE || step == CURSOR_SHOW) {
        o = geforce_display_ops();
        if (!o || !o->ctx || !o->cursor_show) return GF4_ESTATE;
        return o->cursor_show(o->ctx, q->head, step == CURSOR_SHOW);
    }
    const struct gf3_surface *s = &display_session.scan[0];
    i32 x = (i32)s->width / 2, y = (i32)s->height / 2;
    if (step >= CURSOR_TOP_LEFT && step <= CURSOR_BOTTOM_RIGHT) {
        x = (step == CURSOR_TOP_RIGHT || step == CURSOR_BOTTOM_RIGHT) ? (i32)s->width - 1 : 0;
        y = step >= CURSOR_BOTTOM_LEFT ? (i32)s->height - 1 : 0;
    } else if (step == CURSOR_NEGATIVE) {
        x = DISPLAY_CURSOR_NEGATIVE_POS; y = DISPLAY_CURSOR_NEGATIVE_POS; }
    o = geforce_display_ops();
    if (!o || !o->ctx || !o->cursor_move) return GF4_ESTATE;
    rc = o->cursor_move(o->ctx, q->head, x, y);
    if (rc) return rc;
    o = geforce_display_ops();
    if (!o || !o->ctx || !o->cursor_show) return GF4_ESTATE;
    return o->cursor_show(o->ctx, q->head, true);
}
/* Each async submission is explicitly completed through vblank_wait before
 * reusing either descriptor. B7 verifies base and C2 owns both reservations.
 * MIT nouveau dispnv04/crtc.c:1074-1087,1136-1217; OS32 uses F1 counters. */
static i32 display_flip_step(const struct display_request *q, u32 step, u32 dl)
{
    for (u32 i = 0; i < DISPLAY_FLIP_REPEATS; ++i) {
        u32 target = (i + 1U) & 1U, seen;
        const struct gf4_test_ops *o = geforce_display_ops();
        if (!o || !o->ctx || !o->ctx->ch || !o->ctx->ch->f) return GF4_ESTATE;
        const struct gf1_test_ops *f = geforce_foundation_ops();
        if (!f || !f->production || !f->call || f->production != o->ctx->ch->f)
            return GF4_ESTATE;
        const struct gf1_test_request snap = {sizeof(snap), GF1T_SNAPSHOT, 0};
        struct gf1_test_reply reply;
        i32 rc = f->call(f->production, &snap, &reply);
        if (rc) return rc;
        u32 after = reply.stats.vblank[q->head];
        seen = after;
        o = geforce_display_ops();
        if (!o || !o->ctx || !o->flip) return GF4_ESTATE;
        rc = o->flip(o->ctx, q->head, &display_session.scan[target], step == 0, dl);
        if (!rc) {
            o = geforce_display_ops();
            if (!o || !o->ctx || !o->vblank_wait) rc = GF4_ESTATE;
            else {
                u32 observed;
                rc = o->vblank_wait(o->ctx, q->head, after, dl, &observed);
                if (!rc) seen = observed;
            }
        }
        /* base is B7/C2's verified, committed front, never a requested off. */
        o = geforce_display_ops();
        if (!rc && (!o || !o->ctx || !o->ctx->state ||
            o->ctx->state->head[q->head].front.off != display_session.scan[target].off))
            rc = GF4_ESTATE;
        if (!serial_gate_active()) {
            serial_puts_polled("GF4 FLIP"); display_number(" step=", step);
            display_number(" head=", q->head);
            display_number(" frame=", i); display_number(" sync=", step == 0);
            display_number(" after=", after);
            /* On failure report the last observed counter, not an invented one. */
            display_number(" counter=", seen);
            if (o && o->ctx && o->ctx->state)
                display_number(" base=", o->ctx->state->head[q->head].front.off);
            serial_puts_polled(" rc="); serial_diag_i32(rc); serial_puts_polled("\r\n");
        }
        if (rc) return rc;
        display_session.crc = display_session.scan_crc[target];
    }
    return 0;
}
static i32 display_step(const struct display_request *q)
{
    u32 step = display_session.step;
    if (q->phase) {
        u32 n = sizeof(display_power) / sizeof(display_power[0]);
        if (q->test == DISPLAY_CURSOR) n = DISPLAY_CURSOR_STEPS;
        if (q->test == DISPLAY_FLIP) n = 2U;
        if (q->test == DISPLAY_MODE) {
            struct gf4_mode ignored;
            i32 rc = display_mode_at(q->connector, 0, &ignored, &n);
            if (rc) return rc;
        }
        if (step + 1U >= n) return display_restore(true);
        ++step;
        /* Restore first, then discard only now-unused diagnostic surfaces. */
        if (q->test == DISPLAY_MODE ||
            (q->test == DISPLAY_CURSOR && step == CURSOR_MODE)) {
            i32 rc = display_restore(false);
            if (rc) return rc;
        }
    }
    const struct gf4_test_ops *o = geforce_display_ops();
    u32 dl;
    if (!o || !o->ctx) return GF4_ESTATE;
    i32 rc = display_deadline(o->ctx, &dl);
    if (rc) return rc;
    display_session.step = step;
    bool cursor_mode = q->test == DISPLAY_CURSOR && step == CURSOR_MODE;
    if (q->test == DISPLAY_MODE || !q->phase || cursor_mode) {
        display_session.mode = display_session.crc = 0;
        if (q->phase) {
            rc = display_discard(q->head, dl);
            if (rc) return rc;
            display_session.prepared = false;
            display_session.prepared_heads &= ~(1U << q->head);
        }
        rc = display_image(q, q->test == DISPLAY_MODE ? step : cursor_mode ? 1U : 0U, dl, NULL);
        if (rc) return rc;
    }
    if (q->test == DISPLAY_DPMS) {
        o = geforce_display_ops();
        if (!o || !o->ctx || !o->dpms_set) return GF4_ESTATE;
        /* nouveau dispnv04/crtc.c:175-233: ON/STANDBY/SUSPEND/OFF. */
        rc = o->dpms_set(o->ctx, q->head, display_power[step], dl);
        if (rc) return rc;
    }
    if (q->test == DISPLAY_CURSOR) rc = display_cursor_step(q, step, dl);
    if (q->test == DISPLAY_FLIP) rc = display_flip_step(q, step, dl);
    if (rc) return rc;
    display_record(q, "READY", step, display_session.mode, display_session.crc, 0);
    return 0;
}
/* The camera ramp is the integer curve specified by A3-case, independent
 * of hardware LUT packing (MIT nouveau dispnv04/crtc.c:778-797,816-834). */
#define DISPLAY_LUT_ENTRIES 256U
#define DISPLAY_LUT_MAX (DISPLAY_LUT_ENTRIES - 1U)
#define DISPLAY_LUT_SCALE 257U
#define DISPLAY_GAMMA_STEPS 4U
static u16 display_ramp[3][DISPLAY_LUT_ENTRIES];
static i32 display_format_index(u8 connector, bool indexed, u32 *index)
{
    for (u32 i = 0;; ++i) {
        struct gf4_mode m;
        u32 n;
        i32 rc = display_mode_at(connector, i, &m, &n);
        if (rc) return rc;
        if (i >= n) return GF4_EFORMAT;
        if ((m.fmt == GF3_INDEX8) == indexed) { *index = i; return 0; }
    }
}
/* The snapshot still owns its front. Only unused diagnostic banks are
 * discarded, including both heads if the second prepare failed midway. */
static i32 display_reset_images(u32 *dl)
{
    i32 rc = display_restore(false);
    if (rc) return rc;
    const struct gf4_test_ops *o = geforce_display_ops();
    if (!o || !o->ctx) return GF4_ESTATE;
    rc = display_deadline(o->ctx, dl);
    if (rc) return rc;
    for (u8 h = 0; h < GF4_HEADS; ++h) {
        if (!(display_session.prepared_heads & (1U << h))) continue;
        rc = display_discard(h, *dl);
        if (rc) return rc;
        display_session.prepared_heads &= ~(1U << h);
    }
    display_session.prepared = false;
    return 0;
}
static i32 display_gamma_step(const struct display_request *q)
{
    u32 step = display_session.step + (q->phase ? 1U : 0U), dl;
    if (step == DISPLAY_GAMMA_STEPS) return display_restore(true);
    display_session.step = step;
    i32 rc;
    if (step == DISPLAY_GAMMA_STEPS - 1U) rc = display_reset_images(&dl);
    else {
        const struct gf4_test_ops *o = geforce_display_ops();
        if (!o || !o->ctx) return GF4_ESTATE;
        rc = display_deadline(o->ctx, &dl);
    }
    if (rc) return rc;
    if (!step || step == DISPLAY_GAMMA_STEPS - 1U) {
        u32 index;
        rc = display_format_index(q->connector, step != 0, &index);
        if (rc) return rc;
        rc = display_image(q, index, dl, NULL);
        if (rc) return rc;
    }
    if (step != DISPLAY_GAMMA_STEPS - 1U) {
        for (u32 i = 0; i < DISPLAY_LUT_ENTRIES; ++i)
            for (u32 c = 0; c < 3U; ++c)
                display_ramp[c][i] = step == 1U ?
                    i * i * DISPLAY_LUT_SCALE / DISPLAY_LUT_MAX : i * DISPLAY_LUT_SCALE;
        const struct gf4_test_ops *o = geforce_display_ops();
        if (!o || !o->ctx || !o->gamma) return GF4_ESTATE;
        rc = o->gamma(o->ctx, q->head, display_ramp);
        if (rc) return rc;
        /* Sum the successfully applied RGB u16 entries, before DAC packing
         * (MIT nouveau dispnv04/crtc.c:778-797,816-834). Frame CRC stays
         * unchanged while gamma changes, so record this separately (§12). */
        u32 checksum = 0;
        for (u32 c = 0; c < sizeof(display_ramp) / sizeof(display_ramp[0]); ++c)
            for (u32 i = 0; i < DISPLAY_LUT_ENTRIES; ++i)
                checksum += display_ramp[c][i];
        if (!serial_gate_active()) {
            serial_puts_polled("GF4 GAMMA"); display_number(" step=", step);
            display_number(" head=", q->head);
            display_number(" LUT checksum=", checksum);
            serial_puts_polled("\r\n");
        }
    }
    display_record(q, "READY", step, display_session.mode, display_session.crc, 0);
    return 0;
}
static i32 display_output_route(u32 i, u8 head, struct gf4_route *route)
{
    const struct gf4_test_ops *o = geforce_display_ops();
    if (!o || !o->ctx) return GF4_ESTATE;
    if (i >= o->ctx->nroutes) return GF4_ENODEV;
    *route = o->ctx->routes[i];
    if (!(route->head_mask & (1U << head))) return GF4_ENODEV;
    route->head = head;
    return 0;
}
static i32 display_output_image(struct display_request *q,
                                const struct gf4_route *r, u32 index, u32 dl)
{
    q->connector = r->connector; q->head = r->head;
    i32 rc = display_image(q, index, dl, r);
    if (rc) return rc;
    for (u32 i = 0; i < 2U; ++i) {
        display_session.output_scan[q->head][i] = display_session.scan[i];
        display_session.output_crc[q->head][i] = display_session.scan_crc[i];
    }
    display_session.output_request[q->head] = *q;
    return 0;
}
/* DCB route order is the diagnostic order. Pair only distinct connectors
 * on independent internal ORs; set_mode owns PLL/encoder feasibility.
 * MIT nouveau dispnv04/disp.c:247-294, dfp.c:68-90,204-278. */
static i32 display_output_pair(struct gf4_route pair[GF4_HEADS], u32 n)
{
    for (u32 a = 0; a < n; ++a) {
        i32 rc = display_output_route(a, 0, &pair[0]);
        if (rc == GF4_ENODEV) continue;
        if (rc) return rc;
        for (u32 b = 0; b < n; ++b) {
            rc = display_output_route(b, 1, &pair[1]);
            if (rc == GF4_ENODEV) continue;
            if (rc) return rc;
            if (pair[0].connector == pair[1].connector) continue;
            if (pair[0].location == pair[1].location &&
                (pair[0].or_mask & pair[1].or_mask)) continue;
            return 0;
        }
    }
    return GF4_ENODEV;
}
static i32 display_outputs_step(const struct display_request *q)
{
    const struct gf4_test_ops *o = geforce_display_ops();
    if (!o || !o->ctx) return GF4_ESTATE;
    u32 n = o->ctx->nroutes, step = display_session.step + (q->phase ? 1U : 0U), dl;
    if (step >= n + 3U) return display_restore(true);
    /* A skipped pair never established the banks used by DPMS/flip.
     * Its next phase goes straight to final restore/discard/release. */
    if (step > n && !display_session.output_pair_ready) return display_restore(true);
    display_session.step = step;
    i32 rc = display_deadline(o->ctx, &dl);
    if (rc) return rc;
    struct display_request item = *q;
    if (step <= n) {
        if (q->phase) { rc = display_reset_images(&dl); if (rc) return rc; }
        struct gf4_route pair[GF4_HEADS];
        if (step < n) {
            rc = display_output_route(step, q->head, &pair[0]);
            if (rc == GF4_ENODEV)
                rc = display_output_route(step, q->head ^ 1U, &pair[0]);
            if (rc) return rc;
            rc = display_output_image(&item, &pair[0], 0, dl);
            if (rc) return rc;
            o = geforce_display_ops();
            if (!o || !o->ctx || !o->ctx->state) return GF4_ESTATE;
            u8 other = item.head ^ 1U;
            if (o->ctx->state->head[other].enabled) {
                if (!o->dpms_set) return GF4_ESTATE;
                rc = o->dpms_set(o->ctx, other, GF4_OFF, dl);
            }
            if (!rc) display_record(&item, "READY", step,
                                   display_session.mode, display_session.crc, 0);
            return rc;
        }
        rc = display_output_pair(pair, n);
        if (rc) return rc;
        for (u8 h = 0; h < GF4_HEADS; ++h) {
            u32 index = 0;
            if (h == 1U) {
                struct gf4_mode ignored;
                u32 count;
                rc = display_mode_at(pair[h].connector, 1, &ignored, &count);
                if (rc) return rc;
                if (count > 1U) index = 1;
            }
            rc = display_output_image(&item, &pair[h], index, dl);
            if (rc) return rc;
        }
        o = geforce_display_ops();
        if (!o || !o->ctx || !o->ctx->state) return GF4_ESTATE;
        for (u8 h = 0; h < GF4_HEADS; ++h) {
            const struct gf4_head_image *head = &o->ctx->state->head[h];
            if (!head->enabled || head->front.off != display_session.output_scan[h][0].off)
                return GF4_ESTATE;
        }
        display_session.output_pair_ready = true;
        for (u8 h = 0; h < GF4_HEADS; ++h)
            display_record(&display_session.output_request[h], "READY", step,
                           o->ctx->state->head[h].mode.id,
                           display_session.output_crc[h][0], 0);
        return 0;
    }
    /* Keep A visible while B powers off/on and flips its distinct card.
     * DPMS and flip register work is delegated (crtc.c:175-233,1136-1217). */
    item = display_session.output_request[1];
    o = geforce_display_ops();
    if (!o || !o->ctx || !o->ctx->state || !o->dpms_set) return GF4_ESTATE;
    const struct gf4_head_image *other = &o->ctx->state->head[0];
    u32 other_mode = other->mode.id, other_front = other->front.off;
    bool other_enabled = other->enabled;
    enum gf4_dpms other_dpms = other->dpms;
    rc = o->dpms_set(o->ctx, 1, step == n + 1U ? GF4_OFF : GF4_ON, dl);
    if (rc) return rc;
    if (step == n + 2U) {
        o = geforce_display_ops();
        if (!o || !o->ctx || !o->flip) return GF4_ESTATE;
        rc = o->flip(o->ctx, 1, &display_session.output_scan[1][1], true, dl);
        if (rc) return rc;
    }
    o = geforce_display_ops();
    if (!o || !o->ctx || !o->ctx->state) return GF4_ESTATE;
    const struct gf4_head_image *a = &o->ctx->state->head[0];
    const struct gf4_head_image *b = &o->ctx->state->head[1];
    if (a->enabled != other_enabled || a->mode.id != other_mode ||
        a->front.off != other_front || a->dpms != other_dpms ||
        (step == n + 2U && b->front.off != display_session.output_scan[1][1].off))
        return GF4_ESTATE;
    display_record(&item, "READY", step, b->mode.id,
                   display_session.output_crc[1][step == n + 2U], 0);
    return 0;
}
/* Final PM decision: no snapshot crosses leave. Call the existing kernel
 * backend table, never retain module callbacks/ctx across enter or leave.
 * backend_geforce.c:198-209 owns routing and complete service stop. */
static i32 display_lifecycle(const struct display_request *q)
{
    u32 step = q->phase ? display_session.step + 1U : 0U;
    if (!q->phase) {
        const struct gf4_test_ops *o = geforce_display_ops();
        if (!o || !o->ctx || !o->ctx->ready || o->ctx->saved_closed) return GF4_ESTATE;
        display_session.request = *q;
        display_session.lifecycle = display_session.running = true;
        display_session.mode = display_session.crc = 0;
    }
    display_session.step = step;
    i32 rc = 0;
    if (&gfx_backend_geforce == NULL) rc = GF4_ENODEV;
    else if (step == 1U) {
        if (!gfx_backend_geforce.leave) rc = GF4_EUNSUPPORTED;
        else gfx_backend_geforce.leave();
    } else {
        if (!gfx_backend_geforce.enter) rc = GF4_EUNSUPPORTED;
        else gfx_backend_geforce.enter();
    }
    const struct gf4_test_ops *o = geforce_display_ops();
    if (!rc && step == 1U) {
        if (o && o->ctx && o->ctx->ready) rc = GF4_ESTATE;
    } else if (!rc) {
        /* Existing enter selects the default head A, irrespective of the
         * wire's head selector. A valid committed mode/front proves restart. */
        if (!o || !o->ctx || !o->ctx->ready || !o->ctx->state) rc = GF4_ESTATE;
        else {
            const struct gf4_head_image *h = &o->ctx->state->head[0];
            if (!h->enabled || !h->mode.id || !h->mode.t.hd || !h->mode.t.vd ||
                !h->front.reservation_id || h->dpms != GF4_ON) rc = GF4_ESTATE;
            else display_session.mode = h->mode.id;
        }
    }
    if (rc) {
        display_session.running = display_session.lifecycle = false;
        display_record(q, rc == GF4_ENODEV || rc == GF4_EUNSUPPORTED ? "SKIP" : "ERROR",
                       step, display_session.mode, 0, rc);
    } else {
        struct display_request item = *q; item.head = 0;
        display_record(&item, "READY", step, display_session.mode, 0, 0);
        if (step == 2U) display_session.lifecycle = display_session.running = false;
    }
    return rc;
}

/* The shell remains runnable while this owner holds the diagnostic display.
 * App teardown funnels normal exit, CTRL+STOP and fault through owner_exit. */
static int display_cui_owner;
static void display_failure(const char *stage, i32 rc)
{
    if (serial_gate_active()) return;
    serial_puts_polled("GF4 ERROR stage="); serial_puts_polled(stage);
    serial_puts_polled(" rc="); serial_diag_i32(rc); serial_puts_polled("\r\n");
}
static i32 display_cui(bool enter)
{
    i32 rc = 0;
    const struct gf4_test_ops *o = geforce_display_ops();
    if (enter) {
        if (display_cui_owner || display_session.valid || (o && o->ctx && o->ctx->ready)) rc = GF4_EBUSY;
        else if (!gfx_backend_geforce.enter || !gfx_backend_geforce.leave) rc = GF4_ENODEV;
        else {
            display_cui_owner = res_owner_get();
            gfx_backend_geforce.enter();
            o = geforce_display_ops();
            if (!o || !o->ctx || !o->ctx->ready) {
                gfx_backend_geforce.leave();
                display_cui_owner = 0;
                rc = GF4_ESTATE;
            }
        }
    } else if (!display_cui_owner || display_cui_owner != res_owner_get()) rc = GF4_ESTATE;
    else {
        /* Complete a camera snapshot before the backend tears down its loans. */
        rc = display_restore(true);
        if (!rc) {
            gfx_backend_geforce.leave();
            o = geforce_display_ops();
            if (o && o->ctx && o->ctx->ready) rc = GF4_ESTATE;
            else {
                display_cui_owner = 0;
                display_session = (typeof(display_session)){0};
            }
        }
    }
    if (rc) display_failure(enter ? "enter" : "leave", rc);
    return rc;
}
void gf_display_owner_exit(int id)
{
    if (!display_cui_owner || display_cui_owner != id) return;
    /* A failing restore must not prevent routing back to 98 on owner death. */
    i32 rc = display_restore(true);
    if (rc) display_failure("owner_restore", rc);
    if (gfx_backend_geforce.leave) gfx_backend_geforce.leave();
    display_cui_owner = 0;
    if (!rc) display_session = (typeof(display_session)){0};
    else display_session.running = display_session.lifecycle = false;
}

i32 gf_display_test(u32 session)
{
    if (session == GF4_CUI_ENTER || session == GF4_CUI_LEAVE)
        return display_cui(session == GF4_CUI_ENTER);
    struct display_request q;
    i32 rc = display_decode(session, &q);
    if (rc) return rc;
    rc = display_admit(&q);
    if (rc) { display_failure("display_admit", rc); return rc; }
    if (q.test == DISPLAY_RESTORE) return display_restore(true);
    if (q.test == DISPLAY_ENUM) return q.phase ? GF4_EINVAL : display_enumerate(&q);
    if (q.test == DISPLAY_LIFECYCLE) return display_lifecycle(&q);
    if (!q.phase) {
        rc = display_begin(&q);
        if (rc) { display_failure("display_begin", rc); return rc; }
    }
    rc = q.test == DISPLAY_GAMMA ? display_gamma_step(&q) :
         q.test == DISPLAY_OUTPUTS ? display_outputs_step(&q) : display_step(&q);
    if (rc) {
        display_session.running = false;
        /* A connector's union table may be unusable on this route/head. */
        display_record(&q, rc == GF4_ENODEV || rc == GF4_EUNSUPPORTED ||
                       rc == GF4_EPLL || rc == GF4_ERANGE || rc == GF4_EFORMAT ||
                       (q.test == DISPLAY_OUTPUTS && (rc == GF4_EBUSY || rc == GF4_ENOSPC)) ? "SKIP" : "ERROR",
                       display_session.step, display_session.mode, display_session.crc, rc);
        if ((q.test == DISPLAY_MODE && (rc == GF4_EPLL || rc == GF4_ERANGE)) ||
            (q.test == DISPLAY_OUTPUTS && (rc == GF4_ENODEV || rc == GF4_EUNSUPPORTED ||
             rc == GF4_EPLL || rc == GF4_ERANGE || rc == GF4_EBUSY || rc == GF4_ENOSPC))) {
            /* The union mode table can reject just this head's candidate.
             * Restore before allowing phase1 to advance past the SKIP. */
            i32 restore_rc = display_restore(false);
            if (restore_rc) {
                display_record(&q, "ERROR", display_session.step,
                               display_session.mode, display_session.crc, restore_rc);
                return restore_rc;
            }
        }
    }
    return rc;
}
