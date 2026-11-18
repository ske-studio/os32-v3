/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_SND 0
#include "exec_snd.h"

void exec_snd_owner_exit(int owner)
{
    (void)owner;
}
