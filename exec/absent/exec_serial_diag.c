/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_SERIAL_DIAG 0
#include "exec_serial_diag.h"

void exec_serial_puts_polled(const char *s) { (void)s; }
