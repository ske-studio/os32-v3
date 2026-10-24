/*
 * Copyright 2005-2006 Erik Waling
 * Copyright 2006 Stephane Marchesin
 * Copyright 1993-2003 NVIDIA, Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
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
/* NV44A-only adaptation of nouveau c03c1a1 MIT bios/pll.c:301-333,
 * clk/pllnv04.c:28-247 and devinit/nv04.c:200-353. Data tables only.
 * Arithmetic is u32 throughout; no libc, floating point or runtime helpers. */
#include "gfinit_pll.h"
#include <stdbool.h>
STATIC_ASSERT(GF_PLL_REF <= U32_MAX / GF_PLL_NM_MAX / GF_PLL_NM_MAX, pll_product);
static int pll_span(u32 size, u32 at, u32 n)
{
    return at <= size && n <= size - at ? 0 : -1;
}
static u32 pll_le(const u8 *r, u32 at, u32 n)
{
    u32 v = 0;
    for (u32 i = 0; i < n; i++) v |= (u32)r[at + i] << (8 * i);
    return v;
}
static int vco_ok(const struct gfinit_pll_vco *v)
{
    return v->min_freq && v->min_freq <= v->max_freq &&
        v->min_input && v->min_input <= v->max_input &&
        v->min_n && v->min_n <= v->max_n && v->max_n <= GF_PLL_NM_MAX &&
        v->min_m && v->min_m <= v->max_m && v->max_m <= GF_PLL_NM_MAX;
}
static int limits_ok(const struct gfinit_pll_limits *l)
{
    return l && l->ref == GF_PLL_REF && l->max_p <= 7 && l->bias_p <= 7 &&
        vco_ok(&l->vco1) && (!l->vco2.max_freq || vco_ok(&l->vco2));
}
int gfinit_pll_parse(const u8 *r, u32 size, u32 table, u32 reg, struct gfinit_pll_limits *o)
{
    if (!r || !o || pll_span(size, table, GF_PLL_TBL_HDR_MIN) ||
        r[table + GF_PLL_HDR_VERSION] != GF_PLL_TBL_VERSION ||
        r[table + GF_PLL_HDR_HEADER_LEN] < GF_PLL_TBL_HDR_MIN ||
        r[table + GF_PLL_HDR_ENTRY_LEN] < GF_PLL_TBL_ENTRY_MIN || r[table + GF_PLL_HDR_COUNT] != GF_PLL_TBL_COUNT) return -1;
    u32 header = r[table + GF_PLL_HDR_HEADER_LEN], len = r[table + GF_PLL_HDR_ENTRY_LEN];
    u32 count = r[table + GF_PLL_HDR_COUNT];
    if (pll_span(size, table, header + len * count)) return -1;
    u32 key = reg;
    if (reg == GF_PLL_CORE || reg == GF_PLL_MEMORY) key -= GF_PLL_CTRL_OFFSET;
    else if (reg != GF_PLL_VIDEO) return -1;
    /* No matching key: use the first entry, as nouveau nvkm/subdev/bios/pll.c:162-175
     * (MIT) does for version 0x2x tables (Ra266's ROM keys only the memory PLL). */
    u32 at = table + header, found = 0;
    for (u32 i = 0; i < count; i++) {
        if (pll_le(r, table + header + i * len + GF_PLL_ENT_REG, 4) != key) continue;
        if (found++) return -1;
        at = table + header + i * len;
    }
    *o = (struct gfinit_pll_limits){0};
    o->vco1 = (struct gfinit_pll_vco){
        pll_le(r, at + GF_PLL_ENT_VCO1_MIN, 2) * GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at + GF_PLL_ENT_VCO1_MAX, 2) * GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at + GF_PLL_ENT_VCO1_INPUT_MIN, 2) * GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at + GF_PLL_ENT_VCO1_INPUT_MAX, 2) * GF_PLL_KHZ_PER_MHZ,
        r[at + GF_PLL_ENT_VCO1_N_MIN],
        r[at + GF_PLL_ENT_VCO1_N_MAX],
        r[at + GF_PLL_ENT_VCO1_M_MIN],
        r[at + GF_PLL_ENT_VCO1_M_MAX]
    };
    o->vco2 = (struct gfinit_pll_vco){
        pll_le(r, at + GF_PLL_ENT_VCO2_MIN, 2) * GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at + GF_PLL_ENT_VCO2_MAX, 2) * GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at + GF_PLL_ENT_VCO2_INPUT_MIN, 2) * GF_PLL_KHZ_PER_MHZ,
        pll_le(r, at + GF_PLL_ENT_VCO2_INPUT_MAX, 2) * GF_PLL_KHZ_PER_MHZ,
        r[at + GF_PLL_ENT_VCO2_N_MIN],
        r[at + GF_PLL_ENT_VCO2_N_MAX],
        r[at + GF_PLL_ENT_VCO2_M_MIN],
        r[at + GF_PLL_ENT_VCO2_M_MAX]
    };
    o->max_p = r[at + GF_PLL_ENT_MAX_P]; o->bias_p = r[at + GF_PLL_ENT_BIAS_P];
    o->ref = len > GF_PLL_ENT_REF_LAST ? pll_le(r, at + GF_PLL_ENT_REF, 4) : 0;
    if (!o->ref) o->ref = GF_PLL_REF;
    if (!limits_ok(o)) return -1;
    return 0;
}
static void pll_best(struct gfinit_pll_solution *s, u32 clk, u32 actual,
                     u32 n1, u32 m1, u32 n2, u32 m2, u32 p)
{
    u32 error = actual > clk ? actual - clk : clk - actual;
    if (actual && (!s->actual || error < s->error))
        *s = (struct gfinit_pll_solution){n1, m1, n2, m2, p, actual, error};
}
int gfinit_pll_solve(const struct gfinit_pll_limits *l, u32 clk, struct gfinit_pll_solution *s)
{
    if (!s || !limits_ok(l) || !clk || clk > (U32_MAX >> GF_PLL_P_MAX)) return -1;
    *s = (struct gfinit_pll_solution){0};
    const struct gfinit_pll_vco *a = &l->vco1, *b = &l->vco2;
    u32 max = a->max_freq, min = a->min_freq;
    if (!b->max_freq) {
        if ((clk << GF_PLL_P_MAX) < min) { min = clk * GF_PLL_P_MAX; max = min * 2; }
        if (clk + clk / GF_PLL_MARGIN_DIV > max) max = clk + clk / GF_PLL_MARGIN_DIV;
        for (u32 p = 0; p <= GF_PLL_P_MAX; p++) {
            u32 div = 1U << p, cp = clk << p;
            if (cp < min) continue;
            if (cp > max) break;
            for (u32 m = a->min_m; m <= a->max_m; m++) {
                if (GF_PLL_REF / m < a->min_input) return s->actual ? 0 : -1;
                if (GF_PLL_REF / m > a->max_input) continue;
                if (cp > (U32_MAX - GF_PLL_REF / 2) / m) return -1;
                u32 n = (cp * m + GF_PLL_REF / 2) / GF_PLL_REF;
                if (n < a->min_n) continue;
                if (n > a->max_n) break;
                u32 actual = ((n * GF_PLL_REF + div / 2) / div + m / 2) / m;
                pll_best(s, clk, actual, n, m, 1, 1, p);
                if (s->actual && !s->error) return 0;
            }
        }
    } else {
        u32 p = 0, half = (b->max_freq - b->max_freq / GF_PLL_MARGIN_DIV) / 2;
        while (p < GF_PLL_P_MAX && clk <= (half >> p)) p++;
        u32 cp = clk << p, max2 = b->max_freq;
        if (max2 < clk + clk / GF_PLL_MARGIN_DIV) max2 = clk + clk / GF_PLL_MARGIN_DIV;
        bool fixed = b->min_m == b->max_m && b->min_n == b->max_n;
        for (u32 m1 = a->min_m; m1 <= a->max_m; m1++) {
            if (GF_PLL_REF / m1 < a->min_input) break;
            if (GF_PLL_REF / m1 > a->max_input) continue;
            for (u32 n1 = a->min_n; n1 <= a->max_n; n1++) {
                u32 c1 = GF_PLL_REF * n1 / m1;
                if (c1 < a->min_freq) continue;
                if (c1 > a->max_freq) break;
                for (u32 m2 = b->min_m; m2 <= b->max_m; m2++) {
                    if (c1 / m2 < b->min_input) break;
                    if (c1 / m2 > b->max_input) continue;
                    if (cp > (U32_MAX - c1 / 2) / m2) return -1;
                    u32 n2 = (cp * m2 + c1 / 2) / c1;
                    if (n2 < b->min_n) continue;
                    if (n2 > b->max_n) break;
                    u32 c2 = c1;
                    if (!fixed) {
                        if (n2 / m2 < 4 || n2 / m2 > 10) continue;
                        c2 = c1 * n2 / m2;
                        if (c2 < b->min_freq) break;
                        if (c2 > max2) continue;
                    }
                    pll_best(s, clk, c2 >> p, n1, m1, n2, m2, p);
                    if (s->actual && !s->error) return 0;
                }
            }
        }
    }
    return s->actual ? 0 : -1;
}
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
    if (reg == GF_PLL_VIDEO) {
        pll_write(q, GF_PLL_GATE, GF_PLL_GATE, ~GF_PLL_GATE_VIDEO, 0);
        pll_write(q, GF_PLL_VIDEO_STAGE, GF_PLL_VIDEO_STAGE, ~GF_PLL_CTRL_SINGLE, single ? GF_PLL_CTRL_SINGLE : 0);
        pll_write(q, GF_PLL_VIDEO_COEFF, GF_PLL_VIDEO_COEFF, GF_PLL_VIDEO_S2_KEEP, GF_PLL_VIDEO_S2_ENABLE | nm2 | (single ? 0x011fU : 0));
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
