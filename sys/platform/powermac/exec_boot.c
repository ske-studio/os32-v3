#include "exec_boot.h"
#include "fd_redirect.h"
#include "pipe_buffer.h"
#include "exec.h"
#include "sys/kern/exec/appslot.h"
#include "sys/kern/exec/shell_service.h"
#include "ppc_trap.h"

void exec_fault_recover(void);

/* Recover only a committed resident shell. */
static void resident_fault(struct ppc_frame *frame)
{
    if (!g_shell_service.generation || exec_nest_level != 1 ||
        res_owner_get() != APP_ID_SHELL || appslot_cur() != APP_ID_SHELL)
        return;
    AppSlot *shell = appslot_get(APP_ID_SHELL);
    if (!shell || shell->state != APP_STATE_RUNNING || shell->depth != 1 || shell->as)
        return;
    __builtin_memcpy(shell->jmpbuf->opaque + PPC_JMP_BUSY_OFFSET,
                     &frame->busy, sizeof(frame->busy));
    ppc_trap_onfault_clear();
    exec_fault_recover();
}

void pm_exec_boot(void)
{
    fd_redirect_init();
    pipe_buffer_init();
    exec_init();
    ppc_trap_set_resident_fault(resident_fault);
}
