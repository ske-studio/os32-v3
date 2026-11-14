/* PowerMac link boundary: DEC drives link; foreground only yields. */
#include "net/link_platform.h"
#include "net/link.h"
#include "kernel_tick.h"
#include "kernel/idle_work.h"
#include "kernel/irq/tick.h"
#include "arch_io.h"

/* Same position as PC98_TICK_LINK; USB occupies slot zero on PowerMac.
 * tick_dispatch owns the shared budget; link_tick keeps its bounded TX loop. */
enum { PM_TICK_LINK = 4 };
static int idle_running;
static int tick_registered;

u32 link_platform_now(void) { return tick_count; }
u32 link_platform_lock(void) { return ppc_irq_save(); }
void link_platform_unlock(u32 saved) { ppc_irq_restore(saved); }

void link_platform_idle(void)
{
    if (idle_running) return;
    idle_running = 1;
    /* _idle() never returns on PPC. Use the existing lightweight wait,
     * enabling EE so DEC can advance the common tick while we wait. */
    _enable();
    idle_work_run();
    _halt();
    idle_running = 0;
}

/* Foreground boot wiring is supplied by P1g. Retry a failed registration. */
int pm_link_tick_register(void)
{
    int rc;
    if (tick_registered) return 0;
    rc = tick_register(PM_TICK_LINK, link_tick);
    if (!rc) tick_registered = 1;
    return rc;
}
