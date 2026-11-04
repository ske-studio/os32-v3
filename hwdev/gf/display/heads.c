/*
 * Copyright 2012 Red Hat Inc.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include "api.h"

/* nouveau nvkm/engine/disp/nv04.c:30-44 enables VBLANK per head.
 * F1 owns the actual IRQ programming and ACK (irq.c:106-125); its wanted
 * fields change even on failure. The caller's transaction rollback restores
 * those fields, while the channel's published head set changes only on success. */
i32 gf4_heads_apply(struct gf4_ctx *ctx, u32 heads)
{
    struct gf_foundation *f = ctx->ch->f;
    int rc = gf_irq_enable(f, f->wanted_fifo != 0, f->wanted_graph != 0, heads);
    if (rc) return rc;
    ctx->ch->heads = heads;
    return 0;
}
