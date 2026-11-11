#ifndef OS32_EXEC_GUI_H
#define OS32_EXEC_GUI_H
typedef void (*ExecGuiPump)(void);
ExecGuiPump exec_gui_pump(void);
void exec_gui_owner_exit(int owner, int kind);
#endif
