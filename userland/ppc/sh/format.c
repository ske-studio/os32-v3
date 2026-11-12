#include "runtime.h"

struct sink { sh_put_fn put; void *ctx; size_t count; };

static void emit(struct sink *k, char ch)
{
    k->put(k->ctx, k->count, ch);
    ++k->count;
}

/* Stream every formatted byte to put (no length limit, like the kernel kprintf). */
int sh_vformat(sh_put_fn put, void *ctx, const char *fmt, va_list args)
{
    struct sink sink = { put, ctx, 0 };
    struct sink *k = &sink;
    while (*fmt) {
        unsigned int value, base, width = 0, n = 0;
        char digits[32];
        int negative = 0;
        char spec;
        if (*fmt != '%') {
            emit(k, *fmt++);
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
            while (*s) emit(k, *s++);
            continue;
        }
        if (spec == 'c' || spec == '%') {
            emit(k, spec == 'c' ? (char)va_arg(args, int) : '%');
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
        if (negative) emit(k, '-');
        while (n + (unsigned int)negative < width) {
            emit(k, '0');
            --width;
        }
        while (n) emit(k, digits[--n]);
    }
    return (int)k->count;
}

struct buffer { char *out; size_t size; };

static void put_buffer(void *ctx, size_t index, char ch)
{
    struct buffer *b = ctx;
    if (index + 1 < b->size) b->out[index] = ch;
}

int sh_vsnprintf(char *out, size_t size, const char *fmt, va_list args)
{
    struct buffer b = { out, size };
    int count = sh_vformat(put_buffer, &b, fmt, args);
    if (size) out[(size_t)count < size ? (size_t)count : size - 1] = '\0';
    return count;
}
