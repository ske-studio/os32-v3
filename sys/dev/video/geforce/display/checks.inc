/* Shared required display checks; no route/cell or diagnostic comparison policy. */
    /* DESIGN §§3-4: coefficients are checked only after 50C selection.
     * 510 is evidence only; TEST excludes sense and compares bit20 only. */
    CHECK_MMIO(C_PLL_SELECT,~GF_MODE_PLLSEL_VPLL_A,m->pllsel); /* bit28 reads 0 on NV44A */
    CHECK_MMIO(C_PLL_STATUS,0,0);
    const struct gfinit_pll_write *q=m->sequence.writes;
    CHECK_MMIO(GF_PLL_VIDEO,~GF_PLL_VIDEO_KEEP,q[C_PLL_PRIMARY_WRITE].value);
    CHECK_MMIO(GF_PLL_VIDEO_COEFF,~GF_PLL_VIDEO_S2_KEEP,q[C_PLL_COEFF_WRITE].value);
    CHECK_MMIO(GF_PLL_VIDEO_STAGE,GF_PLL_CTRL_SINGLE,q[C_PLL_STAGE_WRITE].value);
    CHECK_SAVED(7,~0U,~GF_MODE_SEL_CLK_CLEAR);
    CHECK_SAVED(GF_DISPLAY_COUNT(c_fields),~0U,~0U);
    CHECK_MMIO(C_CONFIG,C_CONFIG_MASK,m->config);
    CHECK_MMIO(C_START,~0U,m->start);
    for(u32 index=0;index<=GF_MODE_CR_LINE_COMPARE;index++) {
        u32 expected=m->cr[index];
        if(index==GF_MODE_CR_VE || index==GF_MODE_CR_MODE)expected|=C_CR_LOCK;
        CHECK_INDEX(GF_C_CR,index,GF_MODE_BYTE_MASK,expected);
    }
    const u32 extra[]={GF_MODE_CR_PITCH_HI,GF_MODE_CR_RPC1,GF_MODE_CR_LSR,
        GF_MODE_CR_PIXEL,RECOVER_CR44,GF_MODE_CR_PITCH11,GF_MODE_CR_BURST,GF_MODE_CR_LWM,C_CR_PITCH_EXTRA};
    for(u32 i=0;i<GF_DISPLAY_COUNT(extra);i++)
        CHECK_INDEX(GF_C_CR,extra[i],
            extra[i]==GF_MODE_CR_RPC1 ? C_CR_RPC1_MASK : GF_MODE_BYTE_MASK,m->cr[extra[i]]);
    CHECK_SAVED(1,GF_MODE_BYTE_MASK,~GF_MODE_LCD_CLEAR);
    CHECK_INDEX(GF_C_SR,1,GF_MODE_BYTE_MASK,GF_MODE_DAC_ENABLE);
    CHECK_INDEX(GF_C_MISC,0,GF_MODE_BYTE_MASK,m->misc);
    CHECK_MMIO(C_GENERAL,~0U,m->general);
    CHECK_MMIO(C_DACCLK,C_DACCLK_MASK,m->dacclk);
    CHECK_MMIO(C_TEST,GF_MODE_TEST_CONTROL,m->test_control);
