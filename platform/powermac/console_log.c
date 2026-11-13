/* Diagnostic formatter endpoints: preserve the log protocol, while drawing
 * through the common console without feeding the terminal mirror again. */
#include "console.h"
#include "kboot_log.h"
#include "kstring.h"
extern int boot_serial_mirror, rshell_active;
void pm_diag_write(const char *bytes, u32 n, u8 color)
{
    pm_log_write(bytes, n);
    int boot = boot_serial_mirror, remote = rshell_active;
    boot_serial_mirror = rshell_active = 0;
    console_write(bytes, n, color);
    boot_serial_mirror = boot;
    rshell_active = remote;
}
void pm_diag_print(const char *s, u8 color)
{
    pm_diag_write(s, kstrlen(s), color);
}
void pm_diag_print_utf8(const char *s, u8 color)
{
    pm_diag_print(s, color);
}
