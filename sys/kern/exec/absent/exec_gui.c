/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_GUI 0
#include "exec_gui.h"
int exec_gui_registered(void) { return 0; }
ExecGuiPump exec_gui_pump(void) { return 0; }
void exec_gui_owner_exit(int owner, int kind) { (void)owner; (void)kind; }
