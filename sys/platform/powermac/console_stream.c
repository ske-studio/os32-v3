/* Terminal output is distinct from the diagnostic log sinks. */
#include "sys/kern/console/console_stream.h"
#include "lan_boot.h"
#include "ofw.h"
#include "sys/arch/ppc/kernel_boot.h"

#if !KBOOT_LINK
void console_stream_write(const void *bytes, u32 len)
{
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    struct pm_escc serial = { dev->escc_csr_va, dev->escc_data_va, 0 };
    pm_term_write(bytes, len);
    pm_terminal_poll();
    (void)pm_escc_write(&serial, bytes, len);
}
#endif

#include "kboot_log.h"
#include "platform_console.h"
#include "console_output.h"
#include "console.h"
#include "text_screen.h"
#include "kstring.h"
extern int boot_serial_mirror;
#if !KBOOT_LINK
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
#else
/* The common console has already captured PRINT before invoking a mirror. */
static int stream_putchar(char ch) { (void)ch; return 0; }
static const ConOutputOps stream_output = { .putchar = stream_putchar };
void pm_console_stream_init(void)
{
    con_output_register(&stream_output);
    boot_serial_mirror = 0;
}
void console_stream_write(const void *bytes, u32 len)
{
    console_write(bytes, len, 1);
}
#endif
void pm_fd1_write(const char *buf, u32 size, u8 color)
{
    console_write(buf, size, color);
}
static void emergency_puts(const char *s) { pm_log_write(s, kstrlen(s)); }
static int emergency_gate(void) { return 0; }
const EmergencyOps emergency_output = { emergency_puts, emergency_gate };

/* KAPI clear: local grid and terminal control bytes have separate routes. */
void pm_console_clear(void)
{
    tvram_clear();
#if !KBOOT_LINK
    console_stream_write("\033[2J\033[H", 7);
#endif
}
