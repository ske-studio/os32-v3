/* GF_DRIVER_DESIGN §4-1: CPU copy to the mapped BAR1 display surface. */
#include "gfx_hal.h"
#include "../../../gfx/gfx_internal.h"
#include "hwdev/gfinit_mode.h"
#ifdef OS32_GEFORCE_MODULE
#include "geforce_module.h"
#define gfx_counters (*geforce_context->counters)
#endif

/* GF6200_4C_DESIGN §4: read-safe PFB capacity register in BAR0. */
#define GF_PRESENT_PFB_AMOUNT 0x10020cU

/* bar0 and display are already mapped device pointers. point is the caller's
 * non-reentrant identity/interruption check; it runs before every row, so a
 * one-row present is checked too and the final BAR0 drain follows a passed
 * check. A failed check returns -1 without touching either window again,
 * still accounting for the rows already written. */
int gf_present_rect(const u8 *client, u32 client_pitch,
                    volatile u8 *display, u32 display_pitch,
                    volatile u8 *bar0, int x, int y, int w, int h,
                    int (*point)(void *), void *opaque)
{
    u32 rows = 0;
    int rc = 0;

    if (!gfx_clip_screen(&x, &y, &w, &h, GF_MODE_WIDTH, GF_MODE_HEIGHT))
        return 0;

    client += (u32)y * client_pitch + (u32)x;
    display += (u32)y * display_pitch + (u32)x;
    for (int row = 0; row < h; row++) {
        if (point(opaque)) {
            rc = -1;
            break;
        }
        for (int col = 0; col < w; col++)
            display[col] = client[col];
        rows++;
        client += client_pitch;
        display += display_pitch;
    }

    /* A BAR0 read drains posted BAR1 writes; do not read the display. */
    if (!rc) (void)*(volatile u32 *)(bar0 + GF_PRESENT_PFB_AMOUNT);
    gfx_counters.present_bytes += (u32)w * rows;
    if (!rc) gfx_counters.commits++;
    return rc;
}
