#include "input_usb.h"
#include "io.h"
#include "sys/hal/input/in2_int.h"

static u8 last_local, last_shift;
int platform_fep_key_local(void) { return last_local; }
u8 platform_fep_key_shift(void) { return last_shift; }
void pm_input_usb_note_remote(void)
{
    last_local = last_shift = 0;
}
int pm_input_usb_take(void)
{
    u32 flags = irq_save();
    int key = legacy_cooked_take_mods(&last_shift);
    if (key >= 0) last_local = 1;
    irq_restore(flags);
    return key;
}
int pm_input_usb_peek(void) { return legacy_cooked_peek(); }
int pm_input_usb_has_key(void) { return legacy_cooked_pending() != 0; }
u32 pm_input_usb_dropped(void) { return legacy_cooked_dropped() + legacy_raw_dropped(); }
