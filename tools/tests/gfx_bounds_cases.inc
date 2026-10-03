/* Called with each real backend active, both normal and 200-line init. */
static void bounds_test(u32 backend)
{
    struct gfx_kernel_fb fb;
    CHECK(!gfx_kernel_framebuffer(&fb));
    int height = (backend == LEDGER_SF_PC98) ? gfx_current_height : (int)MEM_GFX_BB8_HEIGHT;
    u32 commits = gfx_counters.commits;
    u8 *guard = (u8 *)P2V(PEGC_LINEAR_BASE) + PEGC_FB_SIZE_480;
    u8 saved[16];
    for (unsigned i=0;i<sizeof(saved);i++) { saved[i]=guard[i]; guard[i]=0xa5; }
    dirty_queue.count=0;prev_dirty.count=0;
    gfx_present_rect(GFX_WIDTH,0,0x7fffffff,1);
    CHECK(gfx_counters.commits == commits);
    gfx_present_rect(0,(-0x7fffffff-1),1,-1);
    CHECK(gfx_counters.commits == commits);
    gfx_add_dirty_rect(0,(-0x7fffffff-1),1,-1);
    CHECK(!dirty_queue.count);
    /* Huge positive extents clip to the one remaining corner pixel. */
    gfx_present_rect(GFX_WIDTH-1,height-1,0x7fffffff,0x7fffffff);
    CHECK(gfx_counters.commits > commits);
    for (unsigned i=0;i<sizeof(saved);i++) { CHECK(guard[i]==0xa5); guard[i]=saved[i]; }
    if (backend == LEDGER_SF_CIRRUS) {
        CHECK(gfx_hw_fill_rect(GFX_WIDTH,0,0x7fffffff,1,1)==0);
        CHECK(gfx_hw_fill_rect(GFX_WIDTH-1,height-1,0x7fffffff,0x7fffffff,1)==0);
        CHECK(gfx_hw_blit(0x7fffffff,0,0,0,1,1)==0);
    }
    if (backend == LEDGER_SF_PC98) {
        vram_scroll_y=1;
        gfx_hardware_scroll(0x7fffffff);
        CHECK(vram_scroll_y == (1 + 0x7fffffff % gfx_current_height) % gfx_current_height);
        gfx_hardware_scroll((-0x7fffffff-1));
        CHECK(vram_scroll_y >= 0 && vram_scroll_y < gfx_current_height);
        vram_scroll_y=0;
    }
}
