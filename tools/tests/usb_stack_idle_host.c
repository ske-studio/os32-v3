/* Drive the PC-98 callback pair against a permanently exhausted IRQ0 budget. */
#include "sys/platform/pc98/usb_resident.c"

int kbd_shim_irq_depth;
u32 tick_budget_cnt(u32 us) { return us; }
int tick_budget_peek(u32 share, u32 tail)
{
    (void)share;
    (void)tail;
    return 0;
}
void pc98_usb_tick_vprintf(u8 attr, const char *fmt, const u32 *args)
{
    (void)attr;
    (void)fmt;
    (void)args;
}

void usb_stack_idle_step(void)
{
    tick_target = usb_service;
    usb_tick();
    usb_idle();
}
