/* Test-only present adapter; the GUI keeps its independent CPU present. */
#include "hwdev/gf/accel.h"

int gf3_present_m2mf(struct gf3_ctx *c, const struct gf3_surface *scan,
                     struct gf3_rect r, const struct gf3_ram *ram, u32 dl)
{
    return gf3_upload(c, scan, r, ram, true, dl);
}
