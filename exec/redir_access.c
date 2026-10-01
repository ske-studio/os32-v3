/* Shared caller/registrant identity. Saved redirects allow a different current
 * CR3; current syscall callers additionally require current slot/owner/CR3. */
#include "redir_access.h"
#include "appslot.h"
#include "fd_redirect.h"
#include "io.h"
#include "kstring.h"

extern int ring3_call_from_user(void);
extern volatile int ring3_wm_depth;

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

static int access_capture(RedirAccess *out, enum caller_origin origin)
{
    unsigned int flags = irq_save();
    AppSlot *slot;
    RedirAccess a = {0};
    if (origin != CALLER_USER && origin != CALLER_TRUSTED) goto fail;
    a.origin = origin;
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

/* Only values survive across nested calls. Nonlocal exits invalidate them. */
static CallerAccessFrame caller_frame;

int caller_access_enter(CallerAccessFrame *previous, enum caller_origin origin)
{
    unsigned int flags = irq_save();
    struct caller_access a;
    int ok = access_capture(&a, origin);
    if (ok) {
        *previous = caller_frame;
        caller_frame.access = a;
        caller_frame.valid = 1;
    }
    irq_restore(flags);
    return ok;
}

void caller_access_leave(const volatile CallerAccessFrame *previous)
{
    unsigned int flags = irq_save();
    caller_frame = *previous;
    irq_restore(flags);
}

static int caller_access_saved_get(struct caller_access *out, int user_only)
{
    unsigned int flags = irq_save();
    const struct caller_access *a = &caller_frame.access;
    int ok = caller_frame.valid && redir_live(a);
    if (user_only && a->origin != CALLER_USER) ok = 0;
    if (ok && a->origin == CALLER_USER)
        ok = a->app_id == appslot_cur() && res_owner_get() == a->app_id &&
             a->pd_phys == paging_current_cr3();
    if (ok) *out = *a;
    irq_restore(flags);
    return ok;
}

void caller_access_save(volatile CallerAccessFrame *out)
{
    unsigned int flags = irq_save();
    *out = caller_frame;
    irq_restore(flags);
}

void caller_access_invalidate(void)
{
    unsigned int flags = irq_save();
    caller_frame.valid = 0;
    irq_restore(flags);
}

int caller_access_get_user(struct caller_access *out)
{
    return caller_access_saved_get(out, 1);
}

int caller_access_get(struct caller_access *out)
{
    /* Only explicit WM enter/leave grants this scope. Preserve the USER value
     * underneath; current CPL, pointer address and master CR3 grant nothing. */
    if (ring3_wm_depth > 0) return access_capture(out, CALLER_TRUSTED);
    return caller_access_saved_get(out, 0);
}

int redir_access_capture(RedirAccess *out)
{
    /* USER registration consumes the fixed entry identity. CPL0/WM direct
     * registration keeps its existing explicit trusted calling convention. */
    if (ring3_call_from_user()) return caller_access_get_user(out);
    return access_capture(out, CALLER_TRUSTED);
}

static int redir_page(const RedirAccess *a, u32 va, int write, u32 *pa)
{
    if (!redir_live(a)) return 0;
    if (a->origin == REDIR_TRUSTED) {
        *pa = V2P((const void *)(uptr)va);
        return 1;
    }
    return (write ? as_va_to_pa(a->pd_phys, va, pa) :
                    as_va_to_pa_read(a->pd_phys, va, pa)) == 0;
}

int redir_access_check(const RedirAccess *a, u32 va, u32 len, int write)
{
    if (len && (!va || len - 1 > ~(u32)0 - va)) return 0;
    if (a->origin == REDIR_TRUSTED &&
        (va > MEM_APP_BAND_BASE || len > MEM_APP_BAND_BASE - va)) return 0;
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
        u32 pa, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        unsigned int flags = irq_save();
        if (!redir_page(a, va, write, &pa)) {
            irq_restore(flags);
            return done ? (int)done : -1;
        }
        if (n > len - done) n = len - done;
        if (write) kmemcpy(P2V(pa), bytes + done, n);
        else kmemcpy(bytes + done, P2V(pa), n);
        *pos += n;
        irq_restore(flags);
        done += n;
        if (done < len) va += n;
    }
    return (int)done;
}
