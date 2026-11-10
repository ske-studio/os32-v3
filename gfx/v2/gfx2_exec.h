/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef GFX_V2_GFX2_EXEC_H
#define GFX_V2_GFX2_EXEC_H
#include "types.h"
struct addrspace;
int gfx2_owner_release(struct addrspace *as);
#endif
