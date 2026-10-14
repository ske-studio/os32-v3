/* Real side-effect-free init, and every S1 import replaced with a counter. */
#include "kernel/module.h"
#include "kdebug_imports.h"
#include "kprintf.h"
static unsigned calls;
void kprintf(unsigned char color, const char *format, ...)
{
    (void)color; (void)format;
    calls++;
}
void crt_diag_bootlog(void) { calls++; }
extern const struct kdebug_ops kdebug_ops;

void _start(void)
{
    struct kdebug_ops candidate;
    for (u32 i = 0; i < sizeof(candidate); i++) ((u8 *)&candidate)[i] = 0xa5;
    int rc = kdebug_init(&candidate);
    const u8 *a = (const u8 *)&candidate, *b = (const u8 *)&kdebug_ops;
    for (u32 i = 0; i < sizeof(candidate); i++) if (a[i] != b[i]) rc = 1;
    if (calls || candidate.version != KDEBUG_OPS_VERSION ||
        candidate.crt_diag_bootlog != crt_diag_bootlog || candidate.selftest_run ||
        candidate.gcap_release) rc = 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
