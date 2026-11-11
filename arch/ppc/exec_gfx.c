#include "exec_gfx.h"
int exec_gfx_owner_release(struct addrspace *as) { (void)as; return 0; }
int exec_gfx_return_emergency(u32 owner, int reason) { (void)owner; (void)reason; return 0; }
