#ifndef EXEC_HWB_H
#define EXEC_HWB_H
#include "types.h"
/* HW_BASIC_DESIGN §3/F: save/disable IRQ, read and clear the dedicated CUI
 * shell abort flag, restore original IF. Return exactly 0/1. No other exec
 * state changes; kdebug POINT calls this through a private module import. */
u32 exec_hwb_abort_take(void);
#endif
