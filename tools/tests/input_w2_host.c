#include "input_policy.h"
static IkmCheck check;
static int test(void)
{
    IkmRow r = {1, 0, 1, 0, 0, 0, 128, 0};
    ikm_check_init(&check);
    if (ikm_check_row(&check, 1, &r) || !ikm_check_row(&check, 1, &r)) return 1;
    ikm_check_init(&check); r.flags=1;
    if (!ikm_check_row(&check, 1, &r)) return 2;
    r.flags=0; if (!ikm_check_row(&check, 2, &r)) return 3;
    r.kind=2; r.context=2; r.index=255; r.selector=4; r.value=0xf500;
    if (ikm_check_row(&check, 2, &r)) return 4;
    r.index=256; if (!ikm_check_row(&check, 2, &r)) return 5;
    r.index=1; r.value=0x300; if (!ikm_check_row(&check, 2, &r)) return 6;
    r.kind=3; r.context=1; r.index=0; r.selector=15; r.value=0xf500; r.aux=0x8001;
    if (ikm_check_row(&check, 1, &r)) return 7;
    r.index=1; r.aux=0x8901; if (!ikm_check_row(&check, 1, &r)) return 8;
    r.kind=4; if (!ikm_check_row(&check, 1, &r)) return 9;
    r.kind=5; r.profile=1; r.context=0; r.index=1; r.selector=0; r.value=0; r.aux=0;
    if (ikm_check_row(&check, 1, &r)) return 10;
    r.kind=6; r.index=0; r.selector=0xffff; r.value=255;
    if (ikm_check_row(&check, 1, &r)) return 11;
    r.selector=0x10; r.value=18; if (ikm_check_row(&check, 1, &r)) return 12;
    r.selector=8; if (!ikm_check_row(&check, 1, &r)) return 13;
    r.kind=7; r.profile=0; r.context=1; r.index=127; r.selector=0; r.value=3;
    if (ikm_check_row(&check, 1, &r)) return 14;
    r.kind=8; r.profile=4; r.context=4; r.index=255; r.selector=2; r.value=18; r.aux=0x17;
    if (ikm_check_row(&check, 2, &r)) return 15;
    r.aux=0; if (!ikm_check_row(&check, 2, &r)) return 16;
    ikm_check_init(&check); r.kind=6; r.profile=1; r.context=1; r.selector=0; r.value=0; r.aux=0;
    for (r.index=0; r.index<128; r.index++) if (ikm_check_row(&check, 1, &r)) return 17;
    r.context=2; r.index=0; if (!ikm_check_row(&check, 1, &r)) return 18;
    ikm_check_init(&check);
    r.kind=2; r.profile=0; r.context=2; r.index=0; r.selector=0; r.aux=0;
    r.value=0xd800; if (!ikm_check_row(&check, 1, &r)) return 19;
    r.value=0xffff; if (!ikm_check_row(&check, 1, &r)) return 20;
    r.value=0x10000; if (!ikm_check_row(&check, 1, &r)) return 21;
    r.value=0; if (ikm_check_row(&check, 1, &r)) return 22;
    r.kind=1; r.context=2; r.index=1; r.value=1;
    if (!ikm_check_row(&check, 1, &r)) return 23;
    r.kind=8; r.profile=1; r.context=0; r.index=0; r.selector=2; r.value=19;
    if (!ikm_check_row(&check, 1, &r)) return 24;
    r.value=0; r.aux=8; if (!ikm_check_row(&check, 1, &r)) return 25;
    return 0;
}
void _start(void) {
    int r = test();
#ifdef PPC_TEST
    register int call __asm__("r0") = 1;
    register int status __asm__("r3") = r;
    __asm__ volatile("sc" : : "r"(call), "r"(status) : "memory");
#else
    __asm__ volatile("int $0x80" : : "a"(1), "b"(r));
#endif
    __builtin_unreachable();
}
