/* HAL v2 userland entry points and the SDK KernelAPI declarations. */
#ifndef OS32GFX2U_H
#define OS32GFX2U_H
#include "os32api.h"

/* INDEX8 chooses the nearest RGB entry (ties use the lowest index).
 * pal256 is required only for INDEX8. Errors leave *out unchanged.
 * All other formats return the shared format converter's result.
 */
int gfx2_color_pack(u32 fmt, u32 argb, const u32 *pal256, u32 *out);

#endif
