#include "console.h"
#include "console_output.h"
#include "port_service.h"

static void kernel_boot_serial(void)
{
    const char *log;
    u32 len, i;

    if (sysconfig_get_int(SYS_SYSTEM_CFG, "BOOTSERIAL", 0) != 1) return;
    port_init(SYS_SERIAL_BAUD);
    log = bootlog_compose(0, &len);
    for (i = 0; i < len; i++) con_output_putchar(log[i]);
    boot_serial_mirror = 1;
}
