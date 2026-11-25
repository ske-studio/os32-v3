#include "idle_work.h"
#include "install_access.h"
#include "io.h"
#include "io_work.h"
#include "irq.h"
#include "sys/net/nic.h"
#include "os32_kapi_shared.h"

#define IDLE_WORK_LIMIT 4

static void (*work[IDLE_WORK_LIMIT])(void);
static unsigned int work_count;

int idle_work_register(void (*fn)(void))
{
    unsigned int i;

    if (!fn) return OS32_ERR_INVAL;
    for (i = 0; i < work_count; ++i)
        if (work[i] == fn) return OS32_ERR_EXIST;
    if (work_count < IDLE_WORK_LIMIT) {
        work[work_count++] = fn;
        return 0;
    }
    return OS32_ERR_FULL;
}

/* Budget-unaware compatibility callbacks: always run once per dispatch. */
unsigned int idle_work_compat_count(void) { return work_count; }
void idle_work_compat_run(void)
{
    for (unsigned int i = 0; i < work_count; ++i)
        work[i]();
}

void idle_work_run(void)
{
    if (!_irq_enabled()) return;
    if (kctx_irq_depth != 0) return;
    if (platform_install_medium()) install_retry_recovery();
    (void)io_dispatch(IO_PASS_US);
}

void idle_wait_step(void)
{
    if (!_irq_enabled() || kctx_irq_depth != 0) return;
    idle_work_run();
    if (!net_rx_recent(NET_RX_ACTIVE_MS)) _idle();
}
