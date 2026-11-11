/* Capability initialization is needed by I1 surface allocation, before C9. */
#include "gfx2_int.h"
#include "kstring.h"

int gfx2_caps_from_v1(struct gfx2_adapter *a, GFX2_Caps *out)
{
    const GfxMode *m = gfx_backend_mode(a->v1);
    GFX2_Caps c = {0};
    c.size = sizeof(c); c.adapter = a->index; c.backend_kind = a->kind;
    c.fmt_surface = (1UL << GFX2_FMT_INDEX8) | (1UL << GFX2_FMT_RGB565) |
                    (1UL << GFX2_FMT_XRGB8888) | (1UL << GFX2_FMT_ARGB8888);
    c.fmt_render2d = c.fmt_src_stretch = c.fmt_surface;
    c.max_w = m ? m->width : GFX_WIDTH;
    c.max_h = m ? m->height : GFX_HEIGHT;
    c.pitch_align = c.offset_align = c.scanout_align = sizeof(u32);
    c.max_pitch = c.max_w * sizeof(u32);
    c.max_cmds = GFX2_MAX_CMDS;
    c.heads = c.connectors = 1;
    c.palette_entries = a->v1->bb_format == GFX_BB_PLANAR4 ? 16 : 256;
    *out = c;
    return 0;
}

void gfx2_caps_refresh(struct gfx2_adapter *a)
{
    GFX2_Caps c;
    if (!a->be->caps(a->ctx, &c)) a->caps = c;
}

int gfx2_caps(u32 index, GFX2_Caps *out, u32 size)
{
    struct gfx2_adapter *a = gfx2_adapter_get(index);
    if (!a || !out || size < sizeof(u32)) return OS32_ERR_INVAL;
    kmemcpy(out, &a->caps, size < sizeof(a->caps) ? size : sizeof(a->caps));
    return 0;
}
