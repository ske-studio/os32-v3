#ifndef OS32_EXEC_GUI_H
#define OS32_EXEC_GUI_H
#include "gui.h"
static inline void exec_gui_owner_exit(int owner, int kind) { gui_owner_exit(owner, kind); }
#endif
