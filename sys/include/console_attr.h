/* Pure printf attribute conversion; raw console attributes bypass this. */
#ifndef CONSOLE_ATTR_H
#define CONSOLE_ATTR_H

#include "types.h"

u8 con_backend_printf_attr(u8 input);

#endif /* CONSOLE_ATTR_H */
