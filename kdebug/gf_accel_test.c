/*
 * Copyright 2003 NVIDIA, Corporation
 * Copyright 2008 Maarten Maathuis
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/* Layer 3 foreground serial acceptance. GF_FEAT3_DESIGN sections 10/13,
 * G1 oracle contract and C2-2 window lifetime (2026-11-04).
 * T1 calls gf_accel_test(case_id): 0 init, 1 fill, 2 copy, 3 mono,
 * 4 ifc, 5 upload, 6 download, 7 scale, 8 present-m2mf, 9 all.
 * Every module call reacquires its dispatch. Only the window's saved unmap
 * pointer survives a call, as expressly permitted by C2-2. No GUI entry.
 */
#include "recover.h"
#include "../hwdev/gf/accel.h"
#include "../hwdev/gf/gf1_test.h"
#include "../hwdev/gf/gf2_test.h"
#include "../drivers/video/geforce/geforce_module.h"
#include "../drivers/pci/pci.h"
#include "../gfx/gfx.h"
#include "../lib/serial_diag.h"
#include "../lib/crc32_core.inc"
#ifdef HWDEV_MODULE
#include "../hwdev/generated/hwdev_import_macros.h"
#endif

/* Test geometry and RAM budget, not hardware constants. A 64x48 32bpp
 * image and an 8KiB tail cover source/destination and different-pitch copy.
 * The tail fits 32 rows at 32*4+8 byte pitch, rounded to whole pages.
 * The 4097-byte transfer cases cross the existing 4KiB staging boundary. */
#define TEST_SIDE 64U
#define TEST_HEIGHT 48U
#define TEST_HALF (TEST_SIDE * TEST_HEIGHT * 4U)
#define TEST_TAIL 8192U
#define TEST_BYTES (TEST_HALF + TEST_TAIL)
#define TEST_BYTE_WIDTH 256U
#define TEST_BYTE_HEIGHT (TEST_BYTES / TEST_BYTE_WIDTH)
#define TEST_GUARD 16U
#define TEST_GUARD_VALUE 0xa7U
#define TEST_COPY_ALU 3U
#define TEST_FULL_MASK 0xffffffffU
#define TEST_FRAC_BITS 20U
#define TEST_FRAC_ONE (1U << TEST_FRAC_BITS)

enum gf3_case { TEST_INIT, TEST_FILL, TEST_COPY, TEST_MONO, TEST_IFC,
    TEST_UPLOAD, TEST_DOWNLOAD, TEST_SCALE, TEST_PRESENT, TEST_ALL };
struct gf3_test_view {
    struct gf_vram_view view;
    __typeof__(&gf_vram_view_unmap) unmap;
};
static struct gf3_test_view test_view;
/* No large automatic arrays, allocation, or host fixture dependencies. */
static u8 expected[TEST_BYTES], before[TEST_HALF];
static u8 transfer[TEST_BYTES + TEST_GUARD];
static u32 compared, differences, first_bad, max_delta, crc_expected, crc_actual;
/* XRGB's unused byte is excluded only within these drawn image extents. */
static u32 xrgb_first, xrgb_second;

static void number(const char *key, u32 value)
{
    serial_puts_polled(key); serial_diag_u32(value);
}
static void hex(const char *key, u32 value)
{
    serial_puts_polled(key); serial_diag_hex(value, 8);
}
static i32 gf3_test_view_close(void *owner, struct gf_vram_view *view)
{
    struct gf3_test_view *v = owner;
    if (!v || view != &v->view || !v->unmap) return GF2_EMAP;
    int rc = v->unmap(view);
    if (!rc) v->unmap = 0;
    return rc;
}
static const struct gf3_test_ops *dispatch(void)
{
    const struct gf3_test_ops *o = geforce_accel_ops();
    if (!o || !o->ctx || !o->ctx->ch || !o->test_surface ||
        !o->ctx->ready || o->ctx->epoch != o->ctx->ch->epoch ||
        o->ctx->ch->state != GF2_RUN || o->ctx->ch->fault.latched)
        return 0;
    return o;
}
static int deadline(u32 *dl)
{
    const struct gf3_test_ops *o = dispatch();
    if (!o || !o->ctx->ch->f) return GF3_ESTATE;
    struct gf_foundation *f = o->ctx->ch->f;
    u32 now;
    if (!f->io.timing.clock_us || f->io.timing.clock_us(f->io.opaque, &now))
        return GF2_EIO;
    *dl = now + GF3_TIMEOUT_US;
    return 0;
}
static int snapshot(struct gf2_test_reply *reply)
{
    const struct gf1_test_ops *o = geforce_foundation_ops();
    if (!o || !o->production || !o->command || !o->command->call)
        return GF3_ESTATE;
    struct gf_foundation *f = o->production;
    u32 now;
    if (!f->io.timing.clock_us || f->io.timing.clock_us(f->io.opaque, &now))
        return GF2_EIO;
    const struct gf2_test_request q = {sizeof(q), GF2T_SNAPSHOT, now + GF3_TIMEOUT_US};
    return o->command->call(o->production, &q, reply);
}
static u32 cpp(enum gf3_fmt fmt)
{
    return fmt == GF3_INDEX8 || fmt == GF3_Y8 ? 1U :
           fmt == GF3_RGB565 || fmt == GF3_YUY2 || fmt == GF3_UYVY ? 2U : 4U;
}
/* Derive a value descriptor anew for each call, preserving the current
 * reservation ID/generation. C1 owns placement, reserve and release. */
