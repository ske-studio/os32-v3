/* Kernel diagnostics go to the registered ESCC and LAN log sinks. */
#include "console.h"
#include "console_log.h"
#include "kboot_log.h"
#include "kstring.h"
void pm_diag_write(const char *bytes, u32 n, u8 color)
{
    (void)color;
    pm_log_write(bytes, n);
}
void pm_diag_print(const char *s, u8 color)
{
    pm_diag_write(s, kstrlen(s), color);
}
void pm_diag_print_utf8(const char *s, u8 color)
{
    pm_diag_print(s, color);
}
