/* CPL=3 の直接書込み試験: text / rodata は USER write #PF で kill、
 * data は PASS・終了値 0。負例は別々に起動し、SURVIVED が出たら不合格。
 * 引数なしでは usage だけを出す。main は最初の関数に置く。 */
#include "os32api.h"
#include <string.h>

static const u8 ro_byte = 0x52;
static u8 data_byte = 0x44;

int main(int argc, char **argv, KernelAPI *api)
{
    volatile u8 *target;
    int writable;

    if (argc != 2 || (strcmp(argv[1], "text") != 0 &&
                      strcmp(argv[1], "rodata") != 0 &&
                      strcmp(argv[1], "data") != 0)) {
        api->kprintf(0x07, "usage: text_ro_test <text|rodata|data>\n");
        return 1;
    }
    writable = strcmp(argv[1], "data") == 0;
    if (writable)
        target = &data_byte;
    else if (strcmp(argv[1], "text") == 0)
        target = (volatile u8 *)(void *)main;
    else
        target = (volatile u8 *)&ro_byte;

    api->kprintf(0x07, "text_ro_test: writing %s at %p (expect %s)\n",
                 argv[1], (void *)target, writable ? "PASS" : "#PF kill");
    *target = 0xA5;
    if (writable && *target == 0xA5) {
        api->kprintf(0x0A, "text_ro_test: data PASS\n");
        return 0;
    }
    api->kprintf(0x0C, "text_ro_test: FAIL SURVIVED %s\n", argv[1]);
    return 2;
}
