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
    return as_access_page(a->as, va, write, pa);
}

/* d4 uses this inside its whole-range IRQ interval. Explicit USER contexts
 * remain USER during WM; current slot/owner/root must still match. */
int caller_access_page(const struct caller_access *a, u32 va, int write, u32 *pa)
{
    if (!a || !va || (a->origin == CALLER_TRUSTED && va >= MEM_APP_BAND_BASE))
        return 0;
    if (a->origin == CALLER_USER &&
        (a->app_id != appslot_cur() || res_owner_get() != a->app_id ||
         a->pd_phys != paging_current_cr3())) return 0;
    return redir_page(a, va, write, pa);
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

/* Bounded current-caller copies (T2d d4). No allocation/callback while IRQs
 * are saved. Kernel staging is valid, nonoverlapping and sized by the caller. */
static int caller_range(const struct caller_access *c, u32 va, u32 len, int write,
                         int per_page_irq)
{
    if (len && (!va || len - 1 > ~(u32)0 - va)) return 0;
    if (len && c && c->origin == CALLER_TRUSTED &&
        (va >= MEM_APP_BAND_BASE || len > MEM_APP_BAND_BASE - va)) return 0;
    while (len) {
        u32 pa, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        unsigned int flags = per_page_irq ? irq_save() : 0;
        int ok = caller_access_page(c, va, write, &pa);
        if (per_page_irq) irq_restore(flags);
        if (!ok) return 0;
        if (n > len) n = len;
        len -= n;
        if (len) va += n;
    }
    return 1;
}

int check_caller_write_range(const struct caller_access *c, void *dst, u32 len)
{
    /* Inspection is not a reservation: bound each IRQ interval to one page. */
    return caller_range(c, (u32)(uptr)dst, len, 1, 1);
}

static int caller_copy(const struct caller_access *c, u32 va, void *staging,
                       u32 len, int write)
{
    unsigned int flags = irq_save();
    u8 *bytes = staging;
    int ok = (!len || staging) && caller_range(c, va, len, write, 0);
    if (!ok) goto out;
    while (len) {
        u32 pa, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        /* Same IRQ interval as preflight: no AS/map mutation can intervene. */
        if (!caller_access_page(c, va, write, &pa)) { ok = 0; goto out; }
        if (n > len) n = len;
        if (write) kmemcpy(P2V(pa), bytes, n);
        else kmemcpy(bytes, P2V(pa), n);
        bytes += n;
        len -= n;
        if (len) va += n;
    }
out:
    irq_restore(flags);
    return ok;
}

int copy_to_caller(const struct caller_access *c, void *dst,
                   const void *src, u32 len)
{
    return caller_copy(c, (u32)(uptr)dst, (void *)src, len, 1);
}

int copy_caller_bytes(const struct caller_access *c, const void *src,
                      void *dst, u32 len)
{
    return caller_copy(c, (u32)(uptr)src, dst, len, 0);
}

int copy_caller_cstr(const struct caller_access *c, const char *src,
                     char *dst, u32 cap)
{
    u32 va = (u32)(uptr)src;
    unsigned int flags = irq_save();
    int ok = 0;
    u32 pa = 0;
    if (!va || !dst || !cap) goto out;
    for (u32 i = 0; i < cap; i++) {
        if (i > ~(u32)0 - va) goto out;
        /* Maps/identity cannot change in this IRQ interval. Reuse this page's
         * checked translation, never probe the next page before seeing NUL. */
        if (!i || !((va + i) & (PAGE_SIZE - 1))) {
            if (!caller_access_page(c, va + i, 0, &pa)) goto out;
        } else pa++;
        dst[i] = *(const char *)P2V(pa);
        if (!dst[i]) { ok = 1; break; }
    }
out:
    irq_restore(flags);
    return ok;
}
