/* Terminal output is distinct from the diagnostic log sinks. */
#include "kernel/console/console_stream.h"
#include "lan_boot.h"
#include "ofw.h"
#include "arch/ppc/kernel_boot.h"

void console_stream_write(const void *bytes, u32 len)
{
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    struct pm_escc serial = { dev->escc_csr_va, dev->escc_data_va, 0 };
    pm_term_write(bytes, len);
    (void)pm_escc_write(&serial, bytes, len);
}

#include "kboot_log.h"
#include "console_output.h"
#include "console.h"
#include "kstring.h"
extern int boot_serial_mirror;
static int stream_putchar(char ch)
{
    console_stream_write(&ch, 1);
    return 0;
}
static const ConOutputOps stream_output = { .putchar = stream_putchar };
void pm_console_stream_init(void)
{
    con_output_register(&stream_output);
    boot_serial_mirror = 1;
}
void pm_fd1_write(const char *buf, u32 size, u8 color)
{
    console_write(buf, size, color);
}
void pm_cli_write(const void *buf, u32 size)
{
    console_write(buf, size, 1);
}
static void emergency_puts(const char *s) { pm_log_write(s, kstrlen(s)); }
static int emergency_gate(void) { return 0; }
const EmergencyOps emergency_output = { emergency_puts, emergency_gate };
