/* Shared HAL v2 pixel formats; kernel and userland, without libc. */
#ifndef GFX_V2_GFX2_FMT_H
#define GFX_V2_GFX2_FMT_H
#include "types.h"
#include "os32_gfx2.h"

struct gfx2_fmt_info { u8 bpp, cpp, has_alpha, indexed; u8 rs, rb, gs, gb, bs, bb, as, ab; }; /* 位置・幅 */
int  gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *o);      /* C2。0/INVAL */
u32  gfx2_px_read(const u8 *p, u32 fmt);  void gfx2_px_write(u8 *p, u32 fmt, u32 v);   /* C2 */
u32  gfx2_to_argb(u32 fmt, u32 v, const u32 *pal256);       /* C2。§3-3 の拡げ */
int  gfx2_from_argb(u32 fmt, u32 argb, u32 *v);            /* C2。INDEX8 は NOSYS */
u32  gfx2_yuv_argb(u8 y, u8 u, u8 v);  u32 gfx2_div255(u32 t);                       /* C2 */

#endif
