#include "lan_boot.h"
#if KBOOT_SH
#include "usb_boot.h"
#include "sys/kern/idle_work.h"
#endif
#ifdef PM_TERMINAL_TEST
extern u64 pm_terminal_tb(void);
#else
static u64 pm_terminal_tb(void)
{
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((u64)hi << 32) | lo;
}
#endif

void pm_terminal_poll(void)
{
#if KBOOT_SH
    pm_usb_poll();
#endif
    pm_lan_poll(pm_terminal_tb());
}


#if KBOOT_SH
/* FG1 work is independent of terminal reads; direct legacy polls remain. */
static void terminal_service_once(void)
{
    static int running;

    if (running) return;
    running = 1;
    pm_terminal_poll();
    running = 0;
}

int pm_terminal_work_register(void)
{
    return idle_work_register(terminal_service_once);
}
#endif
