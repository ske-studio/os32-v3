/* T2d d0b: saved redirect buffers belong to the registrant, not current CR3.
 * This is deliberately separate from the future syscall caller context (d1).
 */
#include "redir_access.h"
#include "appslot.h"
#include "fd_redirect.h"
#include "io.h"

extern int ring3_call_from_user(void);

/* Called only with IRQs saved. Never dereference the saved AS pointer. */
static int redir_live(const RedirAccess *a)
{
    AppSlot *slot;
    if (a->origin == REDIR_TRUSTED) return 1;
    if (a->origin != REDIR_USER) return 0;
    slot = appslot_get(a->app_id);
    if (!slot || !slot->cpl3 || !slot->as || slot->as != a->as ||
        slot->state == APP_STATE_ABORT_PENDING ||
        slot->state == APP_STATE_FAULT_PENDING) return 0;
    return slot->as->generation == a->generation && a->generation != 0 &&
           slot->as->owner == a->owner &&
           slot->as->pd_phys == a->pd_phys && a->pd_phys != 0;
}

int redir_access_capture(RedirAccess *out)
{
    unsigned int flags = irq_save();
    AppSlot *slot;
    RedirAccess a = {0};
    a.origin = ring3_call_from_user() ? REDIR_USER : REDIR_TRUSTED;
    a.app_id = appslot_cur();
    a.owner = (u32)res_owner_get();
    a.pd_phys = paging_kernel_pd_phys();
    if (a.origin == REDIR_USER) {
        slot = appslot_get(a.app_id);
        if (!slot || !slot->cpl3 || !slot->as ||
            res_owner_get() != a.app_id) goto fail;
        a.as = slot->as;
        a.owner = slot->as->owner;
        a.generation = slot->as->generation;
        a.pd_phys = slot->as->pd_phys;
        if (!redir_live(&a) || a.pd_phys != paging_current_cr3()) goto fail;
    }
    *out = a;
    irq_restore(flags);
    return 1;
fail:
    irq_restore(flags);
    return 0;
}

static int redir_page(const RedirAccess *a, u32 va, int write, u32 *pa)
{
    if (!redir_live(a)) return 0;
    if (a->origin == REDIR_TRUSTED) {
        *pa = va; /* Kernel/WM buffers are in the permanent identity mapping. */
        return 1;
    }
    return (write ? as_va_to_pa(a->pd_phys, va, pa) :
                    as_va_to_pa_read(a->pd_phys, va, pa)) == 0;
}

int redir_access_check(const RedirAccess *a, u32 va, u32 len, int write)
{
    if (len && (!va || len - 1 > ~(u32)0 - va)) return 0;
    while (len) {
        u32 pa, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        unsigned int flags = irq_save();
        int ok = redir_page(a, va, write, &pa);
        irq_restore(flags);
        if (!ok) return 0;
        if (n > len) n = len;
        len -= n;
        if (len) va += n;
    }
    return 1;
}

int redir_access_copy(const RedirAccess *a, u8 *base, u32 *pos,
                      void *peer, u32 len, int write)
{
    u32 va = (u32)(uptr)base, done = 0;
    u8 *bytes = peer;
    if (*pos > ~(u32)0 - va || len > ~(u32)0 - *pos ||
        (len && !peer)) return -1;
    va += *pos;
    if (!redir_access_check(a, va, len, write)) return -1;
    while (done < len) {
        u32 pa, i, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        unsigned int flags = irq_save();
        if (!redir_page(a, va, write, &pa)) {
            irq_restore(flags);
            return done ? (int)done : -1;
        }
        if (n > len - done) n = len - done;
        for (i = 0; i < n; i++) {
            if (write) ((u8 *)P2V(pa))[i] = bytes[done + i];
            else bytes[done + i] = ((const u8 *)P2V(pa))[i];
        }
        *pos += n;
        irq_restore(flags);
        done += n;
        if (done < len) va += n;
    }
    return (int)done;
}
