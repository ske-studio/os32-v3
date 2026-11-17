/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_GFX 0
#include "exec_gfx.h"
int exec_gfx_owner_release(struct addrspace *as) { (void)as; return 0; }
int exec_gfx_return_emergency(u32 owner, int reason) { (void)owner; (void)reason; return 0; }
