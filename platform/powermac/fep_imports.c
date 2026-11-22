/* Resident providers for the shared FEP private ABI. */
#include "fep_imports.h"
#include "input_service.h"
#include "kernel_tick.h"
#include "kboot_log.h"
#include "kstring.h"
#include <stdarg.h>

int kbd_getkey_wait(int interruptible) { return input_getkey_wait(interruptible); }
int kbd_trygetchar(void) { return input_trygetchar(); }
int kbd_trygetkey(void) { return input_trygetkey(); }
u32 ktime_ticks(void) { return tick_count; }
void serial_puts(const char *s) { pm_log_write(s, kstrlen(s)); }

/* FEP only formats integers and strings. Keep va_list on the soft-float side. */
char *os32_sqlite_snprintf(int size, char *out, const char *format, ...)
{
    if (size <= 0) return out;
    char *dst = out, *end = out + size - 1;
    va_list args;
    va_start(args, format);
    while (*format && dst < end) {
        if (*format != '%') {
            *dst++ = *format++;
            continue;
        }
        ++format;
        if (*format == 's') {
            const char *s = va_arg(args, const char *);
            while (s && *s && dst < end) *dst++ = *s++;
        } else if (*format == 'u' || *format == 'd') {
            u32 value;
            if (*format == 'd') {
                int number = va_arg(args, int);
                value = number < 0 ? 0u - (u32)number : (u32)number;
                if (number < 0) *dst++ = '-';
            } else {
                value = va_arg(args, unsigned);
            }
            char digits[10];
            unsigned count = 0;
            do {
                digits[count++] = '0' + value % 10;
                value /= 10;
            } while (value);
            while (count && dst < end) *dst++ = digits[--count];
        } else if (*format == '%') {
            *dst++ = '%';
        } else {
            break;
        }
        ++format;
    }
    va_end(args);
    *dst = 0;
    return out;
}
