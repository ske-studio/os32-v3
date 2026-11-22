#ifndef PM_INPUT_USB_H
#define PM_INPUT_USB_H
#include "types.h"

void pm_input_keymap_boot(void);
/* Foreground queries preserve the caller's interrupt state. */
int pm_input_usb_take(void);
void pm_input_usb_note_remote(void);
/* Switch to the common platform_fep_input.h declarations when M1 lands. */
int platform_fep_key_local(void);
u8 platform_fep_key_shift(void);
int pm_input_usb_has_key(void);
/* Oldest cooked key without consuming it; -1 when empty (P3 ③b-1). */
int pm_input_usb_peek(void);
u32 pm_input_usb_dropped(void);
#endif
