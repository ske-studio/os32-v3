/* Kernel console target: an owned HAL v2 sysmem surface, never a VRAM alias. */
#ifndef OS32_GFX_CONSOLE_H
#define OS32_GFX_CONSOLE_H
#include "types.h"
struct gfx_console_target {
    u32 adapter, generation, head, sid, owner;
    u32 width, height, pitch;
    int locked;
};
/* Targets must initially be zero. Close before opening again. */
int gfx_console_open(u32 adapter, u32 head, struct gfx_console_target *t);
int gfx_console_lock(struct gfx_console_target *t, u8 **pixels);
int gfx_console_unlock(struct gfx_console_target *t, i32 x, i32 y, i32 w, i32 h);
int gfx_console_present(struct gfx_console_target *t);
int gfx_console_close(struct gfx_console_target *t);
#endif
