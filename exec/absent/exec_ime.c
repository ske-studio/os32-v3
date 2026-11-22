/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_IME 0
#include "exec_ime.h"
#include "ime_control.h"

void ime_set_render(void *table)
{
    (void)table;
}

void exec_ime_owner_exit(int owner)
{
    (void)owner;
}
