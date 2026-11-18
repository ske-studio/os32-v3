/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_GFX_POLL 0
#include "exec_gfx_poll.h"
bool exec_gfx_poll_due(u32 now) { (void)now; return false; }
void exec_gfx_poll(void) { }
