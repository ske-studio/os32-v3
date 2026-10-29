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
