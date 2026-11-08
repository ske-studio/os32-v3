/* A0 placeholder; L6 supplies SHADOW/DIRECT screen-to-screen COPY. */
#include "../gfx_direct_internal.h"

int gfx_copy_rect(int dx, int dy, int sx, int sy, int w, int h)
{
    (void)dx; (void)dy; (void)sx; (void)sy; (void)w; (void)h;
    return OS32_ERR_NOSYS;
}
