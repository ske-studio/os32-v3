/* HAL v2 software readback. Inputs have been validated by submit. */
#include "gfx2_sw.h"

int gfx2_sw_readback(const struct gfx2_exec *ex, u8 *kbuf)
{
    const GFX2_Op *op = ex->op;
    struct gfx2_fmt_info info;
    int rc;

    if (!op->sw || !op->sh) return 0;
    rc = gfx2_fmt_info(ex->sv.fmt, &info);
    if (rc) return rc;
    for (u32 y = 0; y < op->sh; ++y) {
        u8 *dst = kbuf + y * op->pitch;
        if (ex->sv.sync) {
            rc = gfxsync_read_rows(ex->sv.sync, op->sx, op->sy + y,
                                   op->sw, 1, dst, op->pitch);
            if (rc) return rc;
        } else {
            const u8 *src = ex->sv.base + (op->sy + y) * ex->sv.pitch + op->sx * info.cpp;
            for (u32 x = 0; x < op->sw * info.cpp; ++x) dst[x] = src[x];
        }
    }
    return 0;
}
