/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_IME 0
#include "exec_ime.h"

void exec_ime_owner_exit(int owner)
{
    (void)owner;
}
