#include "exec_boot.h"
#include "fd_redirect.h"
#include "pipe_buffer.h"
#include "exec.h"

void pm_exec_boot(void)
{
    fd_redirect_init();
    pipe_buffer_init();
    exec_init();
    /* The kernel boot context is the parent (ID 1). */
    exec_nest_level = 1;
}
