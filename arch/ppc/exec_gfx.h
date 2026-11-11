#ifndef OS32_EXEC_GFX_H
#define OS32_EXEC_GFX_H
#include "types.h"
struct addrspace;
int exec_gfx_owner_release(struct addrspace *as);
int exec_gfx_return_emergency(u32 owner, int reason);
#endif
