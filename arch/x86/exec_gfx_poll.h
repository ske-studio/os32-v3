#ifndef OS32_EXEC_GFX_POLL_H
#define OS32_EXEC_GFX_POLL_H
#include "../../drivers/video/geforce/geforce_module.h"
static inline bool exec_gfx_poll_due(u32 now) { return geforce_fg_due(now); }
static inline void exec_gfx_poll(void) { geforce_foreground_poll(); }
#endif
