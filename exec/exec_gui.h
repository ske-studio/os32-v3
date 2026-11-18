#ifndef OS32_EXEC_GUI_H
#define OS32_EXEC_GUI_H
#include "exec_hooks.h"
#if OS32_HOOK_GUI
#include "gui.h"
typedef void (*ExecGuiPump)(void);
static inline ExecGuiPump exec_gui_pump(void) { return (ExecGuiPump)gui_get_pump(); }
static inline void exec_gui_owner_exit(int owner, int kind) { gui_owner_exit(owner, kind); }
#else
typedef void (*ExecGuiPump)(void);
ExecGuiPump exec_gui_pump(void);
void exec_gui_owner_exit(int owner, int kind);
#endif
#endif
