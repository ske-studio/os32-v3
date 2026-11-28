#include "av_contract.h"
#include "in2_int.h"
#include "input_work.h"
#include "io.h"

static void clear_input(void)
{
    i32 x, y;
    input_work_invalidate_locked();
    in2_ring_clear();
    mouse_ext_read(&x, &y);
    mouse_ext_read_wheel();
}

void in2_reclaim(int owner)
{
    unsigned f = irq_save();
    int foreground = owner == av_focus_owner();
    if (foreground) clear_input();
    in2_excl_release(owner);
    if (foreground) in2_reclaim_generation();
    irq_restore(f);
}

void in2_on_focus(int old, int owner)
{
    unsigned f = irq_save();
    clear_input();
    in2_ring_lost(IN2_LOST_FOCUS);
    in2_excl_focus(old);
    (void)owner; /* F1 already published the new foreground owner. */
    irq_restore(f);
}
