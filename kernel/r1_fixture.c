/* T2h: one-shot destructive probes, linked only into kernel-r1. */
#ifdef OS32_R1_FIXTURE
#include "r1_fixture.h"
#include "pgalloc.h"
#include "v86_mem.h"
#include "../exec/appslot.h"
#include "paging.h"

extern volatile int ring3_in_syscall;

volatile u32 r1_fixture_arm[R1_FIXTURE_SLOTS];
volatile u32 r1_fixture_id[R1_FIXTURE_SLOTS];
volatile u32 r1_fixture_generation[R1_FIXTURE_SLOTS];

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

void r1_fixture_v86_ready(void)
{
    if (v86_session.open && !v86_session.closing && ring3_in_syscall &&
        r1_fixture_take(R1_FIXTURE_V86_FAULT))
        __asm__ volatile("ud2"); /* CPL0, before entering the V86 guest. */
}

static AppSlot *r1_fixture_target(u32 hook, int id)
{
    AppSlot *a = appslot_get(id);
    if (!a || id < APP_ID_MIN || !a->cpl3 || !a->as ||
        !a->as->pd_phys || a->as->appmem_poisoned ||
        r1_fixture_id[hook] != (u32)id ||
        r1_fixture_generation[hook] != a->as->generation) return 0;
    return a;
}

void r1_fixture_parent(int child_id)
{
    AppSlot *child = appslot_get(child_id);
    if (!child || child->parent == child_id) return;
    AppSlot *parent = r1_fixture_target(R1_FIXTURE_PARENT_POISON, child->parent);
    if (parent && parent->state == APP_STATE_RUNNING &&
        r1_fixture_take(R1_FIXTURE_PARENT_POISON))
        paging_addrspace_poison(parent->as);
}

void r1_fixture_resume(int id)
{
    AppSlot *a = r1_fixture_target(R1_FIXTURE_PARKED_POISON, id);
    if (a && a->state == APP_STATE_PARKED && a->parked_from_wait &&
        r1_fixture_take(R1_FIXTURE_PARKED_POISON))
        paging_addrspace_poison(a->as);
}

int r1_fixture_lease(struct addrspace *as, u32 role)
{
    AppSlot *a = r1_fixture_target(R1_FIXTURE_LEASE_FAIL, appslot_cur());
    return a && a->as == as && role == LEDGER_ROLE_CLIENT &&
           r1_fixture_take(R1_FIXTURE_LEASE_FAIL);
}
#endif