static int surface(const struct gf3_test_ops *o, struct gf3_surface *s,
                   enum gf3_fmt fmt, u32 off, u32 w, u32 h, u32 pitch)
{
    if (o->test_surface->bytes < TEST_BYTES) return GF2_ENOSPC;
    if (off > TEST_BYTES || !w || !h || pitch < w * cpp(fmt) ||
        h > (TEST_BYTES - off) / pitch) return GF3_ERANGE;
    *s = *o->test_surface;
    s->off += off; s->bytes = h * pitch;
    s->width = w; s->height = h; s->pitch = pitch; s->fmt = fmt;
    return 0;
}
static int bytes_transfer(bool download)
{
    u32 dl;
    int rc = deadline(&dl);
    if (rc) return rc;
    const struct gf3_test_ops *o = dispatch();
    struct gf3_surface s;
    if (!o) return GF3_ESTATE;
    rc = surface(o, &s, GF3_INDEX8, 0, TEST_BYTE_WIDTH,
                 TEST_BYTE_HEIGHT, TEST_BYTE_WIDTH);
    if (rc) return rc;
    struct gf3_rect r = {0, 0, s.width, s.height};
    struct gf3_ram ram = {download ? transfer : expected, TEST_BYTES, s.pitch};
    if (download) return o->download ? o->download(o->ctx, &s, r, &ram, dl) : GF3_ESTATE;
    return o->upload ? o->upload(o->ctx, &s, r, &ram, true, dl) : GF3_ESTATE;
}
static void seed(void)
{
    for (u32 i = 0; i < TEST_BYTES; i++)
        expected[i] = (u8)((i * 37U) ^ (i >> 5) ^ (i >> 11));
    for (u32 i = TEST_BYTES; i < sizeof(transfer); i++)
        transfer[i] = TEST_GUARD_VALUE;
    xrgb_first = xrgb_second = 0;
}
static u32 get_pixel(const u8 *p, u32 n)
{
    u32 value = 0;
    for (u32 b = 0; b < n; b++) value |= (u32)p[b] << (8U * b);
    return value;
}
static void put_pixel(u8 *p, u32 n, u32 value)
{
    for (u32 b = 0; b < n; b++) p[b] = (u8)(value >> (8U * b));
}
static bool ignored(u32 i)
{
    return (i & 3U) == 3U &&
        (i < xrgb_first || (i >= TEST_HALF && i - TEST_HALF < xrgb_second));
}
static int compare(bool download, bool ideal)
{
    int rc;
    if (download) {
        rc = bytes_transfer(true);
        if (rc) return rc;
        for (u32 i = TEST_BYTES; i < sizeof(transfer); i++)
            if (transfer[i] != TEST_GUARD_VALUE) return GF2_EIO;
    } else {
        if (test_view.view.mapped) return GF2_EMAP;
        const struct gf3_test_ops *o = dispatch();
        if (!o || !o->map || !o->unmap) return GF3_ESTATE;
        test_view.unmap = o->unmap;
        test_view.view.close = gf3_test_view_close;
        test_view.view.close_owner = &test_view;
        volatile u8 *va = 0;
        rc = o->map(&test_view.view, o->ctx->ch, o->test_surface->off,
                    TEST_BYTES, &va);
        if (rc) { test_view.unmap = 0; return rc; }
        /* All public operations completed their reference before mapping.
         * This window is read-only: no posted CPU writes need draining. */
        for (u32 i = 0; i < TEST_BYTES; i++) transfer[i] = va[i];
        va = 0;
        rc = gf3_test_view_close(&test_view, &test_view.view);
        if (rc) return rc; /* static record retains ownership for retry */
    }
    u32 bad = 0, first = TEST_FULL_MASK, delta = 0, count = 0;
    for (u32 i = 0; i < TEST_BYTES; i++) {
        if (ignored(i)) { transfer[i] = expected[i]; continue; }
        count++;
        if (transfer[i] == expected[i]) continue;
        if (!bad) first = i;
        bad++;
        u32 d = transfer[i] > expected[i] ? transfer[i] - expected[i] : expected[i] - transfer[i];
        if (d > delta) delta = d;
    }
    compared += count; differences += bad;
    if (first_bad == TEST_FULL_MASK && bad) first_bad = first;
    if (delta > max_delta) max_delta = delta;
    crc_expected = crc32_core_final(crc32_core_update(CRC32_INIT, expected, TEST_BYTES));
    crc_actual = crc32_core_final(crc32_core_update(CRC32_INIT, transfer, TEST_BYTES));
    serial_puts_polled("GF3 compare");
    number(" compare_bytes=", count); number(" differences=", bad);
    hex(" first_bad=", first); number(" max_delta=", delta);
    if (bad) {
        number(" expected_byte=", expected[first]);
        number(" actual_byte=", transfer[first]);
    }
    hex(" crc_expected=", crc_expected); hex(" crc_actual=", crc_actual);
    serial_puts_polled(ideal ? " comparison=ideal-observation\r\n" : " comparison=exact\r\n");
    /* No guessed tolerance for bilinear/YUV. Differences require explicit
     * hardware acceptance; report a failure while preserving the evidence. */
    return bad ? GF2_EIO : 0;
}

