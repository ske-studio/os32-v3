/* Small output capture for the freestanding start-failure hosts. */
#ifndef GF_STARTLOG_HOST_H
#define GF_STARTLOG_HOST_H
#include "kprintf.h"
static char startlog[1024];
static unsigned startlog_len;
static void startlog_char(char c)
{
    if (startlog_len + 1 >= sizeof(startlog)) __builtin_trap();
    startlog[startlog_len++] = c;
    startlog[startlog_len] = 0;
}
static void startlog_uint(unsigned v)
{
    if (v >= 10) startlog_uint(v / 10);
    startlog_char('0' + v % 10);
}
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    __builtin_va_list args;
    (void)attr;
    __builtin_va_start(args, fmt);
    while (*fmt) {
        if (*fmt++ != '%') { startlog_char(fmt[-1]); continue; }
        char spec = *fmt++;
        if (spec == 's') {
            const char *s = __builtin_va_arg(args, const char *);
            while (*s) startlog_char(*s++);
        } else if (spec == 'd') {
            int v = __builtin_va_arg(args, int);
            if (v < 0) startlog_char('-');
            startlog_uint(v < 0 ? 0U - (unsigned)v : (unsigned)v);
        } else if (spec == 'u') startlog_uint(__builtin_va_arg(args, unsigned));
        else __builtin_trap();
    }
    __builtin_va_end(args);
}
static void startlog_clear(void) { startlog_len = 0; startlog[0] = 0; }
static int startlog_equal(const char *s)
{
    unsigned i = 0;
    while (startlog[i] && startlog[i] == s[i]) i++;
    return startlog[i] == s[i];
}
#endif
