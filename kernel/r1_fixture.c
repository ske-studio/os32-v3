/* T2h: one-shot destructive probes, linked only into kernel-r1. */
#ifdef OS32_R1_FIXTURE
#include "r1_fixture.h"
#include "pgalloc.h"
#include "v86_mem.h"

extern volatile int ring3_in_syscall;

volatile u32 r1_fixture_arm[R1_FIXTURE_SLOTS];

static int r1_fixture_take(u32 slot)
{
    if (r1_fixture_arm[slot] != R1_FIXTURE_ARM) return 0;
    r1_fixture_arm[slot] = 0; /* Clear before entering a non-returning path. */
    return 1;
}

void r1_fixture_timer(void)
{
    if (r1_fixture_take(R1_FIXTURE_IRQ_ALLOC))
        (void)pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, 1);
}

void r1_fixture_exception(u32 vector)
{
    if (vector == R1_FIXTURE_UD_VECTOR && r1_fixture_take(R1_FIXTURE_UD_FREE))
        (void)pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, 0, 1);
}

void r1_fixture_v86_end(void)
{
    if (v86_session.open && ring3_in_syscall && r1_fixture_take(R1_FIXTURE_V86_END))
        __asm__ volatile("ud2"); /* Actual nested exception, with closing=1. */
}
#endif
