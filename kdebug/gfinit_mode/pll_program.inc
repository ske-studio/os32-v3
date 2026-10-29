    /* crtc.c:129-162,685-689. Reuse the integer solver/5-write sequence;
     * 50c includes head A bit28 as well as Curie bit2 and source bit9. */
    o->sel_clk = saved->sel_clk & ~GF_MODE_SEL_CLK_CLEAR;
    o->pllsel = GF_MODE_PLL_SELECT;
    o->limits = *limits;
    o->single = GF_MODE_CLOCK_KHZ <= limits->vco1.max_freq / 2;
    if (o->single) o->limits.vco2 = (struct gfinit_pll_vco){0};
    if (gfinit_pll_solve(&o->limits, GF_MODE_CLOCK_KHZ, &o->pll)) return -1;
    return gfinit_pll_sequence(GF_PLL_VIDEO, &o->limits, &o->pll, &o->sequence);
