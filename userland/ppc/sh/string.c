#include "string.h"

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { ++a; ++b; --n; }
    return n ? (int)(unsigned char)*a - (int)(unsigned char)*b : 0;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *p = dst;
    while (n && *src) { *p++ = *src++; --n; }
    while (n) { *p++ = '\0'; --n; }
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *p = dst;
    while (*p) ++p;
    while ((*p++ = *src++) != '\0') { }
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *p = dst;
    while (*p) ++p;
    while (n && *src) { *p++ = *src++; --n; }
    *p = '\0';
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d > s) {
        while (n) { --n; d[n] = s[n]; }
    } else {
        while (n--) *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
