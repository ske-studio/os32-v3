/* Linux i386 host support, also used by qemu-i386. No libc headers or CRT. */
#ifndef TOOLS_TESTS_GF_COMMAND_RUNTIME_HOST_H
#define TOOLS_TESTS_GF_COMMAND_RUNTIME_HOST_H

static inline unsigned gf2_host_strlen(const char *s)
{
    unsigned n = 0;
    while (s[n]) ++n;
    return n;
}
static inline void __attribute__((noreturn)) gf2_host_exit(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory", "cc");
    __builtin_unreachable();
}
static inline void __attribute__((noreturn)) gf2_host_fail(const char *message)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc)
                     : "0"(4), "b"(2), "c"(message), "d"(gf2_host_strlen(message))
                     : "memory", "cc");
    (void)rc;
    gf2_host_exit(1);
}
#define GF2_HOST_STRING_(x) #x
#define GF2_HOST_STRING(x) GF2_HOST_STRING_(x)
/* Keep void assertion-only helpers observable to IPA even with a local
 * noreturn failure path; otherwise GCC can discard event comparisons.
 * Expressions still evaluate once, including fixture side effects. */
#define assert(x) do { __asm__ volatile("" : : : "memory"); if (!(x)) gf2_host_fail( \
    __FILE__ ":" GF2_HOST_STRING(__LINE__) ": " #x "\n"); } while (0)

static inline void *gf2_host_memset(void *dst, int value, unsigned n)
{
    unsigned char *d = dst;
    for (unsigned i = 0; i < n; ++i) d[i] = (unsigned char)value;
    return dst;
}
static inline void *gf2_host_memcpy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (unsigned i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
static inline int gf2_host_memcmp(const void *left, const void *right, unsigned n)
{
    const unsigned char *a = left, *b = right;
    for (unsigned i = 0; i < n; ++i)
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}
static inline const char *gf2_host_strstr(const char *s, const char *needle)
{
    for (;; ++s) {
        unsigned i = 0;
        while (needle[i] && s[i] && s[i] == needle[i]) ++i;
        if (!needle[i]) return s;
        if (!*s) return 0;
    }
}
#endif
