/* A0 inert entries; L1 supplies batching and synchronous GET. */
#include "../gfx_direct_internal.h"

int gfx_flush(void) { return 0; }
void gfx_cmd_drop(void) { }

int gfx_cmd_enqueue_fill(int x, int y, int w, int h, u8 color, u8 rop)
{
    (void)x; (void)y; (void)w; (void)h; (void)color; (void)rop;
    return OS32_ERR_NOSYS;
}

int gfx_cmd_enqueue_copy(int dx, int dy, int sx, int sy, int w, int h)
{
    (void)dx; (void)dy; (void)sx; (void)sy; (void)w; (void)h;
    return OS32_ERR_NOSYS;
}

int gfx_cmd_enqueue_put(int x, int y, int w, int h, const u8 *src, u32 pitch, int key)
{
    (void)x; (void)y; (void)w; (void)h; (void)src; (void)pitch; (void)key;
    return OS32_ERR_NOSYS;
}

int gfx_cmd_enqueue_mono(int x, int y, int w, int h, const u8 *bits, u32 pitch,
                         u8 fg, int bg, int cx, int cy, int cw, int ch)
{
    (void)x; (void)y; (void)w; (void)h; (void)bits; (void)pitch;
    (void)fg; (void)bg; (void)cx; (void)cy; (void)cw; (void)ch;
    return OS32_ERR_NOSYS;
}

int gfx_cmd_get(int x, int y, int w, int h, u8 *dst, u32 pitch)
{
    (void)x; (void)y; (void)w; (void)h; (void)dst; (void)pitch;
    return OS32_ERR_NOSYS;
}
