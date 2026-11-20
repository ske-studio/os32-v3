#ifndef OS32_FEP_INPUT_H
#define OS32_FEP_INPUT_H

#ifdef OS32_FEP_STATIC
#include "input_service.h"
#include "platform_fep_input.h"

#define kbd_getkey_wait input_getkey_wait
#define kbd_trygetkey   input_trygetkey
#define kbd_trygetchar  input_trygetchar
#define kbd_shift_state platform_fep_key_shift()
#endif

#endif
