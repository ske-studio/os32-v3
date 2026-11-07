/* Direct VRAM CLIENT present for both accelerated and NoAccel modes.
 * TRIDENT_FULL_DESIGN §2/F7 and the tri-w32 PM decision: no transfer.
 * F1 tri_sync implements X13/trident_accel.c:226-251 (MIT/X11 notice
 * retained in accel.c); do not add another wait here. */
#include "trident.h"

int trident_present(struct trident_device *d, int x, int y, int w, int h)
{
    /* The HAL caller owns drawable/generation checks and rectangle clipping.
     * CLIENT already aliases DISPLAY, so the rectangle needs no transfer. */
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return tri_sync(d);
}
