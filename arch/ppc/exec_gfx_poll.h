#ifndef OS32_EXEC_GFX_POLL_H
#define OS32_EXEC_GFX_POLL_H
#include "types.h"
#include <stdbool.h>
bool exec_gfx_poll_due(u32 now);
void exec_gfx_poll(void);
#endif
