#ifndef OS32_EXEC_GFX_POLL_H
#define OS32_EXEC_GFX_POLL_H
#include "exec_hooks.h"
#if OS32_HOOK_GFX_POLL
#include "geforce_contract.h"
static inline bool exec_gfx_poll_due(u32 now) { return geforce_fg_due(now); }
static inline void exec_gfx_poll(void) { geforce_foreground_poll(); }
#else
#include "types.h"
#include <stdbool.h>
bool exec_gfx_poll_due(u32 now);
void exec_gfx_poll(void);
#endif
#endif
