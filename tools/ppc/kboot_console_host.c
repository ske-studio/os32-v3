/* ILP32 freestanding fixture. Only ESCC MMIO and process exit are host code. */
#include "console.h"
#include "kprintf.h"
#include "kboot_log.h"
#include "ofw.h"

static char output[4096];
static unsigned used, queued_used, calls;
static int queue_full;
static void finish(int status)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status));
    __builtin_unreachable();
}
static void reached(const char *text, unsigned size)
{
    int written;
    __asm__ volatile("int $0x80" : "=a"(written)
                     : "a"(4), "b"(1), "c"(text), "d"(size) : "memory");
    if (written != (int)size) finish(91);
}
#define REACHED(s) reached(s "\n", sizeof(s))
#define REQUIRE(x) do { if (!(x)) finish(90); } while (0)
unsigned ppc_load8(cell address)
{
    REQUIRE(address == 0xfe300000);
    return PM_RR0_TX_READY;
}
void ppc_store8(cell address, unsigned value)
{
    REQUIRE(address == 0xfe300010 && used < sizeof(output));
    output[used++] = (char)value;
}
static int queued(void *ctx, const char *bytes, u32 n)
{
    REQUIRE(ctx == &calls && bytes && n);
    /* The serial sink must finish this exact chunk before the queue sink. */
    REQUIRE(used == queued_used + n);
    for (u32 i = 0; i < n; i++) REQUIRE(output[queued_used + i] == bytes[i]);
    queued_used = used;
    calls++;
    return queue_full ? -1 : 0;
}
static const struct pm_log_sink queue = { queued, &calls };
static void expect(const char *text, unsigned n, int verdict)
{
    if (used != n) finish(verdict);
    for (unsigned i = 0; i < n; i++)
        if (output[i] != text[i]) finish(verdict);
    used = queued_used = 0;
}
#define EXPECT(s, rc) expect(s, sizeof(s) - 1, rc)
extern void console_text_gdc_stop(void);
extern void console_text_gdc_start(void);
void _start(void)
{
    int w = 80, h = 25;
    struct pm_log_stats stats;
    REQUIRE(!pm_log_escc_init(0xfe300000, 0xfe300010));
    REQUIRE(!pm_log_add_sink(&queue));
    /* Embedded NUL, explicit length, and trailing sentinel. */
    console_write("A\0BC!", 4, 0xff);
    REACHED("REACHED write-length");
    EXPECT("A\0BC", 41);
    shell_print("end", 0);
    shell_print("", 0xff);
    REACHED("REACHED string-end");
    EXPECT("end", 42);
    shell_print("L\n\r\n", 1);
    REACHED("REACHED lf-expansion");
    EXPECT("L\n\r\n", 43);
    console_write("ignored", 0, 0);
    shell_putchar('\0', 0);
    shell_putchar('!', 0xff);
    shell_print_utf8("\xe6\x97\xa5\xff", 7);
    EXPECT("\0!\xe6\x97\xa5\xff", 45);
    shell_print_dec(4294967295UL, 7);
    shell_print_hex32(0x0123abcd, 7);
    EXPECT("42949672950x0123ABCD", 45);
    /* Native va_list, enough arguments to exceed PPC's argument registers. */
    kprintf(7, "%d %u %x %s %c %%|%5d|%04u|%-4s|%d %u %x %d\n",
            -123, 4294967295UL, 0xabcdefUL, "str", (u32)'Q',
            42UL, 7UL, "xy", (u32)-2147483647 - 1, 0UL, 15UL, (u32)-9);
    EXPECT("-123 4294967295 abcdef str Q %|   42|0007|xy  |-2147483648 0 f -9\n", 45);
    console_set_cursor(3, 4);
    console_hw_cursor_enable();
    console_text_gdc_stop();
    console_text_gdc_start();
    console_get_size(&w, &h);
    REACHED("REACHED screen-success");
    if (console_get_cursor_x() != -1 || console_get_cursor_y() != -1 || w || h)
        finish(44);
    console_get_size(0, 0);
    w = 1; h = 1;
    console_get_size(&w, 0);
    console_get_size(0, &h);
    if (w || h || used) finish(44);
    REQUIRE(calls > 2);
    queue_full = 1;
    shell_print("drop", 7);
    EXPECT("drop", 45);
    REQUIRE(!pm_log_get_stats(1, &stats) && stats.failures == 1 && stats.dropped == 4);
    REQUIRE(!pm_log_get_stats(0, &stats) && !stats.failures && !stats.dropped);
    finish(0);
}
