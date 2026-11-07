/* Source notices retained from trident_mode.hwb:3–48.
 * Copyright 1991-1999 by The XFree86 Project, Inc.
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * vgaHW.c license: xorg-server-21.1.13 COPYING:54-76
 * Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.
 * Copyright (C) Colin Harrison 2005-2008
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall not
 * be used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from the XFree86 Project.
 * Copyright 1992-2003 by Alan Hourihane, North Wales, UK.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Alan Hourihane not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Alan Hourihane makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * ALAN HOURIHANE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ALAN HOURIHANE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */
/* SPR: MIT trident_driver-1.4.0.c:2919-2975 (9682 mask 0F).
 * Manufacturer sync source: NEC LCD172VXM/192VXM manual p.20; the 9682
 * register manual is unavailable (H:50-52). No waits or invented timeout.
 */
#include "trident.h"

const struct tri_format_cap tri_format_caps[TRI_FORMAT_COUNT] =
    TRI_FORMAT_CAPS_INIT;

/* OS32 replaces the X server FB manager with caller-owned records. Units
 * below are bytes, except VIDEO's unit, which is selected-cpp pixels.
 * D:3637-3661: full-row 2D area followed by the remaining linear area.
 * C:1156-1195: full-row cursor reservation and its address ceiling.
 * R:419-420: PATLOC addresses 64-byte units; V:568-605: linear alignment.
 */
struct tri_vram_constraint {
    u32 unit, alignment, minimum;
    bool rows, area2d;
};
static const struct tri_vram_constraint tri_vram_constraints[] = {
    [TRI_VRAM_2D] = {1, 1, 1, true, true},
    [TRI_VRAM_PATTERN] = {TRI_PATTERN_ALIGN, TRI_PATTERN_ALIGN,
                          TRI_PATTERN_BYTES, false, true},
    [TRI_VRAM_CURSOR] = {1, TRI_CURSOR_ALIGN,
                         TRI_CURSOR_RESERVE_BYTES, true, true},
    [TRI_VRAM_VIDEO] = {TRI_VIDEO_ALIGN_PIXELS, TRI_VIDEO_ALIGN_PIXELS,
                        1, false, false}
};

static u32 tri_usable_bytes(const struct trident_device *d)
{
    u32 bytes = d->declared_bytes;
    if (bytes > d->res->mapped_bytes) bytes = d->res->mapped_bytes;
    if (bytes > d->res->decode_bytes) bytes = d->res->decode_bytes;
    if (d->settings.vram_bytes && bytes > d->settings.vram_bytes)
        bytes = d->settings.vram_bytes;
    return bytes;
}

static const struct tri_format_cap *tri_mode_format(const struct tri_mode *m)
{
    for (u32 i = 0; i < TRI_FORMAT_COUNT; ++i)
        if (m->depth == tri_format_caps[i].depth &&
            m->bpp == tri_format_caps[i].bpp)
            return &tri_format_caps[i];
    return 0;
}

/* Division-based rounding also handles packed24's non-power-of-two pitch. */
static int tri_round_up(u32 value, u32 unit, u32 *rounded)
{
    u32 extra = value % unit;
    if (extra) {
        extra = unit - extra;
        if (value > ~0u - extra) return TRI_E_CAP_TOO_SMALL;
        value += extra;
    }
    *rounded = value;
    return TRI_OK;
}

static int tri_common_alignment(u32 a, u32 b, u32 *alignment)
{
    u32 x = a, y = b;
    while (y) {
        u32 remainder = x % y;
        x = y;
        y = remainder;
    }
    a /= x;
    if (a > ~0u / b) return TRI_E_CAP_TOO_SMALL;
    *alignment = a * b;
    return TRI_OK;
}

u32 tri_vram_display_max_bytes(const struct trident_device *d)
{
    if (!d || !d->res) return 0;
    u32 usable = tri_usable_bytes(d), maximum = 0;
    for (u32 i = 0; i < TRI_MODE_COUNT; ++i) {
        const struct tri_mode *candidate = &tri_modes[i];
        const struct tri_format_cap *cap = tri_mode_format(candidate);
        /* Same formats as F10's hal_format; RGB555/packed24 have no HAL ID. */
        if (!cap || (cap->format != TRI_FMT_INDEX8 &&
                     cap->format != TRI_FMT_RGB565 &&
                     cap->format != TRI_FMT_XRGB8888)) continue;
        struct tri_mode m;
        /* F3's prepare/validate path only reads d, despite its mutable API.
         * Use it like F10 so NoAccel pitch and candidate rejection agree. */
        if (tri_mode_prepare((struct trident_device *)d, candidate, &m))
            continue;
        if (!m.display_width || !m.vd ||
            m.display_width > U32_MAX / cap->cpp) continue;
        u32 pitch = (u32)m.display_width * cap->cpp;
        if (m.vd > U32_MAX / pitch || m.vd > usable / pitch) continue;
        u32 bytes = pitch * m.vd;
        u32 page_bytes;
        if (tri_round_up(bytes, TRI_PAGE_BYTES, &page_bytes) || page_bytes > usable) continue;
        if (maximum < bytes) maximum = bytes;
    }
    /* D:3637-3661's fixed virtual display, page sized for F6's aliases. */
    u32 rounded;
    if (tri_round_up(maximum, TRI_PAGE_BYTES, &rounded)) return 0;
    return rounded;
}

