#include "idle_work.h"
#include "io.h"
#include "irq.h"
#include "os32_kapi_shared.h"

#define IDLE_WORK_LIMIT 4

static void (*work[IDLE_WORK_LIMIT])(void);
static unsigned int work_count;
static int running;

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

void idle_work_run(void)
{
    unsigned int i;

    if (!_irq_enabled()) return;
    if (kctx_irq_depth != 0) return;
    if (running) return;
    running = 1;
    for (i = 0; i < work_count; ++i)
        work[i]();
    running = 0;
}