/* Independent NVROP truth tables, xf86-video-nouveau nv_rop.h:303-318.
 * Bits are indexed by (P<<2)|(S<<1)|D. See license notice below. */
static const u8 rops[16][2] = {
    {0x00,0x00},{0x88,0x8a},{0x44,0x45},{0xcc,0xca},
    {0x22,0x2a},{0xaa,0xaa},{0x66,0x6a},{0xee,0xea},
    {0x11,0x15},{0x99,0x95},{0x55,0x55},{0xdd,0xd5},
    {0x33,0x2e},{0xbb,0xba},{0x77,0x75},{0xff,0xff}
};
static u8 rop_byte(u8 rop, u8 p, u8 s, u8 d)
{
    u8 result = 0;
    for (u32 b = 0; b < 8U; b++) {
        u32 bit = (((p >> b) & 1U) << 2) | (((s >> b) & 1U) << 1) | ((d >> b) & 1U);
        result |= ((rop >> bit) & 1U) << b;
    }
    return result;
}
static int fill_case(void)
{
    static const u32 masks[] = {0, TEST_FULL_MASK, 0x55aa55aaU};
    const struct gf3_rect r = {3, 5, 23, 17};
    for (enum gf3_fmt fmt = GF3_INDEX8; fmt <= GF3_ARGB8888; fmt++) {
        u32 n = cpp(fmt), pitch = TEST_SIDE * n;
        for (u32 alu = 0; alu < (n == 4U ? 1U : 16U); alu++)
        for (u32 m = 0; m < (n == 4U ? 1U : 3U); m++) {
            u32 a = n == 4U ? TEST_COPY_ALU : alu;
            u32 mask = n == 4U ? TEST_FULL_MASK : masks[m];
            seed();
            int rc = bytes_transfer(false);
            if (rc) return rc;
            u32 dl; rc = deadline(&dl); if (rc) return rc;
            const struct gf3_test_ops *o = dispatch();
            struct gf3_surface s;
            if (!o || !o->fill) return GF3_ESTATE;
            rc = surface(o, &s, fmt, 0, TEST_SIDE, TEST_HEIGHT, pitch);
            if (!rc) rc = o->fill(o->ctx, &s, r, 0x963ac55aU, a, mask, dl);
            if (rc) return rc;
            for (u32 y = r.y; y < r.y + r.h; y++)
            for (u32 x = r.x; x < r.x + r.w; x++)
            for (u32 b = 0; b < n; b++) {
                u32 i = y * pitch + x * n + b;
                expected[i] = rop_byte(rops[a][mask != TEST_FULL_MASK],
                    (u8)(mask >> (8U * b)), (u8)(0x963ac55aU >> (8U * b)), expected[i]);
            }
            if (fmt == GF3_XRGB8888) xrgb_first = pitch * TEST_HEIGHT;
            serial_puts_polled("GF3 sample");
            number(" alu=", a); hex(" mask=", mask); number(" fmt=", fmt);
            serial_puts_polled("\r\n");
            rc = compare(false, false); if (rc) return rc;
        }
    }
    return 0;
}
static int copy_case(void)
{
    /* Same descriptor for all overlapping copies. Save the pre-copy image
     * separately: traversal of the CPU oracle cannot feed its own writes. */
    const struct gf3_rect r = {16, 16, 23, 19};
    for (enum gf3_fmt fmt = GF3_INDEX8; fmt <= GF3_ARGB8888; fmt++)
    for (u32 direction = 0; direction < 11U; direction++) {
        u32 n = cpp(fmt), pitch = TEST_SIDE * n;
        u32 dx = 15U + direction % 3U, dy = 15U + direction / 3U;
        struct gf3_rect sr = r;
        if (direction == 9U) { sr = (struct gf3_rect){63, TEST_HEIGHT - 1U, 1, 1}; dx = 0; dy = 0; }
        if (direction == 10U) { dx = 0; dy = 0; }
        seed();
        for (u32 i = 0; i < sizeof(before); i++) before[i] = expected[i];
        int rc = bytes_transfer(false); if (rc) return rc;
        u32 dl; rc = deadline(&dl); if (rc) return rc;
        const struct gf3_test_ops *o = dispatch();
        struct gf3_surface s, d;
        if (!o || !o->copy) return GF3_ESTATE;
        rc = surface(o, &s, fmt, 0, TEST_SIDE, TEST_HEIGHT, pitch);
        if (rc) return rc;
        d = s;
        if (direction == 10U)
            rc = surface(o, &d, fmt, TEST_HALF, 32, 32, 32U * n + 8U);
        if (!rc) rc = o->copy(o->ctx, &s, &d, sr, dx, dy, TEST_COPY_ALU, TEST_FULL_MASK, dl);
        if (rc) return rc;
        u32 dst_off = direction == 10U ? TEST_HALF : 0;
        u32 dst_pitch = direction == 10U ? 32U * n + 8U : pitch;
        for (u32 y = 0; y < sr.h; y++)
        for (u32 b = 0; b < sr.w * n; b++)
            expected[dst_off + (dy + y) * dst_pitch + dx * n + b] =
                before[(sr.y + y) * pitch + sr.x * n + b];
        if (fmt == GF3_XRGB8888) {
            xrgb_first = pitch * TEST_HEIGHT;
            if (direction == 10U) xrgb_second = dst_pitch * 32U;
        }
        serial_puts_polled("GF3 sample");
        number(" direction=", direction); number(" fmt=", fmt);
        serial_puts_polled("\r\n");
        rc = compare(false, false); if (rc) return rc;
    }
    return 0;
}
static int mono_case(void)
{
    static const u32 widths[] = {1, 7, 8, 9, 31, 32, 33};
    for (u32 widx = 0; widx < sizeof(widths)/sizeof(widths[0]); widx++) {
        u32 w = widths[widx], h = w == 33U ? 104U : 9U;
        /* 33*104 packs 520 bytes (>128 dwords); pitch 9 is noncontiguous. */
        struct gf3_rect r = {3, 2, w, h}, clip = {w == 1U ? 3U : 4U, 3, w, h - 2U};
        seed();
        int rc = bytes_transfer(false); if (rc) return rc;
        for (u32 i = 0; i < h * 9U; i++) transfer[i] = (u8)(0x91U ^ i * 13U);
        struct gf3_ram ram = {transfer, (h - 1U) * 9U + (w + 7U)/8U, 9};
        u32 dl; rc = deadline(&dl); if (rc) return rc;
        const struct gf3_test_ops *o = dispatch();
        struct gf3_surface s;
        if (!o || !o->mono) return GF3_ESTATE;
        rc = surface(o, &s, GF3_INDEX8, 0, 64, 128, 64);
        if (!rc) rc = o->mono(o->ctx, &s, r, &ram, 0xe3, 0x19, clip, dl);
        if (rc) return rc;
        for (u32 y = clip.y; y < clip.y + clip.h; y++)
        for (u32 x = clip.x; x < r.x + r.w; x++) {
            u8 byte = transfer[(y - r.y) * ram.pitch + (x - r.x)/8U];
            expected[y * 64U + x] = byte & (0x80U >> ((x - r.x) % 8U)) ? 0xe3 : 0x19;
        }
        serial_puts_polled("GF3 sample"); number(" mono_width=", w);
        serial_puts_polled("\r\n");
        rc = compare(false, false); if (rc) return rc;
    }
    return 0;
}
/* RAM offsets include pitch padding and the trailing guard. Derive each
 * payload byte from the tightly packed seed, not the transfer's own data. */
