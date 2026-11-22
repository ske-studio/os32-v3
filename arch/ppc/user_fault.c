/* Match the exception context established by x86 EXC_ENTER. */
#include "ppc_trap.h"
#include "exec/exec.h"
#include "mm/pgalloc.h"

void ppc_user_fault_kill(void)
{
    kctx_exc_depth++;
    ring3_fault_kill();
    kctx_exc_depth--;
}
