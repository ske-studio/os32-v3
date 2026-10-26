#ifndef OS32_SERIAL_DIAG_H
#define OS32_SERIAL_DIAG_H

/* These names are the common emergency facade, available before registration.
 * Cooperative-context diagnostics: callers check the serial protocol gate
 * once before starting a line. Keep formatting away from console sinks. */
extern void serial_puts_polled(const char *str);
extern int serial_gate_active(void);

static void __attribute__((cold, unused)) serial_diag_u32(unsigned int n)
{
    char buf[11], *p = buf + sizeof(buf) - 1;
    *p = 0;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    serial_puts_polled(p);
}

static void __attribute__((cold, unused)) serial_diag_i32(int n)
{
    if (n < 0) serial_puts_polled("-");
    serial_diag_u32(n < 0 ? 0U - (unsigned int)n : (unsigned int)n);
}

static void __attribute__((cold, unused)) serial_diag_hex(unsigned int n, unsigned int digits)
{
    char buf[9];
    if (digits > 8) digits = 8;
    buf[digits] = 0;
    while (digits) {
        buf[--digits] = "0123456789abcdef"[n & 15U];
        n >>= 4;
    }
    serial_puts_polled(buf);
}
#endif
