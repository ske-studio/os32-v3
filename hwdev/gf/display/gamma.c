/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 * Copyright 2007-2009 Stuart Bennett
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "api.h"
#include "../foundation.h"

/* nouveau dispnv04/nvreg.h:92,129-139,433-437; hw.h:194-200. */
#define LUT_BAR 0U
#define LUT_STRIDE 0x2000U
#define LUT_MASK 0x6813c6U
#define LUT_INDEX 0x6813c8U
#define LUT_DATA 0x6813c9U
#define LUT_STATUS 0x6013daU
#define LUT_ATTR 0x6013c0U
#define LUT_MASK_ALL 0xffU
#define LUT_GATE_OFF 0x20U
#define LUT_ENTRIES 256U
#define LUT_COMPONENTS 3U

static int lut_check(struct gf4_ctx *ctx, u8 head)
{
    if (!ctx || !ctx->state || head >= GF4_HEADS) return GF4_EINVAL;
    if (!ctx->ready || !ctx->ch || !ctx->ch->f || ctx->epoch != ctx->ch->epoch)
        return GF4_ESTATE;
    return 0;
}

static struct gf4_head_image *lut_image(struct gf4_ctx *ctx, u8 head)
{
    return ctx->changing ? &ctx->state->next[head] : &ctx->state->head[head];
}

/* hw.c:795-810: complete RGB table, then reset AR flip-flop and gate. */
i32 gf4_lut_load(struct gf4_ctx *ctx, u8 head)
{
    int rc = lut_check(ctx, head);
    if (rc) return rc;
    struct gf4_head_image *h = lut_image(ctx, head);
    if (h->mode.fmt != GF3_INDEX8 && h->mode.fmt != GF3_RGB565 &&
        h->mode.fmt != GF3_XRGB8888) return GF4_EFORMAT;
    struct gf_io *io = &ctx->ch->f->io;
    u32 delta = head * LUT_STRIDE, ignored;
    h->lut_dirty = true;
    rc = io->write(io->opaque, LUT_BAR, LUT_MASK + delta, 1, LUT_MASK_ALL);
    if (!rc) rc = io->write(io->opaque, LUT_BAR, LUT_INDEX + delta, 1, 0);
    for (u32 i = 0; !rc && i < LUT_ENTRIES; ++i) {
        for (u32 c = 0; !rc && c < LUT_COMPONENTS; ++c) {
            u8 v = h->mode.fmt == GF3_INDEX8 ? h->palette[i * LUT_COMPONENTS + c] :
                (u8)(h->gamma[c][i] >> 8); /* crtc.c:778-797 */
            rc = io->write(io->opaque, LUT_BAR, LUT_DATA + delta, 1, v);
        }
    }
    if (!rc) rc = io->read(io->opaque, LUT_BAR, LUT_STATUS + delta, 1, &ignored);
    if (!rc) rc = io->write(io->opaque, LUT_BAR, LUT_ATTR + delta, 1, LUT_GATE_OFF);
    if (!rc) h->lut_dirty = false;
    return rc;
}

i32 gf4_palette_set(struct gf4_ctx *ctx, u8 head, u32 first, u32 count, const u8 *rgb)
{
    int rc = lut_check(ctx, head);
    if (rc) return rc;
    if (!rgb || first >= LUT_ENTRIES || !count || count > LUT_ENTRIES - first)
        return GF4_EINVAL;
    struct gf4_head_image *h = lut_image(ctx, head);
    if (h->mode.fmt != GF3_INDEX8) return GF4_EFORMAT;
    for (u32 i = 0; i < count * LUT_COMPONENTS; ++i)
        h->palette[first * LUT_COMPONENTS + i] = rgb[i];
    h->lut_dirty = true;
    return h->enabled ? gf4_lut_load(ctx, head) : 0;
}

int gf4_gamma(struct gf4_ctx *ctx, u8 head, const u16 rgb[3][256])
{
    int rc = lut_check(ctx, head);
    if (rc) return rc;
    if (!rgb) return GF4_EINVAL;
    struct gf4_head_image *h = lut_image(ctx, head);
    if (h->mode.fmt != GF3_RGB565 && h->mode.fmt != GF3_XRGB8888)
        return GF4_EFORMAT;
    for (u32 c = 0; c < LUT_COMPONENTS; ++c)
        for (u32 i = 0; i < LUT_ENTRIES; ++i)
            h->gamma[c][i] = rgb[c][i];
    h->lut_dirty = true;
    return h->enabled ? gf4_lut_load(ctx, head) : 0;
}
