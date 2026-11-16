/* PowerMac link boundary: DEC and FG1 idle_work_run points drive the shared NIC/link
 * (waits, syscall entry, after net_tx; foreground link progress at most once/ms, RX-active poll every pass). */
#include "net/link_platform.h"
#include "kernel_tick.h"
#include "kernel/idle_work.h"
#include "kernel/irq/tick.h"
#include "arch_io.h"

/* Same position as PC98_TICK_LINK; USB occupies slot zero on PowerMac.
 * tick_dispatch owns the shared budget; link_tick keeps its bounded TX loop. */
enum { PM_TICK_LINK = 4 };
static int idle_running;

/* tick_clock_config が DEC を論理 100Hz の jiffy (10ms) に変換する。 */
u32 link_platform_now(void) { return tick_count; }
u32 link_platform_lock(void) { return ppc_irq_save(); }
void link_platform_unlock(u32 saved) { ppc_irq_restore(saved); }

void link_platform_idle(void)
{
    if (idle_running) return;
    idle_running = 1;
    idle_wait_step();
    idle_running = 0;
}

/* The NIC owns registration lifetime and supplies net_jiffy at bind. */
int link_platform_tick_register(void (*fn)(void))
{
    return tick_register(PM_TICK_LINK, fn);
}

#if KBOOT_SH
#include "platform/powermac/link_platform.h"
#include "usb_boot.h"


void pm_link_service_once(void)
{
    static int running;

    if (running) return;
    running = 1;
    pm_usb_poll();
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    pm_lan_poll(((u64)hi << 32) | lo);
    running = 0;
}
#endif
