#include "snd_writer_probe.h"
#include "tsc_clock.h"

static u32 now;
static int failed;
int pc98_tsc_read_us(u32 *lo, u32 *hi)
{
    *lo = now;
    *hi = 0;
    return failed ? -1 : 0;
}

int main(void)
{
    unsigned long out[SW_PAIRS + 1] = {0};
    sw_reset();
    sw_sleep();
    now = ~0u - 80;
    sw_irq(10);
    sw_irq(0);
    now += 50;
    sw_ready();
    now += 10000;
    sw_point(SW_GFX);
    now += 2;
    sw_point(SW_IO_END);
    sw_exit();
    now += 20;
    sw_write();
    sw_ready();
    now += 8;
    sw_point(SW_DISPLAY);
    sw_exit();
    now += 9970;
    sw_point(SW_IDLE);
    now += 10;
    sw_write();
    sw_snapshot(out);
    if (out[SW_WAIT_EXIT] != 10002 || out[SW_EXIT_WRITE] != 9980 ||
        out[SW_TOTAL] != 10022 || out[SW_WORK] != (SW_GFX | (SW_GFX << SW_WAIT_SHIFT) | (SW_IDLE << SW_WRITE_SHIFT)) ||
        out[SW_WORK_US] != 10000 || out[SW_JUMPS] != 1 ||
        out[SW_PAIRS] != 2 || out[SW_WAKE_IRQ] != 10 || out[SW_WAKE_US] != 50)
        return 1;
    sw_ready();
    now += 10001;
    sw_point(SW_OTHER);
    sw_cancel();
    sw_write();
    sw_ready();
    failed = 1;
    sw_point(SW_OTHER);
    sw_exit();
    sw_write();
    sw_snapshot(out);
    return out[SW_PAIRS] != 2 || out[SW_JUMPS] != 1;
}
