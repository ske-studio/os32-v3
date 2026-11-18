/* exec を持つ像のカーネルの診断の出力 (kprintf・kvprintf)。LAN のログへ出す (console_log.c)。
 * 端末に出す物は console_printf (lib/kprintf.c)。書式の処理は共通の雛形。 */
#include "kprintf.h"
#include <stdarg.h>
#include "console_log.h"

#define KPRINTF_WRITE pm_diag_write
#define KPRINTF_PRINT pm_diag_print
#define KPRINTF_PRINT_UTF8 pm_diag_print_utf8
#define KPRINTF_VNAME kvprintf
#define KPRINTF_NAME kprintf
#include "lib/kprintf_format.inc"
