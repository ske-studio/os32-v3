/* ILP32 execution of the exact guest helpers, with controlled resume damage. */
#include "h2_stack_probe.h"
#include "h2_stack_probe.inc"
static int calls, damage;
static unsigned int array_bytes;
static int resumed(struct h2_probe *p, volatile unsigned char *a, unsigned int n)
{
    (void)n;
    calls++;
    if (damage == 1) a[0] ^= 1;
    if (damage == 2) p->argc = 1;
    if (damage == 3) ((volatile unsigned char *)(p->high - array_bytes))[0] ^= 1;
    return 1;
}
static int run(void)
{
    char *args[] = {"h2_stack512", "park", "h2-arg", 0};
    struct h2_probe p;
    unsigned int d, variant, bytes, depth, stack;
    unsigned long guard;
    if (!h2_entry_ok(3) || h2_entry_ok(2) || h2_entry_ok(4)) return 6;
    for (variant = 0; variant < 2; variant++) {
        stack = variant ? 524288U : 262144U;
        h2_plan(stack, &bytes, &depth, &guard);
        if (bytes != (variant ? 294912U : 98304U) ||
            depth != (variant ? 7U : 3U) ||
            (bytes > 262144U) != (variant != 0) ||
            guard != MEM_APP_STACK_TOP - stack - 4096U) return 7;
        array_bytes = bytes;
        p.argc = 3; p.argv = args; p.wait = resumed; p.context = 0;
        damage = 0; calls = 0;
        if (!h2_probe_run(&p, bytes, depth) || calls != 1 ||
            p.pages != (variant ? 104U : 40U) ||
            p.high - p.low < bytes + (depth + 1) * H2_DEEP_BYTES ||
            (!variant && p.high - p.low >= 262144U)) return 1;
        for (d = 1; d <= 3; d++) {
            p.argc = 3; calls = 0; damage = d;
            if (h2_probe_run(&p, bytes, depth) || calls != 1) return 2;
        }
        damage = 0; p.argc = 2; calls = 0;
        if (h2_probe_run(&p, bytes, depth) || calls) return 3;
        p.argc = 3; args[2] = "h2-bad"; calls = 0;
        if (h2_probe_run(&p, bytes, depth) || calls) return 4;
        args[2] = "h2-arg";
    }
    return 0;
}
void _start(void)
{
    int rc = run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
