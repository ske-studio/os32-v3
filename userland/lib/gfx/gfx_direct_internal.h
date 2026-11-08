#ifndef GFX_DIRECT_INTERNAL_H
#define GFX_DIRECT_INTERNAL_H

#include "libos32gfx.h"

/* L0 owns storage in libos32gfx_core.c. L1 sets lost on submit/get failure.
 * gen is the nonzero u16 screen_info.reserved[0], widened for KAPI calls. */
extern int gfx_direct;
extern u16 gfx_gen;
extern int gfx_lost;

/* L1 owns draw/gfx_cmd.c. All return 0 on success, negative on failure.
 * FILL: rop 0=COPY, 1=XOR. COPY: destination precedes source.
 * PUT/MONO copy source bytes into the 4KiB queue arena before returning;
 * row splitting and automatic flush preserve ordering, never retain src.
 * key/bg=-1 means no key/transparent background respectively; else INDEX8.
 * MONO bits are MSB-first, clip rectangle is in screen coordinates.
 * Invalid/unrepresentable inputs fail rather than truncate to GFX_Cmd i16.
 * A failure may have submitted a prefix: callers must not replay commands. */
int gfx_cmd_enqueue_fill(int x, int y, int w, int h, u8 color, u8 rop);
int gfx_cmd_enqueue_copy(int dx, int dy, int sx, int sy, int w, int h);
int gfx_cmd_enqueue_put(int x, int y, int w, int h, const u8 *src, u32 pitch, int key);
int gfx_cmd_enqueue_mono(int x, int y, int w, int h, const u8 *bits, u32 pitch,
                         u8 fg, int bg, int cx, int cy, int cw, int ch);
/* Flush first, GET only on success. Borrow dst until return; no queued GET. */
int gfx_cmd_get(int x, int y, int w, int h, u8 *dst, u32 pitch);
/* Discard pending commands and their copied data; never submits. */
void gfx_cmd_drop(void);

#endif
