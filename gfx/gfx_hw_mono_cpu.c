/* A0 placeholder; H3 replaces this with bounded CPU expansion via put. */
#include "gfx_hw_mono_cpu.h"
#include "os32_kapi_shared.h"

int gfx_hw_mono_cpu(int x, int y, int w, int h, const u8 *bits, u32 pitch,
                    u8 fg, int bg, int cx, int cy, int cw, int ch)
{
    (void)x; (void)y; (void)w; (void)h; (void)bits; (void)pitch;
    (void)fg; (void)bg; (void)cx; (void)cy; (void)cw; (void)ch;
    return OS32_ERR_NOSYS;
}
