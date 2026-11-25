#ifndef GFX_HW_MONO_CPU_H
#define GFX_HW_MONO_CPU_H

#include "types.h"

/* H3 implements, H2 calls after validation. MSB-first bits, byte pitch;
 * bg=-1 transparent, otherwise INDEX8. Clip rectangle is in screen space.
 * Uses the selected backend's put synchronously; borrows bits until return.
 * 0 success, negative error; failure can leave a partially drawn rectangle. */
int gfx_hw_mono_cpu(int x, int y, int w, int h, const u8 *bits, u32 pitch,
                    u8 fg, int bg, int cx, int cy, int cw, int ch);

#endif
