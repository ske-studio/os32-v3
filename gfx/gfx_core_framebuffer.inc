/* Kernel and legacy framebuffer access and CLIENT binding. */
/* ======================================================================== */
/*  KAPI: フレームバッファ取得                                              */
/* ======================================================================== */
/* Internal backing is separate from the future public value/lease ABI.
 * Before init (including prepare), the framebuffer is the boot PC98 CLIENT. */
STATIC_ASSERT(GFX_FMT_PLANAR4 == GFX_BB_PLANAR4, gfx_planar_format_matches);
STATIC_ASSERT(GFX_FMT_PACKED8 == GFX_BB_PACKED8, gfx_packed_format_matches);

static int gfx_client_framebuffer(struct gfx_kernel_fb *out, int selected)
{
    struct gfx_kernel_fb fb = {0};
    const struct ledger_surface *sf;
    const GfxMode *mode = gfx_backend_mode(g_backend);
    u8 *base;
    u32 i;
    if (!out) return OS32_ERR_INVAL;
    sf = ledger_surface_find(selected ? gfx_sf_backend() : LEDGER_SF_PC98,
                             LEDGER_ROLE_CLIENT);
    fb.width = GFX_WIDTH;
    fb.height = GFX_HEIGHT;
    fb.pitch = GFX_BPL;
    fb.format = GFX_BB_PLANAR4;
    if (sf && !sf->closing) {
        fb.width = sf->width;
        fb.height = sf->height;
        fb.pitch = sf->pitch;
        fb.format = sf->format;
        if (selected && mode) {
            fb.width = mode->width;
            fb.height = gfx_started ? gfx_current_height : mode->height;
            fb.pitch = mode->pitch;
            fb.format = mode->format;
        }
        base = (sf->backing == LEDGER_SB_MMIO || sf->backing == LEDGER_SB_VRAM)
             ? (u8 *)P2V_IO(sf->first * PAGE_SIZE) : (u8 *)P2V(sf->first * PAGE_SIZE);
        for (i = 0; i < sf->planes; i++) fb.planes[i] = base + sf->plane_offset[i];
    }
    *out = fb;
    return fb.planes[0] ? 0 : OS32_ERR_INVAL;
}

int gfx_kernel_framebuffer(struct gfx_kernel_fb *out)
{
    return gfx_client_framebuffer(out, gfx_started);
}

/* USER gets a revocable lease VA; frame-less TRUSTED calls keep the alias. */
void __cdecl gfx_get_framebuffer(GFX_Framebuffer *fb)
{
    struct gfx_kernel_fb kernel;
    int i;
    if (ring3_call_from_user()) { gfx_framebuffer_bridge(fb); return; }
    if (!fb) return;
    (void)gfx_client_framebuffer(&kernel, 1);
    fb->width = kernel.width;
    fb->height = kernel.height;
    fb->pitch = kernel.pitch;
    for (i = 0; i < SURFACE_QUERY_MAX; i++) fb->planes[i] = kernel.planes[i];
}

/* All legacy planar pointers and the selected backend descriptor share one
 * binding point. Missing CLIENT must erase pointers from the previous mode. */
static void gfx_bind_client(void)
{
    const struct ledger_surface *sf = ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_CLIENT);
    GfxBackend *backend = gfx_sf_backend() == LEDGER_SF_GEFORCE ? &gfx_backend_geforce :
                          gfx_sf_backend() == LEDGER_SF_PEGC ? &gfx_backend_pegc :
                          gfx_sf_backend() == LEDGER_SF_CIRRUS ? &gfx_backend_cirrus : &gfx_backend_pc98;
    u8 *base = 0;
    u32 i;
    if (gfx_mode_ready && sf && !sf->closing)
        base = (sf->backing == LEDGER_SB_MMIO || sf->backing == LEDGER_SB_VRAM)
             ? (u8 *)P2V_IO(sf->first * PAGE_SIZE) : (u8 *)P2V(sf->first * PAGE_SIZE);
    for (i = 0; i < SURFACE_QUERY_MAX; i++) bb[i] = base && i < sf->planes ? base + sf->plane_offset[i] : 0;
    bb_b = bb[0]; bb_r = bb[1]; bb_g = bb[2]; bb_i = bb[3];
    backend->bb_base = bb[0];
    backend->bb_size = base ? sf->npages * PAGE_SIZE : 0;
    if (base) {
        backend->bb_pitch = sf->pitch;
        backend->bb_format = sf->format;
    }
}

