#include "snd_wake_tick.h"
#include "tick_clock.h"
#include "os32_kapi_shared.h"

static volatile u32 due_ms;
static volatile u8 armed;
static void (*wake_notify)(void);

static void wake_fast(u32 now)
{
    if (armed && (i32)(now - due_ms) >= 0) {
        armed = 0;
        wake_notify();
    }
}

int snd_wake_register(void (*notify)(void))
{
    int rc;
    if (!notify)
        return OS32_ERR_INVAL;
    rc = tick_fast_set(wake_fast);
    if (rc == 0)
        wake_notify = notify;
    return rc;
}

int snd_wake_arm(u32 after_us)
{
    if (!after_us) {
        armed = 0;
    } else {
        due_ms = tick_clock_ms() + after_us / 1000u + (after_us % 1000u != 0);
        armed = 1;
    }
    return 0;
}
