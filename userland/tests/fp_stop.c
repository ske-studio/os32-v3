/* Manual x87 isolation fixture; check is also usable by guest_tests. */
#include "os32api.h"

#define DEFAULT_CW 0x037fu
#define ROUND_TRUNCATE 0x0c00u
#define ROUND_MASK 0x0c00u
#define EMPTY_TAG 0xffffu
#define TOP_MASK 0x3800u
#define REPORT_TICKS 300u
#define SQRT2_EXPECTED 1.4142135623730951
#define FP_TOLERANCE 0.000000000001

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int main(int argc, char **argv, KernelAPI *api)
{
    unsigned short cw, sw;
    /* 32-bit FNSTENV: control, status, tag are at offsets 0, 4, 8. */
    struct { unsigned int word[7]; } env;
    __asm__ volatile ("fnstcw %0; fnstsw %1; fnstenv %2; fldenv %2"
                      : "=m" (cw), "=m" (sw), "=m" (env) : : "memory");
    if (argc != 2) return 1;
    if (equal(argv[1], "loop")) {
        unsigned short trunc_cw = (cw & ~ROUND_MASK) | ROUND_TRUNCATE;
        volatile double x = 1.25;
        u32 last = api->get_tick(), count = 0;
        __asm__ volatile ("fldcw %0; fld1" : : "m" (trunc_cw));
        for (;;) {
            for (u32 i = 0; i < 10000; i++) x = (x + 2.0 / x) * 0.5;
            count++;
            if ((u32)(api->get_tick() - last) >= REPORT_TICKS) {
                api->kprintf(ATTR_CYAN, "fp_stop: loop %u\n", count);
                last = api->get_tick();
            }
        }
    }
    if (equal(argv[1], "check")) {
        unsigned int passed = 0;
        volatile double x = 1.0, two = 2.0;
        double diff;
        passed += cw == DEFAULT_CW;
        passed += (sw & TOP_MASK) == 0 && (env.word[2] & 0xffffu) == EMPTY_TAG;
        for (int i = 0; i < 20; i++) x = (x + two / x) * 0.5;
        diff = x - SQRT2_EXPECTED;
        if (diff < 0) diff = -diff;
        passed += diff < FP_TOLERANCE;
        api->kprintf(passed == 3 ? ATTR_GREEN : ATTR_RED,
                     "fp_stop: %s %u/3\n", passed == 3 ? "PASS" : "FAIL", passed);
        return passed == 3 ? 0 : 1;
    }
    return 1;
}
