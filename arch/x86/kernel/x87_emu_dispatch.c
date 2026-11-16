/* OS32 commit owner. Entry assembly / x87.c / build wiring are separate stages. */
#include "x87_emu_internal.h"
#include "x87.h"
#include "appslot.h"
#include "fd_redirect.h"
#include "io.h"
#include "gdt.h"

#ifndef X87_EMU_HOST_TEST
static bool eligible(const struct addrspace *as)
{
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    AppSlot *slot = appslot_get(appslot_cur());
    return !arch_fp_present() && (cr0 & 4) && slot && slot->cpl3 &&
        slot->as && slot->as == as && as->generation && as->pd_phys &&
        slot->state != APP_STATE_ABORT_PENDING && slot->state != APP_STATE_FAULT_PENDING &&
        (u32)res_owner_get() == as->owner && res_owner_get() == appslot_cur() &&
        paging_current_cr3() == as->pd_phys;
}
#else
extern bool x87_emu_test_eligible(const struct addrspace *as);
#define eligible x87_emu_test_eligible
#define irq_save() 0u
#define irq_restore(flags) ((void)(flags))
#endif

enum x87_emu_result x87_emu_dispatch(struct x87_emu_trap *t,
    const struct addrspace *as, struct x87_emu_state *active)
{
    static bool busy;
    unsigned flags = irq_save();
    enum x87_emu_result r = X87_EMU_NOT_USER;
    if (!t || !active || t->segment[X87_CS] != USER_CS ||
        (t->eflags & (1u << 17)) || !eligible(as)) goto leave;
    if (busy) { r = X87_EMU_REENTERED; goto leave; }
    busy = true;
    struct x87_emu_access access = {as, t};
    struct x87_emu_decoded d;
    struct x87_emu_effect e = {.state = *active, .commit = X87_ALL_COMMIT};
    for (unsigned i = 0; i < 8; ++i) e.gpr[i] = t->gpr[i];
    r = x87_emu_decode(t, x87_emu_fetch, &access, &d);
    if (r) goto done;
    e.next_eip = t->eip + d.length;
    r = x87_emu_execute(t, &access, &d, &e);
    if (r) goto done;
    r = x87_emu_exception(active, &d, &e);
    if (r) goto done;
    if (d.memory && d.write && (e.commit & X87_COMMIT_MEMORY)) {
        r = x87_emu_write(&access, &d, e.output);
        if (r) goto done;
    }
    /* Exception policy constructs a complete state, including suppressed fields. */
    *active = e.state;
    if (e.commit & X87_COMMIT_GPR)
        for (unsigned i = 0; i < 8; ++i) t->gpr[i] = e.gpr[i];
    if (e.commit & X87_COMMIT_EIP) t->eip = e.next_eip;
done:
    busy = false;
leave:
    irq_restore(flags);
    return r;
}
