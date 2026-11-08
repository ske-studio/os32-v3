/* H1 declaration stage: H2 replaces these NOSYS entry points. */
#include "gfx_hal.h"

int gfx_hw_submit(const GFX_Cmd *cmds, u32 n, u32 gen, u32 *done)
{
    (void)cmds; (void)n; (void)gen; (void)done;
    return OS32_ERR_NOSYS;
}

int gfx_hw_get(int x, int y, int w, int h, u8 *dst, u32 pitch, u32 gen)
{
    (void)x; (void)y; (void)w; (void)h;
    (void)dst; (void)pitch; (void)gen;
    return OS32_ERR_NOSYS;
}
