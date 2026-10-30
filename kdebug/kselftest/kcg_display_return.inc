/* T7a-2c fixed targets only; no arbitrary physical-read KAPI.
 * KDEBUG=0 has no injection.
 * A9h の値は VSYNC に依存し記録だけ (合否外)。 */
static void test_kcg_display_return(void)
{
    /* ANK 'A' row 4 has dots; KAC/A9h: io_disp.md:207-218,1068-1081; Bible §2-6-5. */
    enum { DIAG_ANK = 'A', DIAG_LINE = 4 };
    static const u32 windows[] = { TVRAM_CG_WINDOW, VRAM_PLANE_B,
        VRAM_PLANE_R, VRAM_PLANE_G, VRAM_PLANE_I };
    u32 before[5];
    KcgCacheStats cold, warm;
    u8 glyph[KCG_ANK_H];
    for (u32 i = 0; i < 5; i++) before[i] = paging_pte_flags(windows[i]);
    /* The absent facade has zero stats; a live body records this boot read. */
    kcg_rom_read_ank(DIAG_ANK, glyph);
    kcg_cache_stats(&cold);
    int ready = cold.misses != 0;
    outp(KCG_CODE_LO, 0); io_wait();
    outp(KCG_CODE_HI, DIAG_ANK); io_wait();
    outp(KCG_LINE_SEL, DIAG_LINE); io_wait();
    u32 code_vsync = !!(inp(GDC_TEXT_STAT) & GDC_STAT_VSYNC);
    u32 code = inp(KCG_DATA);
    outp(MODE_FF1_PORT, MFF1_KCG_BITMAP); io_wait();
    u32 dot_vsync = !!(inp(GDC_TEXT_STAT) & GDC_STAT_VSYNC);
    u32 dot = inp(KCG_DATA);
    v86_io_reset_policy();
    u32 restored_vsync = !!(inp(GDC_TEXT_STAT) & GDC_STAT_VSYNC);
    u32 restored = inp(KCG_DATA);
    kcg_rom_read_ank(DIAG_ANK, glyph);
    kcg_cache_stats(&warm);
    check(!ready || (warm.misses == cold.misses + 1 && warm.hits == cold.hits),
          "kcg:return cache miss");
    for (u32 i = 0; i < 5; i++) {
        u32 after = paging_pte_flags(windows[i]);
        check(((before[i] ^ after) & ~(PTE_ACCESSED | PTE_DIRTY)) == 0,
              "display:cache flags unchanged");
        check((after & (PTE_PRESENT | PTE_PCD | PTE_PWT)) ==
              (PTE_PRESENT | (i ? PTE_PCD : 0)),
              "display:CG WB VRAM UC");
    }
    kprintf(KLOG_ATTR, "[display] kcg=%u A9 code=%02x dot=%02x restored=%02x vsync=%u/%u/%u miss=%u/%u\n",
            ready, code, dot, restored, code_vsync, dot_vsync, restored_vsync,
            cold.misses, warm.misses);
    kprintf(KLOG_ATTR, "[display] PTE CG=%x B=%x R=%x G=%x I=%x\n",
            before[0], before[1], before[2], before[3], before[4]);
}
