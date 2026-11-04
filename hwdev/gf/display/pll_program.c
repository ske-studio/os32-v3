/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "calc.h"
int gf_mode_pll_program(const struct gfinit_pll_limits *limits,
                        const struct gfinit_mode_saved *saved, struct gfinit_mode *o)
{
    /* crtc.c:129-162,685-689. Reuse the integer solver/5-write sequence;
     * 50c includes head A bit28 as well as Curie bit2 and source bit9. */
    o->sel_clk = saved->sel_clk & ~GF_MODE_SEL_CLK_CLEAR;
    o->pllsel = GF_MODE_PLL_SELECT;
    o->limits = *limits;
    o->single = GF_MODE_CLOCK_KHZ <= limits->vco1.max_freq / 2;
    if (o->single) o->limits.vco2 = (struct gfinit_pll_vco){0};
    if (gfinit_pll_solve(&o->limits, GF_MODE_CLOCK_KHZ, &o->pll)) return -1;
    return gfinit_pll_sequence(GF_PLL_VIDEO, &o->limits, &o->pll, &o->sequence);
}

#include "api.h"

/* nouveau dispnv04/crtc.c:91-102,153-162 and nvreg.h:314-323. */
#define GF4_PLLSEL_HEAD_A ((1U << 28) | (1U << 9))
#define GF4_PLLSEL_HEAD_B ((1U << 29) | (1U << 11))
/* B2-b extends gfinit_pll_sequence to recognize this register:
 * nouveau nvkm/subdev/devinit/nv04.c:184-193,198-273. */
#define GF4_PLL_VIDEO_B 0x680520U

static i32 gf4_pll_route(const struct gf4_route *route, u8 head)
{
    if (route->head != head || head >= GF4_HEADS) return GF4_EINVAL;
    if (route->type > GF4_LVDS || route->location || route->ext_type)
        return GF4_EUNSUPPORTED;
    return GF4_OK;
}

i32 gf4_pllsel_calc(const struct gf4_head_image heads[GF4_HEADS], u32 *out)
{
    if (!heads || !out) return GF4_EINVAL;
    u32 value = GF_MODE_PLLSEL_USE_VPLL2;
    for (u8 head = 0; head < GF4_HEADS; ++head) {
        if (!heads[head].enabled) continue;
        i32 rc = gf4_pll_route(&heads[head].route, head);
        if (rc) return rc;
        if (head && heads[0].enabled &&
            (heads[0].route.or_mask & heads[head].route.or_mask)) return GF4_EBUSY;
        /* Internal nv17 TV contributes no TV PLLSEL bits: tvnv17.c:403-577. */
        value |= head ? GF4_PLLSEL_HEAD_B : GF4_PLLSEL_HEAD_A;
    }
    *out = value;
    return GF4_OK;
}

i32 gf4_pll_calc(const struct gf4_mode *mode, const struct gf4_route *route,
                  const struct gfinit_mode_saved *saved, const struct gfinit_pll_limits *limits,
                  struct gfinit_mode *out)
{
    if (!mode || !route || !saved || !limits || !out) return GF4_EINVAL;
    if (route->head >= GF4_HEADS) return GF4_EINVAL;
    i32 rc = gf4_pll_route(route, route->head);
    if (rc) return rc;
    struct gfinit_mode result = *out;
    result.sel_clk = saved->sel_clk & ~GF_MODE_SEL_CLK_CLEAR;
    result.pllsel = GF_MODE_PLLSEL_USE_VPLL2 |
        (route->head ? GF4_PLLSEL_HEAD_B : GF4_PLLSEL_HEAD_A);
    result.limits = *limits;
    result.single = mode->t.khz <= limits->vco1.max_freq / 2;
    if (result.single) result.limits.vco2 = (struct gfinit_pll_vco){0};
    if (gfinit_pll_solve(&result.limits, mode->t.khz, &result.pll) ||
        (unsigned long long)result.pll.error * 200U > mode->t.khz)
        return GF4_EPLL;
    if (gfinit_pll_sequence(route->head ? GF4_PLL_VIDEO_B : GF_PLL_VIDEO,
                            &result.limits, &result.pll, &result.sequence)) return GF4_EPLL;
    *out = result;
    return GF4_OK;
}