int tri_surface_validate(struct trident_device *d, const struct tri_surface *s)
{
    if (!d || !d->res || !s || s->format >= TRI_FORMAT_COUNT)
        return TRI_E_INVAL;
    const struct tri_format_cap *cap = &tri_format_caps[s->format];
    if (s->cpp != cap->cpp || !s->width || !s->height ||
        !s->pitch || s->pitch % s->cpp || s->off % s->cpp ||
        s->pitch < (u32)s->width * s->cpp)
        return TRI_E_SURFACE;
    /* Use division/subtraction so malformed sizes cannot wrap. This is
     * independent of the current mode: F3 validates a candidate before set.
     * GE row placement and the y ceiling are enforced by the 2D allocator. */
    u32 usable = tri_usable_bytes(d);
    if (s->height > s->bytes / s->pitch ||
        s->off > usable || s->bytes > usable - s->off)
        return TRI_E_CAP_TOO_SMALL;
    return TRI_OK;
}

int tri_vram_init(struct trident_device *d, const struct tri_mode *m)
{
    if (!d || !d->res || !m) return TRI_E_INVAL;
    if (d->vram.blocks) return TRI_E_BUSY;
    const struct tri_format_cap *cap = tri_mode_format(m);
    if (!cap || !m->hd || !m->vd || m->display_width < m->hd)
        return TRI_E_INVAL;
    u32 pitch = (u32)m->display_width * cap->cpp;
    u32 usable = tri_usable_bytes(d);
    if (m->vd > usable / pitch) return TRI_E_CAP_TOO_SMALL;
    u32 first_off = d->vram.first_off ? d->vram.first_off : tri_vram_display_max_bytes(d);
    if (!first_off || first_off > usable || m->vd > first_off / pitch) return TRI_E_CAP_TOO_SMALL;
    u32 rows = usable / pitch;
    if (rows > TRI_GE_MAX_Y) rows = TRI_GE_MAX_Y;
    d->vram.usable_bytes = usable;
    d->vram.pitch = pitch;
    d->vram.first_off = first_off;
    d->vram.area2d_end = rows * pitch;
    return TRI_OK;
}

static struct tri_vram_block **tri_block_link(struct trident_device *d,
                                             struct tri_vram_block *block)
{
    struct tri_vram_block **link = &d->vram.blocks;
    while (*link && *link != block) link = &(*link)->next;
    return link;
}

static int tri_allocation_shape(struct trident_device *d,
                                enum tri_vram_kind kind, u32 bytes,
                                u32 requested_align, u32 *size, u32 *align)
{
    if (kind < TRI_VRAM_2D || kind > TRI_VRAM_VIDEO || !bytes)
        return TRI_E_INVAL;
    if (!d->vram.pitch) return TRI_E_STATE;
    const struct tri_vram_constraint *c = &tri_vram_constraints[kind];
    u32 unit = c->unit, alignment = c->alignment;
    if (kind == TRI_VRAM_VIDEO) {
        /* F3 commits the selected mode before F9 initializes consumers. */
        const struct tri_format_cap *cap = tri_mode_format(&d->mode.selected);
        if (!cap) return TRI_E_STATE;
        unit *= cap->cpp;
        alignment *= cap->cpp;
    }
    if (c->rows) unit = d->vram.pitch;
    if (bytes < c->minimum) bytes = c->minimum;
    int rc = tri_round_up(bytes, unit, size);
    if (rc) return rc;
    if (c->rows) {
        rc = tri_common_alignment(alignment, d->vram.pitch, &alignment);
        if (rc) return rc;
    }
    /* Zero means no extra caller constraint; hardware alignment still holds. */
    if (requested_align) {
        rc = tri_common_alignment(alignment, requested_align, &alignment);
        if (rc) return rc;
    }
    *align = alignment;
    return TRI_OK;
}

/* Sorted first-fit over one FB-manager region. No record changes on failure. */
static int tri_find_space(struct trident_device *d, enum tri_vram_kind kind,
                          u32 start, u32 end, u32 bytes, u32 align, u32 *off)
{
    u32 candidate;
    int rc = tri_round_up(start, align, &candidate);
    if (rc) return rc;
    struct tri_vram_block *b = d->vram.blocks;
    for (;;) {
        if (candidate > end || bytes > end - candidate ||
            (kind == TRI_VRAM_CURSOR && candidate >= TRI_CURSOR_OFFSET_LIMIT))
            return TRI_E_CAP_TOO_SMALL;
        while (b && b->off + b->bytes <= candidate) b = b->next;
        if (!b || (candidate <= b->off && bytes <= b->off - candidate)) {
            *off = candidate;
            return TRI_OK;
        }
        rc = tri_round_up(b->off + b->bytes, align, &candidate);
        if (rc) return rc;
        b = b->next;
    }
}

