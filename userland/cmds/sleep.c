/* ======================================================================== */
/*  SLEEP.C -- 指定秒数のウェイト                                             */
/*                                                                          */
/*  Usage: sleep SECONDS                                                     */
/*  PIT 100Hz ティックを使用して指定秒数待機する。                              */
/*  スクリプトエンジンとの連携で使用する。                                     */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>

/* PIT は 100Hz (1ティック = 10ms) */
#define TICKS_PER_SEC 100

int main(int argc, char **argv, KernelAPI *api)
{
    unsigned long seconds;
    unsigned long wait_ticks;
    u32 start;

    if (argc < 2) {
        printf("Usage: sleep SECONDS\n");
        return 1;
    }

    /* 引数を整数に変換 */
    seconds = 0;
    {
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9') {
            seconds = seconds * 10 + (*p - '0');
            p++;
        }
    }

    if (seconds == 0) {
        return 0;
    }

    wait_ticks = seconds * TICKS_PER_SEC;
    start = api->get_tick();

    /* sys_halt で次の割り込みまでCPUをスリープさせつつ待機 */
    while (api->get_tick() - start < wait_ticks) {
        api->sys_halt();
    }

    return 0;
}
