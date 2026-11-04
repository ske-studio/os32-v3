/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
static void pll_write(struct gfinit_pll_sequence *q, u32 reg, u32 source, u32 keep, u32 value)
{
    if (q->count < GF_PLL_WRITES_MAX) q->writes[q->count++] = (struct gfinit_pll_write){reg, source, keep, value};
}
int gfinit_pll_sequence(u32 reg, const struct gfinit_pll_limits *l,
                        const struct gfinit_pll_solution *s, struct gfinit_pll_sequence *q)
{
    if (!q || !s || !limits_ok(l) || !s->actual || !s->m1 || !s->m2 || !s->n1 || !s->n2 ||
        s->n1 > GF_PLL_NM_MAX || s->n2 > GF_PLL_NM_MAX || s->m1 > GF_PLL_NM_MAX ||
        s->m2 > GF_PLL_NM_MAX || s->p > GF_PLL_P_MAX) return -1;
    q->count = 0;
    u32 nm1 = s->n1 << 8 | s->m1, nm2 = s->n2 << 8 | s->m2;
    bool single = s->n2 == s->m2;
    if (reg == GF_PLL_VIDEO || reg == GF_PLL_VIDEO_B) {
        /* nouveau nv04.c:184-193, 202-204, 218-226, 247-273. */
        bool head_b = reg == GF_PLL_VIDEO_B;
        u32 gate = head_b ? GF_PLL_GATE_VIDEO_B : GF_PLL_GATE_VIDEO;
        u32 stage = head_b ? GF_PLL_CTRL_SINGLE_B : GF_PLL_CTRL_SINGLE;
        u32 coeff = head_b ? GF_PLL_VIDEO_COEFF_B : GF_PLL_VIDEO_COEFF;
        pll_write(q, GF_PLL_GATE, GF_PLL_GATE, ~gate, 0);
        pll_write(q, GF_PLL_VIDEO_STAGE, GF_PLL_VIDEO_STAGE, ~stage, single ? stage : 0);
        pll_write(q, coeff, coeff, GF_PLL_VIDEO_S2_KEEP, GF_PLL_VIDEO_S2_ENABLE | nm2 | (single ? 0x011fU : 0));
        pll_write(q, reg, reg, GF_PLL_VIDEO_KEEP, s->p << GF_PLL_CTRL_P_SHIFT | nm1);
        pll_write(q, GF_PLL_GATE, GF_PLL_GATE, ~0U, 0); /* video gate restore */
    } else if (reg == GF_PLL_CORE || reg == GF_PLL_MEMORY) {
        bool mem = reg == GF_PLL_MEMORY;
        u32 preg = reg - GF_PLL_CTRL_OFFSET, keep = mem ? ~GF_PLL_CTRL_P_MASK_MEM : ~GF_PLL_CTRL_P_MASK;
        u32 bits = 0xc0000000U | s->p << GF_PLL_CTRL_P_SHIFT;
        if (mem) {
            u32 bias = s->p + l->bias_p;
            if (bias > l->max_p) bias = l->max_p;
            bits |= 1U << 28 | bias << GF_PLL_CTRL_BIAS_SHIFT;
            pll_write(q, GF_PLL_MEMORY_POWER, GF_PLL_MEMORY_POWER, ~0U, 8U << 28);
        }
        if (single) bits |= mem ? GF_PLL_CTRL_SINGLE_MEM : GF_PLL_CTRL_SINGLE;
        pll_write(q, preg, preg, ~0U, 1U << 28);
        pll_write(q, preg, preg, keep & ~(4U << 28), bits & ~(4U << 28));
        if (mem) {
            bits |= 8U << 20;
            pll_write(q, preg, preg, keep & ~(0xcU << 28), bits & ~(0xcU << 28));
            pll_write(q, GF_PLL_MEMORY_MIRROR, preg, keep & ~(0xcU << 28), bits & ~(0xcU << 28));
        }
        pll_write(q, GF_PLL_GATE, GF_PLL_GATE, mem ? ~GF_PLL_GATE_MEM : ~GF_PLL_GATE_CORE, 0);
        pll_write(q, reg, 0, 0, nm2 << 16 | nm1);
        if (mem) pll_write(q, GF_PLL_MEMORY_COEFF, 0, 0, nm2 << 16 | nm1);
        pll_write(q, preg, preg, keep, bits);
        if (mem) {
            bits &= ~(8U << 20); keep &= ~(8U << 20);
            pll_write(q, preg, preg, keep, bits);
            pll_write(q, GF_PLL_MEMORY_MIRROR, preg, keep, bits);
            pll_write(q, GF_PLL_MEMORY_POWER, GF_PLL_MEMORY_POWER, ~0U, 0);
        }
        pll_write(q, GF_PLL_GATE, GF_PLL_GATE, ~0U, 0); /* clock gate restore */
        if (mem) {
            pll_write(q, preg, preg, keep & ~(1U << 28), bits & ~(1U << 28));
            pll_write(q, GF_PLL_MEMORY_MIRROR, preg, keep & ~(1U << 28), bits & ~(1U << 28));
        }
    } else return -1;
    return 0;
}
