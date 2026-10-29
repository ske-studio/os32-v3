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