static int compare_transfer(bool download, u32 rowbytes, u32 rows, u32 pitch)
{
    u32 count = 0, bad = 0, first = TEST_FULL_MASK, delta = 0;
    u8 first_expected = 0, first_actual = 0;
    for (u32 i = 0; i < sizeof(transfer); i++) {
        u32 y = i / pitch, b = i % pitch;
        bool payload = y < rows && b < rowbytes;
        if (payload && !download) continue;
        u8 want = payload ? expected[y * rowbytes + b] : TEST_GUARD_VALUE;
        u8 actual = transfer[i];
        count++;
        if (actual == want) continue;
        if (!bad) { first = i; first_expected = want; first_actual = actual; }
        bad++;
        u32 d = actual > want ? actual - want : want - actual;
        if (d > delta) delta = d;
    }
    compared += count; differences += bad;
    if (first_bad == TEST_FULL_MASK && bad) first_bad = first;
    if (delta > max_delta) max_delta = delta;
    serial_puts_polled("GF3 RAM compare");
    number(" compare_bytes=", count); number(" differences=", bad);
    hex(" first_bad=", first); number(" max_delta=", delta);
    if (bad) {
        number(" expected_byte=", first_expected);
        number(" actual_byte=", first_actual);
    }
    serial_puts_polled(" offset_space=ram\r\n");
    return bad ? GF2_EIO : 0;
}
static int transfer_case(enum gf3_case which)
{
    /* Transfer dimensions exercise byte lengths and the independent 2047
     * row boundary, without reproducing the product's splitting algorithm. */
    static const u32 widths[] = {1, 1, 1024, 4096, 4097, 63, 63};
    static const u32 heights[] = {2047, 2048, 5, 2, 2, 7, 7};
    for (u32 t = 0; t < sizeof(widths)/sizeof(widths[0]); t++) {
        enum gf3_fmt fmt = t < 5U ? GF3_INDEX8 : t == 5U ? GF3_RGB565 : GF3_ARGB8888;
        if (which == TEST_IFC && t < 4U) continue;
        /* INDEX8's explicit IFC rejection is observed separately below. */
        u32 n = cpp(fmt), w = widths[t], h = heights[t];
        u32 rowbytes = w * n, ram_pitch = rowbytes + 3U;
        u32 ram_bytes = (h - 1U) * ram_pitch + rowbytes;
        struct gf3_rect r = {0, 0, w, h};
        seed();
        int rc = bytes_transfer(false); if (rc) return rc;
        for (u32 i = 0; i < sizeof(transfer); i++) transfer[i] = TEST_GUARD_VALUE;
        if (which != TEST_DOWNLOAD)
            for (u32 y = 0; y < h; y++)
            for (u32 b = 0; b < rowbytes; b++)
                transfer[y * ram_pitch + b] = (u8)(y * 23U + b * 7U + (b >> 8));
        struct gf3_ram ram = {transfer, ram_bytes, ram_pitch};
        u32 dl; rc = deadline(&dl); if (rc) return rc;
        const struct gf3_test_ops *o = dispatch();
        struct gf3_surface s;
        if (!o) return GF3_ESTATE;
        rc = surface(o, &s, fmt, 0, w, h, rowbytes);
        if (rc) return rc;
        if (which == TEST_DOWNLOAD)
            rc = o->download ? o->download(o->ctx, &s, r, &ram, dl) : GF3_ESTATE;
        else if (which == TEST_PRESENT)
            rc = o->present_m2mf ? o->present_m2mf(o->ctx, &s, r, &ram, dl) : GF3_ESTATE;
        else if (which == TEST_IFC)
            rc = o->ifc ? o->ifc(o->ctx, &s, r, &ram, dl) : GF3_ESTATE;
        else rc = o->upload ? o->upload(o->ctx, &s, r, &ram, t < 5U, dl) : GF3_ESTATE;
        if (which == TEST_IFC && fmt == GF3_INDEX8) {
            serial_puts_polled("GF3 sample");
            number(" ifc_unsupported_fmt=", fmt); serial_puts_polled(" rc=");
            serial_diag_i32(rc); serial_puts_polled("\r\n");
            if (rc != GF3_EFORMAT && rc != GF3_EUNSUPPORTED) return rc ? rc : GF2_EIO;
            rc = compare(false, false); if (rc) return rc;
            continue;
        }
        if (rc) return rc;
        serial_puts_polled("GF3 sample");
        number(" fmt=", fmt); number(" width=", w);
        number(" rowbytes=", rowbytes); number(" rows=", h);
        number(" ram_pitch=", ram_pitch); number(" ram_bytes=", ram_bytes);
        serial_puts_polled("\r\n");
        /* Record payload/padding/guard mismatches before returning, even
         * when this is the first transfer and no VRAM comparison has run. */
        rc = compare_transfer(which == TEST_DOWNLOAD, rowbytes, h, ram_pitch);
        if (rc) return rc;
        if (which != TEST_DOWNLOAD)
            for (u32 y = 0; y < h; y++)
            for (u32 b = 0; b < rowbytes; b++)
                expected[y * rowbytes + b] = transfer[y * ram_pitch + b];
        /* Independent UC window first, then a separate download comparison:
         * a matching M2MF round trip alone never establishes correctness. */
        rc = compare(false, false); if (rc) return rc;
        rc = compare(true, false); if (rc) return rc;
    }
    return 0;
}
static i32 floor256(i32 value)
{
    return value >= 0 ? value / 256 : -((-value + 255) / 256);
}
static u32 clamp8(i32 value)
{
    return value < 0 ? 0U : value > 255 ? 255U : (u32)value;
}
/* G1's independent integer ideal, including bit-replicated 565 and
 * BT.601 limited range. Packed result is R in bits 0..7, then G/B/A. */
