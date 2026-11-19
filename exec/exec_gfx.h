#ifndef OS32_EXEC_GFX_H
#define OS32_EXEC_GFX_H
#include "exec_hooks.h"
#if OS32_HOOK_GFX
#include "gfx.h"
#include "display_provider.h"
#include "gfx_hal.h"
#include "gfx_emergency.h"
#include "../gfx/v2/gfx2_exec.h"
static inline int exec_gfx_owner_release(struct addrspace *as) { return gfx2_owner_release(as); }
static inline int exec_gfx_return_emergency(u32 owner, int reason) { return gfx_return_emergency(owner, reason); }
static inline void exec_gfx_owner_exit(int id) { display_provider_owner_exit(id); }
#else
#include "types.h"
struct addrspace;
int exec_gfx_owner_release(struct addrspace *as);
int exec_gfx_return_emergency(u32 owner, int reason);
void exec_gfx_owner_exit(int id);
#endif
#endif
