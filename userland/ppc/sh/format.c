#include "runtime.h"

static void emit(char *out, size_t size, size_t *count, char ch)
{
    if (*count + 1 < size)
        out[*count] = ch;
    ++*count;
}

int sh_vsnprintf(char *out, size_t size, const char *fmt, va_list args)
{
    size_t count = 0;
    while (*fmt) {
        unsigned int value, base, width = 0, n = 0;
        char digits[32];
        int negative = 0;
        char spec;
        if (*fmt != '%') {
            emit(out, size, &count, *fmt++);
            continue;
        }
        ++fmt;
        if (*fmt == '0') {
            ++fmt;
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (unsigned int)(*fmt++ - '0');
        }
        spec = *fmt ? *fmt++ : '\0';
        if (spec == 's') {
            const char *s = va_arg(args, const char *);
            while (*s) emit(out, size, &count, *s++);
            continue;
        }
        if (spec == 'c' || spec == '%') {
            emit(out, size, &count, spec == 'c' ? (char)va_arg(args, int) : '%');
            continue;
        }
        if (spec != 'u' && spec != 'd' && spec != 'x') continue;
        if (spec == 'd') {
            int signed_value = va_arg(args, int);
            negative = signed_value < 0;
            value = negative ? 0u - (unsigned int)signed_value : (unsigned int)signed_value;
        } else value = va_arg(args, unsigned int);
        base = spec == 'x' ? 16u : 10u;
        do {
            digits[n++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value);
        if (negative) emit(out, size, &count, '-');
        while (n + (unsigned int)negative < width) {
            emit(out, size, &count, '0');
            --width;
        }
        while (n) emit(out, size, &count, digits[--n]);
    }
    if (size) out[count < size ? count : size - 1] = '\0';
    return (int)count;
}