int tri_vram_alloc(struct trident_device *d, struct tri_vram_block *block,
                   enum tri_vram_kind kind, u32 bytes, u32 align_bytes)
{
    u32 size, align, off;
    if (!d || !block) return TRI_E_INVAL;
    if (*tri_block_link(d, block) || block->allocated) return TRI_E_BUSY;
    int rc = tri_allocation_shape(d, kind, bytes, align_bytes, &size, &align);
    if (rc) return rc;
    if (kind == TRI_VRAM_VIDEO) {
        /* V:588-602: use linear space first, then free offscreen rows.
         * There are no unlocked cache records to purge in OS32. */
        u32 start = d->vram.area2d_end;
        if (start < d->vram.first_off) start = d->vram.first_off;
        rc = tri_find_space(d, kind, start, d->vram.usable_bytes,
                            size, align, &off);
        if (rc == TRI_E_CAP_TOO_SMALL)
            rc = tri_find_space(d, kind, d->vram.first_off,
                                d->vram.area2d_end, size, align, &off);
    } else {
        rc = tri_find_space(d, kind, d->vram.first_off,
                            d->vram.area2d_end, size, align, &off);
    }
    /* Optional consumers treat this rc as absent memory, like NULL from
     * xf86AllocateOffscreenArea/Linear. Never change lifecycle error state. */
    if (rc) return rc;
    struct tri_vram_block **link = &d->vram.blocks;
    while (*link && (*link)->off < off) link = &(*link)->next;
    block->next = *link;
    block->off = off;
    block->bytes = size;
    block->kind = kind;
    block->allocated = true;
    *link = block;
    return TRI_OK;
}

int tri_vram_free(struct trident_device *d, struct tri_vram_block *block)
{
    if (!d || !block) return TRI_E_INVAL;
    struct tri_vram_block **link = tri_block_link(d, block);
    if (!*link) return block->allocated ? TRI_E_STATE : TRI_OK;
    *link = block->next;
    block->next = 0;
    block->off = block->bytes = 0;
    block->allocated = false;
    return TRI_OK;
}

int tri_vram_resize(struct trident_device *d, struct tri_vram_block *block,
                    enum tri_vram_kind kind, u32 bytes, u32 align_bytes)
{
    u32 size, align;
    if (!d || !block) return TRI_E_INVAL;
    int rc = tri_allocation_shape(d, kind, bytes, align_bytes, &size, &align);
    if (rc) return rc;
    struct tri_vram_block **link = tri_block_link(d, block);
    if (!*link) {
        if (block->allocated) return TRI_E_STATE;
        return tri_vram_alloc(d, block, kind, bytes, align_bytes);
    }
    if (block->kind == kind && !(block->off % align)) {
        /* V:575-585: keep sufficient storage; otherwise try growing in place
         * before releasing and requesting a new allocation. */
        if (block->bytes >= size) return TRI_OK;
        u32 end = tri_vram_constraints[kind].area2d ?
            d->vram.area2d_end : d->vram.usable_bytes;
        if (block->next && end > block->next->off) end = block->next->off;
        if (block->off <= end && size <= end - block->off) {
            block->bytes = size;
            return TRI_OK;
        }
    }
    rc = tri_vram_free(d, block);
    if (rc) return rc;
    return tri_vram_alloc(d, block, kind, bytes, align_bytes);
}

int trident_capacity_plan(struct trident_device *d)
{
    u8 spr, ext;
    u32 kib;
    int rc;
    if (!d || !d->res || !d->io) return TRI_E_INVAL;
    if (!d->armed || !d->pinned) return TRI_E_STATE;
    /* Use the reference's current VGA IOBase; H:120 hardcodes color instead. */
    u16 base = d->cur_vga_base;
    rc = trident_indexed_read(d, base, 0x1F, &spr);
    if (rc) return rc;
    bool unknown = false;
    switch (spr & 0x0F) {
    case 0x01: kib = 512; break;
    case 0x02: kib = 6144; break;
    case 0x03: kib = 1024; break;
    case 0x04: kib = 8192; break;
    case 0x06: kib = 10240; break;
    case 0x07: kib = 2048; break;
    case 0x08: kib = 12288; break;
    case 0x0A: kib = 14336; break;
    case 0x0C: kib = 16384; break;
    case 0x0E:
        rc = trident_indexed_read(d, base, 0xC1, &ext);
        if (rc) return rc;
        switch (ext & 0x11) {
        case 0x00: kib = 20480; break;
        case 0x01: kib = 24576; break;
        case 0x10: kib = 28672; break;
        default:   kib = 32768; break; /* masked 11 */
        }
        break;
    case 0x0F: kib = 4096; break;
    default: kib = 1024; unknown = true; break;
    }
    d->spr = spr;
    d->declared_bytes = kib * 1024u;
    d->declared_unknown = unknown;
    return TRI_OK;
}
