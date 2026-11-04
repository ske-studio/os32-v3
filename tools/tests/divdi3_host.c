/* Exercise the compiler ABI and operators against Python-generated answers. */
typedef unsigned long long u64;
typedef long long i64;
extern u64 __udivdi3(u64, u64);
extern u64 __umoddi3(u64, u64);
extern u64 __udivmoddi4(u64, u64, u64 *);
extern i64 __divdi3(i64, i64);
extern i64 __moddi3(i64, i64);
extern i64 __divmoddi4(i64, i64, i64 *);
#include "divdi3_vectors.h"

static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}

void _start(void)
{
    for (unsigned int i = 0; i < sizeof(unsigned_cases) / sizeof(unsigned_cases[0]); i++) {
        u64 n = unsigned_cases[i].n, d = unsigned_cases[i].d;
        u64 q = unsigned_cases[i].q, r = unsigned_cases[i].r, rest;
        if (__udivdi3(n, d) != q || __umoddi3(n, d) != r) finish(1);
        if (__udivmoddi4(n, d, &rest) != q || rest != r) finish(2);
        if (__udivmoddi4(n, d, 0) != q) finish(3);
        if (n / d != q || n % d != r) finish(4);
    }
    for (unsigned int i = 0; i < sizeof(signed_cases) / sizeof(signed_cases[0]); i++) {
        i64 n = (i64)signed_cases[i].n, d = (i64)signed_cases[i].d;
        i64 q = (i64)signed_cases[i].q, r = (i64)signed_cases[i].r, rest;
        if (__divdi3(n, d) != q || __moddi3(n, d) != r) finish(5);
        if (__divmoddi4(n, d, &rest) != q || rest != r) finish(6);
        if (__divmoddi4(n, d, 0) != q) finish(7);
        if (n / d != q || n % d != r) finish(8);
    }
    finish(0);
}
