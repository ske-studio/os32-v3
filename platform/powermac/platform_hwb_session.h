#ifndef PLATFORM_HWB_SESSION_H
#define PLATFORM_HWB_SESSION_H
/* Private dbg_call operations, outside the module's opcode range. */
#define PLATFORM_HWB_ENTER 0x504d4801U
#define PLATFORM_HWB_LEAVE 0x504d4802U
#define PLATFORM_HWB_SESSION (PM_LOAD_KDEBUG && KBOOT_SH)
int resident_cui_hw_active(void);
int exec_hwb_session(unsigned int op);
#endif
