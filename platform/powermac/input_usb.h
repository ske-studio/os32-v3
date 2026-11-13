#ifndef PM_INPUT_USB_H
#define PM_INPUT_USB_H
#include "types.h"

enum { PM_INPUT_SHIFT = 1, PM_INPUT_CAPS = 2, PM_INPUT_CTRL = 4,
       PM_INPUT_GRPH = 8, PM_INPUT_SOURCES = 4, PM_INPUT_KEYS = 128 };
/* USB service callbacks run with EE=0. */
void pm_input_usb_key(u8 src, u8 sc, int down);
void pm_input_usb_repeat(u8 src, u8 sc);
void pm_input_usb_release(u8 src);
/* Foreground queries preserve the caller's interrupt state. */
int pm_input_usb_take(void);
int pm_input_usb_has_key(void);
u32 pm_input_usb_dropped(void);
/* Pure scan -> cooked key conversion; -1 means no output. */
int pm_input_keymap(u8 sc, u8 shift_state);
#endif
