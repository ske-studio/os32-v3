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