static u32 rgba(enum gf3_fmt fmt, u32 x, u32 y)
{
    const u8 *p = before + y * 16U * cpp(fmt) + x * cpp(fmt);
    u32 r, g, b, a = 255;
    if (fmt == GF3_RGB565) {
        u32 v = get_pixel(p, 2);
        r = (v >> 11) & 31U; g = (v >> 5) & 63U; b = v & 31U;
        r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
    } else if (fmt == GF3_Y8) r = g = b = *p;
    else if (fmt == GF3_YUY2 || fmt == GF3_UYVY) {
        const u8 *pair = before + y * 32U + (x & ~1U) * 2U;
        i32 c, d, e;
        if (fmt == GF3_YUY2) {
            c = (i32)pair[(x & 1U) * 2U] - 16;
            d = (i32)pair[1] - 128; e = (i32)pair[3] - 128;
        } else {
            c = (i32)pair[1U + (x & 1U) * 2U] - 16;
            d = (i32)pair[0] - 128; e = (i32)pair[2] - 128;
        }
        r = clamp8(floor256(298*c + 409*e + 128));
        g = clamp8(floor256(298*c - 100*d - 208*e + 128));
        b = clamp8(floor256(298*c + 516*d + 128));
    } else {
        b = p[0]; g = p[1]; r = p[2];
        if (fmt == GF3_ARGB8888) a = p[3];
    }
    return r | (g << 8) | (b << 16) | (a << 24);
}
static u32 filtered(enum gf3_fmt fmt, u32 fx, u32 fy, bool bilinear)
{
    u32 x = fx >> TEST_FRAC_BITS, y = fy >> TEST_FRAC_BITS;
    if (x > 15U) x = 15U;
    if (y > 15U) y = 15U;
    if (!bilinear) return rgba(fmt, x, y);
    u32 x1 = x < 15U ? x + 1U : x, y1 = y < 15U ? y + 1U : y;
    u32 wx = fx & (TEST_FRAC_ONE - 1U), wy = fy & (TEST_FRAC_ONE - 1U);
    u32 samples[4] = {rgba(fmt,x,y), rgba(fmt,x1,y), rgba(fmt,x,y1), rgba(fmt,x1,y1)};
    u32 value = 0;
    for (u32 b = 0; b < 4U; b++) {
        unsigned long long top = (unsigned long long)((samples[0] >> (8U*b)) & 255U) * (TEST_FRAC_ONE - wx) +
                  (unsigned long long)((samples[1] >> (8U*b)) & 255U) * wx;
        unsigned long long bottom = (unsigned long long)((samples[2] >> (8U*b)) & 255U) * (TEST_FRAC_ONE - wx) +
                     (unsigned long long)((samples[3] >> (8U*b)) & 255U) * wx;
        value |= (u32)((top * (TEST_FRAC_ONE - wy) + bottom * wy) >>
                      (2U * TEST_FRAC_BITS)) << (8U * b);
    }
    return value;
}
static void scale_seed(enum gf3_fmt fmt, bool solid)
{
    seed();
    for (u32 y = 0; y < 16U; y++)
    for (u32 x = 0; x < 16U; x++) {
        u8 *p = expected + (y * 16U + x) * cpp(fmt);
        u32 r = solid ? 96U : x * 16U, g = solid ? 160U : y * 16U, b = solid ? 32U : (x ^ y) * 16U;
        if (fmt == GF3_RGB565)
            put_pixel(p, 2, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        else if (fmt == GF3_Y8) *p = solid ? 128U : (x + y) * 8U;
        else if (fmt == GF3_YUY2 || fmt == GF3_UYVY) {
            /* Non-neutral chroma detects U/V and Y byte ordering. */
            if (fmt == GF3_YUY2) {
                p[0] = solid ? 128U : 32U + x * 8U + y * 3U;
                p[1] = (x & 1U) ? 176U : 96U;
            } else {
                p[0] = (x & 1U) ? 176U : 96U;
                p[1] = solid ? 128U : 32U + x * 8U + y * 3U;
            }
        } else put_pixel(p, 4, 0x7d000000U | (r << 16) | (g << 8) | b);
    }
    for (u32 i = 0; i < sizeof(before); i++) before[i] = expected[i];
}
static int scale_case(void)
{
    static const u32 sizes[] = {12, 24, 6, 19};
    int first_rc = 0;
    for (enum gf3_fmt fmt = GF3_RGB565; fmt <= GF3_UYVY; fmt++)
    for (u32 trial = 0; trial < 6U; trial++) {
        bool bilinear = trial >= 4U, solid = trial == 4U;
        u32 side = sizes[trial < 4U ? trial : 1U];
        enum gf3_fmt dst_fmt = fmt == GF3_RGB565 ? GF3_RGB565 :
                              fmt == GF3_ARGB8888 ? GF3_ARGB8888 : GF3_XRGB8888;
        u32 n = cpp(dst_fmt), pitch = 32U * n;
        const struct gf3_rect sr = trial == 5U ?
            (struct gf3_rect){0, 0, 16, 16} : (struct gf3_rect){2, 2, 12, 12};
        struct gf3_rect dr = {1, 2, side, side}, clip = {3, 3, side - 3U, side - 2U};
        if (trial == 5U) clip = dr; /* include the full-surface clamped edge */
        scale_seed(fmt, solid);
        int rc = bytes_transfer(false); if (rc) return rc;
        u32 dl; rc = deadline(&dl); if (rc) return rc;
        const struct gf3_test_ops *o = dispatch();
        struct gf3_surface src, dst;
        if (!o || !o->scale) return GF3_ESTATE;
        rc = surface(o, &src, fmt, 0, 16, 16, 16U * cpp(fmt));
        if (!rc) rc = surface(o, &dst, dst_fmt, TEST_HALF, 32, 32, pitch);
        if (!rc) rc = o->scale(o->ctx, &src, &dst, sr, dr, clip, bilinear, dl);
        if (rc) return rc;
        u32 du = (sr.w << TEST_FRAC_BITS)/dr.w, dv = (sr.h << TEST_FRAC_BITS)/dr.h;
        for (u32 y = clip.y; y < clip.y + clip.h; y++)
        for (u32 x = clip.x; x < clip.x + clip.w; x++) {
            u32 v = filtered(fmt, (sr.x << TEST_FRAC_BITS) + (x - dr.x)*du,
                                 (sr.y << TEST_FRAC_BITS) + (y - dr.y)*dv, bilinear);
            u32 r = v & 255U, g = (v >> 8) & 255U, b = (v >> 16) & 255U;
            u32 packed = dst_fmt == GF3_RGB565 ?
                ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) :
                (v & 0xff000000U) | (r << 16) | (g << 8) | b;
            put_pixel(expected + TEST_HALF + y*pitch + x*n, n, packed);
        }
        if (fmt == GF3_XRGB8888) xrgb_first = 16U * 16U * 4U;
        if (dst_fmt == GF3_XRGB8888) xrgb_second = 32U * pitch;
        serial_puts_polled("GF3 sample");
        number(" scale_fmt=", fmt); number(" trial=", trial);
        number(" bilinear=", bilinear); number(" solid=", solid);
        serial_puts_polled("\r\n");
        rc = compare(false, bilinear || fmt == GF3_YUY2 || fmt == GF3_UYVY);
        if (rc && !first_rc) first_rc = rc;
        /* Numerical differences are observations; continue collecting all
         * formats/filters. Mapping/cleanup errors retain ownership and stop. */
        if (test_view.view.mapped || (rc && rc != GF2_EIO)) return rc;
    }
    return first_rc;
}

