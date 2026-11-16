/* IO_TIER C2/L1: public FG1 boundary; arch entries own IF/EE. */
#include "io_kapi.h"
#include "io_policy.h"
#include "io_work.h"
#include "io.h"
#include "appslot.h"
#include "shell_service.h"
#include "fd_redirect.h"
#include "pgalloc.h"
#include "os32_kapi_shared.h"

static int foreground(void)
{
    return _irq_enabled() && !kctx_irq_depth && !kctx_exc_depth; /* IO_KAPI_FG */
}

static int owner_generation(int owner, u32 *gen)
{
    AppSlot *a = appslot_get(owner);
    if (!a) return OS32_ERR_INVAL;
    if (owner == APP_ID_SHELL) {
        *gen = g_shell_service.generation;
    } else {
        if (!a->cpl3 || !a->as) return OS32_ERR_INVAL;
        *gen = a->as->generation;
    }
    return *gen ? 0 : OS32_ERR_INVAL;
}

int kapi_io_policy_set(const void *bytes, unsigned int len)
{
    u32 gen;
    int owner = res_owner_get();
    if (!foreground()) return OS32_ERR_BUSY;
    int rc = owner_generation(owner, &gen);
    if (rc < 0) return rc;
    return io_policy_set(bytes, len, (u32)owner, gen); /* IO_KAPI_SET_OWNER */
}

int kapi_io_policy_get(void *bytes, unsigned int cap, unsigned int *needed)
{
    if (!foreground()) return OS32_ERR_BUSY;
    return io_policy_get(bytes, cap, needed);
}

int kapi_io_policy_reset(void)
{
    u32 gen;
    int owner = res_owner_get();
    if (!foreground()) return OS32_ERR_BUSY;
    int rc = owner_generation(owner, &gen);
    if (rc < 0) return rc;
    return io_policy_reset((u32)owner, gen); /* IO_KAPI_RESET_OWNER */
}

int kapi_io_pump(unsigned int budget_us)
{
    u32 lo, hi;
    if (!foreground()) return OS32_ERR_BUSY;
    if (!budget_us) return OS32_ERR_INVAL;
    /* Empty dispatch also reports zero without a clock. Public absence is
     * explicit, including an image with no clock provider installed. */
    int rc = io_clock_us(&lo, &hi);
    if (rc < 0) return rc;
    return io_dispatch(budget_us < IO_PASS_US ? budget_us : IO_PASS_US); /* IO_KAPI_CAP */
}

void io_kapi_owner_exit(int owner)
{
    u32 gen;
    if (owner_generation(owner, &gen) == 0)
        io_policy_owner_exit((u32)owner, gen); /* IO_KAPI_RECLAIM */
}

/* Count only the outermost same-owner entry. Child execution suspends the
 * parent's interval, retaining its depth for the return to its own syscall. */
static u32 syscall_start[APP_SLOT_COUNT], syscall_active;
static u32 syscall_depth[APP_SLOT_COUNT];
u32 io_syscall_max_count;
void io_syscall_begin(int owner)
{
    if (owner < APP_ID_SHELL || owner > APP_ID_MAX) return;
    if (syscall_depth[owner]++ != 0) return; /* IO_SYSCALL_NEST_BEGIN */
    syscall_start[owner] = io_dispatch_total_count();
    syscall_active |= 1u << owner;
}
void io_syscall_end(int owner)
{
    if (owner < APP_ID_SHELL || owner > APP_ID_MAX ||
        !(syscall_active & (1u << owner))) return;
    if (--syscall_depth[owner] != 0) return; /* IO_SYSCALL_NEST_END */
    u32 elapsed = io_dispatch_total_count() - syscall_start[owner];
    if (elapsed > io_syscall_max_count) io_syscall_max_count = elapsed; /* IO_SYSCALL_MAX */
    syscall_active &= ~(1u << owner);
}

/* Internal exec token: zero means the launcher was outside a syscall. */
u32 io_syscall_suspend(int owner)
{
    if (owner < APP_ID_SHELL || owner > APP_ID_MAX) return 0;
    u32 depth = syscall_depth[owner];
    if (depth) {
        syscall_depth[owner] = 1;
        io_syscall_end(owner);
    }
    return depth;
}
void io_syscall_resume(int owner, u32 depth)
{
    if (!depth || owner < APP_ID_SHELL || owner > APP_ID_MAX) return;
    io_syscall_begin(owner);
    syscall_depth[owner] = depth;
}
