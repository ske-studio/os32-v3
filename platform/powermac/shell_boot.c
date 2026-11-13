#include "shell_boot.h"
#include "console.h"
#include "display_text.h"
#include "input_usb.h"
#include "lan_boot.h"
#include "exec.h"
#include "kprintf.h"
#include "kernel/console/console_stream.h"

static void report(int kind, int code)
{
    char digits[12];
    const char *label = kind == EXEC_KIND_EXITED ? "sh exited=" : "sh failed kind=";
    u32 value = kind == EXEC_KIND_EXITED ? (u32)code : (u32)kind;
    int n = kutoa_dec(value, digits, sizeof(digits));
    const char *p = label;
    while (*p) ++p;
    kprintf(0x07, "sh result kind=%d code=%d\n", kind, code);
    console_stream_write(label, (u32)(p - label));
    if (kind == EXEC_KIND_EXITED && code < 0) {
        value = 0u - value;
        n = kutoa_dec(value, digits, sizeof(digits));
        console_stream_write("-", 1);
    }
    console_stream_write(digits, (u32)n);
    console_stream_write("\n", 1);
}

void pm_shell_boot(void)
{
    int kind, code;
    if (pm_console_text_ready()) shell_print("Press a USB key or connect LAN to start sh\n", 1);
    while (!pm_term_connected() && !pm_input_usb_has_key()) pm_terminal_poll();
    (void)pm_terminal_trygetkey(); /* Consume the first OPEN cancellation. */
    for (;;) {
        (void)exec_run("/bin/sh.bin");
        (void)exec_last_result(&kind, &code);
        report(kind, code);
        if (kind != EXEC_KIND_EXITED) break;
    }
    for (;;) pm_terminal_poll();
}
