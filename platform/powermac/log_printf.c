/* PowerMac kernel diagnostics use the registered log sinks. */
#include "kprintf.h"
#include <stdarg.h>
#include "kboot_log.h"
#include "kstring.h"

static void pm_diag_write(const char *bytes, u32 n, u8 color)
{
    (void)color;
    pm_log_write(bytes, n);
}
static void pm_diag_print(const char *s, u8 color)
{
    pm_diag_write(s, kstrlen(s), color);
}

#define KPRINTF_WRITE pm_diag_write
#define KPRINTF_PRINT pm_diag_print
#define KPRINTF_PRINT_UTF8 pm_diag_print
#define KPRINTF_VNAME kvprintf
#define KPRINTF_NAME kprintf
#include "lib/kprintf_format.inc"
