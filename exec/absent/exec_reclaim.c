/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_RECLAIM 0
#include "exec_reclaim.h"
#include "platform_host.h"

void exec_pcm_reclaim(int owner) { (void)owner; }
void exec_host_owner_exit(int owner) { platform_host_owner_exit(owner); }
