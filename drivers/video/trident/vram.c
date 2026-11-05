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
 * Probe extension: TRIDENT_S3_DESIGN §4; H:411-431 only samples four words.
 * Here every dword is tested, with live page tags to detect cross-page alias.
 * Manufacturer sync source: NEC LCD172VXM/192VXM manual p.20; the 9682
 * register manual is unavailable (H:50-52). No waits or invented timeout.
 */
#include "trident.h"
#include "lib/serial_diag.h"

#define WORD_BYTES ((u32)sizeof(u32))
#define PAGE_WORDS (TRI_PAGE_BYTES / WORD_BYTES)
#define TAG_WORDS (TRI_BAR0_DECODE_BYTES / TRI_PAGE_BYTES)
#define READ(off, ptr) trident_read32(d, TRI_SPACE_LFB, (off), (ptr))
#define WRITE(off, value) trident_write32(d, TRI_SPACE_LFB, (off), (value))

int trident_capacity_plan(struct trident_device *d)
{
    u8 spr, ext;
    u32 kib;
    int rc;
    if (!d || !d->res || !d->io) return TRI_E_INVAL;
    if (!d->init_done || !d->armed || !d->pinned) return TRI_E_STATE;
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

/* Bijective in the dword offset, with neither zero nor all-one page tags. */
static u32 pattern(u32 off) { return off ^ 0x69C35AA5u; }

/* Never abandon the remaining undo operations after one failure. Read back
 * each restored word; writing an absent upper word back to FF is harmless.
 * Page bodies are undone before tags, and all undo runs in reverse order. */
static int undo(struct trident_device *d, u32 off, const u32 *saved, u32 words)
{
    int result = 0;
    while (words) {
        u32 i = --words, value;
        int rc = WRITE(off + i * WORD_BYTES, saved[i]);
        if (rc) result = TRI_E_RESTORE;
        rc = READ(off + i * WORD_BYTES, &value);
        if (rc || value != saved[i]) result = TRI_E_RESTORE;
    }
    return result;
}

/* One non-retrying pass. end is the first suspect word; complete is the
 * fully verified prefix. On alias, the suspect page is restored before the
 * earlier tags, then the caller rechecks the lower prefix with no upper I/O.
 * scratch: 1024 original boundary words plus one saved page (8 KiB total). */
static int measure(struct trident_device *d, u32 limit, u32 *end, u32 *complete,
                   bool *mirror, bool *upper_checked, int *access_rc)
{
    u32 *tags = d->scratch, *page = tags + TAG_WORDS;
    u32 retained = 0;
    int restored = 0;
    *end = *complete = 0;
    for (u32 off = 0; off < limit; off += TRI_PAGE_BYTES) {
        u32 words = (limit - off) / WORD_BYTES, saved = 0;
        int stop = 0;
        if (words > PAGE_WORDS) words = PAGE_WORDS;
        for (; saved < words; ++saved) {
            stop = READ(off + saved * WORD_BYTES, &page[saved]);
            if (stop) { *access_rc = stop; break; }
        }
        /* A read fault can leave a partial page: still test its saved prefix. */
        words = saved;
        *end = off + words * WORD_BYTES;
        bool bad = false;
        for (u32 phase = 0; phase < 2 && !bad; ++phase) {
            for (u32 i = 0; i < words; ++i) {
                u32 at = off + i * WORD_BYTES;
                int rc = WRITE(at, phase ? ~pattern(at) : pattern(at));
                if (rc) { *access_rc = rc; *end = at; bad = true; break; }
            }
            for (u32 i = 0; i < words && !bad; ++i) {
                u32 at = off + i * WORD_BYTES, value;
                int rc = READ(at, &value);
                if (rc || value != (phase ? ~pattern(at) : pattern(at))) {
                    if (rc) *access_rc = rc;
                    *end = at; bad = true;
                }
            }
            bool separate = true;
            for (u32 i = 0; i < retained; ++i) {
                u32 value, at = i * TRI_PAGE_BYTES;
                int rc = READ(at, &value);
                if (rc || value != pattern(at)) {
                    if (rc) *access_rc = rc;
                    else *mirror = true;
                    separate = false;
                    *end = off; bad = true;
                }
            }
            if (off >= TRI_BAR0_DECODE_BYTES / 2 && words && separate && !stop)
                *upper_checked = true;
        }
        if (bad || stop || words < PAGE_WORDS) {
            if (undo(d, off, page, words)) restored = TRI_E_RESTORE;
            if (!bad) *complete = *end;
            break;
        }
        /* Leave only the boundary tag live; restore the page body now. */
        tags[retained] = page[0];
        if (undo(d, off + WORD_BYTES, page + 1, words - 1))
            restored = TRI_E_RESTORE;
        int rc = WRITE(off, pattern(off));
        ++retained; /* even a failed tag write must be undone */
        if (rc) { *access_rc = rc; *end = off; break; }
        if (restored) { *end = off; break; }
        *complete = *end = off + TRI_PAGE_BYTES;
    }
    while (retained) {
        u32 i = --retained;
        if (undo(d, i * TRI_PAGE_BYTES, tags + i, 1)) restored = TRI_E_RESTORE;
    }
    return restored;
}

static void diagnostic(struct trident_device *d, int rc, int access_rc)
{
    if (serial_gate_active()) return;
    serial_puts_polled("[trident] stage=vram spr=");
    serial_diag_hex(d->spr, 2);
    serial_puts_polled(" declared="); serial_diag_u32(d->declared_bytes);
    serial_puts_polled(d->declared_unknown ? " unknown=1 decode=" : " unknown=0 decode=");
    serial_diag_u32(d->res->decode_bytes);
    serial_puts_polled(" verified="); serial_diag_u32(d->verified_bytes);
    serial_puts_polled(d->upper == TRI_UPPER_MIRROR ? " mirror=yes" :
                       d->upper == TRI_UPPER_INDEPENDENT ? " mirror=no" : " mirror=unknown");
    serial_puts_polled(" rc="); serial_diag_i32(rc);
    serial_puts_polled(" access_rc="); serial_diag_i32(access_rc);
    serial_puts_polled("\n");
}

int trident_vram_verify(struct trident_device *d)
{
    if (!d || !d->res || !d->io || !d->scratch ||
        d->scratch_bytes < TRI_VRAM_SCRATCH_BYTES) return TRI_E_INVAL;
    if (!d->init_done || !d->armed || !d->pinned) return TRI_E_STATE;
    u32 limit = d->res->decode_bytes;
    if (limit > d->res->mapped_bytes) limit = d->res->mapped_bytes;
    if (limit > TRI_BAR0_DECODE_BYTES) limit = TRI_BAR0_DECODE_BYTES;
    limit -= limit % WORD_BYTES;
    u32 end, complete;
    bool mirror = false, upper_checked = false;
    int access_rc = 0;
    d->verified_bytes = 0;
    d->upper = TRI_UPPER_UNKNOWN;
    int rc = measure(d, limit, &end, &complete, &mirror, &upper_checked, &access_rc);
    if (!rc && end < limit && end) {
        /* Includes the mandatory lower-range recheck after an upper alias.
         * No access at or above the first suspect position on this pass. */
        u32 confirmed, lower_end;
        rc = measure(d, end, &lower_end, &confirmed, &mirror, &upper_checked, &access_rc);
        complete = confirmed;
    }
    d->verified_bytes = complete;
    if (mirror) d->upper = TRI_UPPER_MIRROR;
    else if (upper_checked) d->upper = TRI_UPPER_INDEPENDENT;
    if (!rc && d->verified_bytes < TRI_DISPLAY_BYTES)
        rc = access_rc ? access_rc : TRI_E_CAP_TOO_SMALL;
    diagnostic(d, rc, access_rc);
    /* Cleanup failure is diagnostic once the display prefix is verified. */
    return d->verified_bytes >= TRI_DISPLAY_BYTES ? TRI_OK : rc;
}
