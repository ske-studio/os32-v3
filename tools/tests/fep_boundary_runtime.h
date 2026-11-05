/* Minimal Linux ILP32 fixture runtime; no libc is linked. The RAM VFS and
 * SQLite MEMSYS5 remain the shared production-dictionary test fixture. */
#ifndef FEP_BOUNDARY_RUNTIME_H
#define FEP_BOUNDARY_RUNTIME_H
#include <stdarg.h>
#include <stddef.h>
#include "kstring.h"
#undef kstrlcpy
#define stderr 2
#define FEP_HOST_HEAP_BYTES (1024u * 1024u)
#define FEP_HOST_FORMAT_BYTES 1024u
static union { unsigned long align; char bytes[FEP_HOST_HEAP_BYTES]; } fixture_heap;
static unsigned fixture_used;
static void exit(int rc) __attribute__((noreturn));
static void exit(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
void *malloc(size_t n)
{
    if (n > FEP_HOST_HEAP_BYTES) return NULL;
    n = (n + sizeof(unsigned long) - 1) & ~(sizeof(unsigned long) - 1);
    if (n > FEP_HOST_HEAP_BYTES - fixture_used) return NULL;
    void *p = fixture_heap.bytes + fixture_used;
    fixture_used += n;
    return p;
}
void free(void *p) { (void)p; }
void *realloc(void *p, size_t n) { (void)p; (void)n; exit(2); }
static void *calloc(size_t n, size_t size)
{
    if (size && n > FEP_HOST_HEAP_BYTES / size) return NULL;
    void *p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}
static int fixture_format(char *out, size_t cap, const char *fmt, va_list args)
{
    unsigned n = 0;
#define EMIT(ch) do { char byte = (ch); if (n + 1 < cap) out[n] = byte; n++; } while (0)
    while (*fmt) {
        if (*fmt != '%') { EMIT(*fmt++); continue; }
        fmt++;
        int width = 0; char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == 's') {
            const char *s = va_arg(args, const char *);
            while (*s) { EMIT(*s++); }
        } else if (*fmt == 'd' || *fmt == 'u' || *fmt == 'x') {
            int value = va_arg(args, int);
            unsigned v = (unsigned)value, base = *fmt == 'x' ? 16 : 10;
            char digits[sizeof(unsigned) * 8]; int used = 0;
            if (*fmt == 'd' && value < 0) { EMIT('-'); v = 0u - v; }
            do { digits[used++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
            while (width-- > used) { EMIT(pad); }
            while (used) { EMIT(digits[--used]); }
        } else { EMIT(*fmt); }
        if (*fmt) fmt++;
    }
    if (cap) out[n < cap ? n : cap - 1] = 0;
#undef EMIT
    return n;
}
static int snprintf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); int n = fixture_format(out, cap, fmt, ap); va_end(ap); return n;
}
static int fixture_print(int fd, const char *fmt, va_list ap)
{
    char out[FEP_HOST_FORMAT_BYTES];
    int n = fixture_format(out, sizeof(out), fmt, ap);
    if (n >= (int)sizeof(out)) exit(2);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(fd), "c"(out), "d"(n) : "memory");
    return n;
}
static int fprintf(int fd, const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); int n = fixture_print(fd, fmt, ap); va_end(ap); return n; }
static int printf(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); int n = fixture_print(1, fmt, ap); va_end(ap); return n; }
static char *strcpy(char *dst, const char *src) { return kstrcpy(dst, src); }
static char *strstr(const char *s, const char *needle)
{
    unsigned n = strlen(needle);
    do { if (!strncmp(s, needle, n)) return (char *)s; } while (*s++);
    return NULL;
}
static int puts(const char *s) { return printf("%s\n", s); }
extern int main(void);
void _start(void) { exit(main()); }
#endif
