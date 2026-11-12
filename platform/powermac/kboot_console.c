/* Temporary kboot stream provider (PPC_KBOOT_DESIGN section 6, G2).
 * H3 must select this TU instead of the kernel/console TUs, never both.
 * Normal PC98 output is byte-preserving: platform/pc98/d2_bind.c:54 ->
 * drivers/char/serial.c:701,875. In particular LF is not expanded here.
 * UTF-8 and attributes have no display semantics on this stream.
 */
#include "console.h"
#include "kprintf.h"
#include "kstring.h"
#include "kboot_log.h"
#if KBOOT_SHELL || KBOOT_SH
#include "kernel/console/console_stream.h"

void pm_fd1_write(const char *buf, u32 size, u8 color)
{
    (void)color;
    console_stream_write(buf, size);
}
#endif

int rshell_active;

/* lib/kprintf_attr.c selects the display backend. H3 excludes that TU too:
 * the stream has no attribute conversion and needs no con_backend provider. */
u8 kprintf_attr_to_pc98(u8 attr) { return attr; }

void console_write(const char *buf, u32 size, u8 color)
{
    (void)color;
    pm_log_write(buf, size);
}

void shell_print(const char *str, u8 color)
{
    console_write(str, kstrlen(str), color);
}

void shell_print_utf8(const char *str, u8 color)
{
    shell_print(str, color);
}

void shell_print_dec(u32 val, u8 color)
{
    char buf[U32_DEC_BUFSIZE + 1];
    kutoa_dec(val, buf, sizeof(buf));
    shell_print(buf, color);
}

void shell_print_hex32(u32 val, u8 color)
{
    char buf[HEX32_DIGITS + sizeof("0x")];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = HEX32_DIGITS + 1; i >= 2; i--) {
        buf[i] = "0123456789ABCDEF"[val & 15];
        val >>= HEX_NIBBLE_BITS;
    }
    buf[HEX32_DIGITS + 2] = '\0';
    shell_print(buf, color);
}

/* No screen capability: -1 is an unavailable coordinate, 0x0 an absent
 * screen. Void screen controls cannot report status; they do nothing and
 * never change these queries into a claim that a screen exists. */
int console_get_cursor_x(void) { return -1; }
int console_get_cursor_y(void) { return -1; }
void console_get_size(int *w, int *h)
{
    if (w) *w = 0;
    if (h) *h = 0;
}
void console_set_cursor(int x, int y) { (void)x; (void)y; }
void console_hw_cursor_enable(void) { }
/* init/kernel.c also declares these void screen controls locally. */
void console_text_gdc_stop(void) { }
void console_text_gdc_start(void) { }
