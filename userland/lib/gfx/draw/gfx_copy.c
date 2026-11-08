#include "../gfx_direct_internal.h"
#include "../libgfx_internal.h"

int gfx_copy_rect(int dx, int dy, int sx, int sy, int w, int h)
{
    int left, top, right, bottom, x, y, xstep, ystep;
    if (gfx_direct) {
        int rc = gfx_cmd_enqueue_copy(dx, dy, sx, sy, w, h);
        if (!rc && !gfx_dirty_suppress)
            gfx_api->gfx_add_dirty_rect(dx, dy, w, h);
        return rc;
    }
    if (!gfx_ready) return OS32_ERR_NOSYS;
    if (w <= 0 || h <= 0) return 0;
    /* Clip both rectangles together, using wide arithmetic for the edges. */
    long long l = 0, t = 0, r = w, b = h;
    if (l < -(long long)dx) l = -(long long)dx;
    if (l < -(long long)sx) l = -(long long)sx;
    if (t < -(long long)dy) t = -(long long)dy;
    if (t < -(long long)sy) t = -(long long)sy;
    if (r > (long long)gfx_fb.width - dx) r = (long long)gfx_fb.width - dx;
    if (r > (long long)gfx_fb.width - sx) r = (long long)gfx_fb.width - sx;
    if (b > (long long)gfx_fb.height - dy) b = (long long)gfx_fb.height - dy;
    if (b > (long long)gfx_fb.height - sy) b = (long long)gfx_fb.height - sy;
    if (l >= r || t >= b) return 0;
    left = (int)l; top = (int)t; right = (int)r; bottom = (int)b;
    xstep = dx > sx ? -1 : 1;
    ystep = dy > sy ? -1 : 1;
    for (y = ystep > 0 ? top : bottom - 1; y >= top && y < bottom; y += ystep)
        for (x = xstep > 0 ? left : right - 1; x >= left && x < right; x += xstep)
            gfx_pixel_nodirty(dx + x, dy + y, gfx_get_pixel(sx + x, sy + y));
    if (!gfx_dirty_suppress)
        gfx_api->gfx_add_dirty_rect(dx + left, dy + top, right - left, bottom - top);
    return 0;
}