static void record(const char *phase, u32 case_id, int rc,
                   const struct gf2_test_reply *reply)
{
    serial_puts_polled("GF3 phase="); serial_puts_polled(phase);
    number(" case_id=", case_id); serial_puts_polled(" rc="); serial_diag_i32(rc);
    if (reply) {
        number(" epoch=", reply->epoch); number(" seq=", reply->seq);
        hex(" reference=", reply->reference); number(" state=", reply->state);
        /* Fault details remain owned by the existing GF2 status record. */
        number(" latched=", reply->latched);
    }
    number(" compared=", compared); number(" differences=", differences);
    hex(" first_bad=", first_bad); number(" max_delta=", max_delta);
    number(" window_retained=", test_view.view.mapped);
    serial_puts_polled("\r\n");
}
/* External T1 entry; prototype is repeated here for -Wmissing-prototypes.
 * The PM owns the caller/header/build/import registration (see report). */
int gf_accel_test(u32 case_id);
int gf_accel_test(u32 case_id)
{
    compared = differences = max_delta = crc_expected = crc_actual = 0;
    first_bad = TEST_FULL_MASK;
    int rc = 0;
    struct gf2_test_reply reply = {0};
    /* Retry a retained window even with the slot closed. No borrowed ctx,
     * dispatch, or surface is needed for this owner-only cleanup. */
    if (test_view.view.mapped)
        rc = gf3_test_view_close(&test_view, &test_view.view);
    if (!rc && case_id > TEST_ALL) rc = GF3_EINVAL;
    if (!rc) rc = snapshot(&reply);
    if (!rc && (reply.state != GF2_RUN || reply.latched ||
                 (i32)(reply.reference - reply.seq) < 0)) rc = GF3_ESTATE;
    if (!rc) {
        const struct gf3_test_ops *o = dispatch();
        if (!o) rc = GF3_ESTATE;
        else {
            serial_puts_polled("GF3 info");
            hex(" bdf=", RECOVER_GF_BDF);
            u32 device = pci_cfg_read32(RECOVER_GF_BUS, RECOVER_GF_DEV,
                                        RECOVER_GF_FN, PCI_CFG_VENDOR_ID);
            hex(" device=", device);
            if (device != RECOVER_GF_DEVICE) rc = GF1_EPCI;
            number(" format=", o->test_surface->fmt); number(" pitch=", o->test_surface->pitch);
            hex(" off=", o->test_surface->off); number(" width=", o->test_surface->width);
            number(" height=", o->test_surface->height);
            number(" reservation_id=", o->test_surface->reservation_id);
            number(" generation=", o->test_surface->generation);
            serial_puts_polled("\r\n");
            if (o->test_surface->bytes < TEST_BYTES) rc = GF2_ENOSPC;
        }
    }
    record("begin", case_id, rc, &reply);
    if (!rc) {
        u32 first = case_id == TEST_ALL ? TEST_INIT : case_id;
        u32 last = case_id == TEST_ALL ? TEST_PRESENT : case_id;
        for (u32 c = first; c <= last && !rc; c++) {
            if (c == TEST_FILL) rc = fill_case();
            else if (c == TEST_COPY) rc = copy_case();
            else if (c == TEST_MONO) rc = mono_case();
            else if (c == TEST_SCALE) rc = scale_case();
            else if (c != TEST_INIT) rc = transfer_case((enum gf3_case)c);
            int observed = snapshot(&reply);
            if (!rc) rc = observed;
            if (!rc && (reply.latched || reply.state != GF2_RUN ||
                        (i32)(reply.reference - reply.seq) < 0)) rc = GF2_EIO;
            record("case", c, rc, &reply);
        }
    }
    /* No scanout writes: all GPU destinations are within C1's test node.
     * Re-present the authoritative RAM BB using the existing CPU path after
     * successful completion; do not race it with an incomplete GPU command. */
    if (!rc) gfx_present();
    record("end", case_id, rc, &reply);
    return rc;
}
