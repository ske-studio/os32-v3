#include "v86_mem.h"
#ifdef OS32_R1_FIXTURE
#include "r1_fixture.h"
#endif
#include "shlib.h"
/* Private SURFACE leases and the v70 single-surface USER entry. */
#include "lease.h"
#include "kstring.h"
#include "io.h"
#include "surface_query.h"
#include "appslot.h"
#include "../gfx/gfx.h"
#include "os32_kapi_shared.h"
static u32 lease_next_token = 1;
u32 lease_selftest_result;
volatile u32 lease_rollback_fail_count;
volatile u32 lease_revoke_fail_count;
volatile u32 lease_revoke_all_fail_count;

/* Keep this root/normal-context predicate in sync with
 * kernel/paging.c:lease_root_context(); paging also validates managed PTs. */
static int context(const struct addrspace *as)
{
    return as && as->pd_phys && !(as->pd_phys % PAGE_SIZE) && as->lease_pt_phys[0] &&
           pgalloc_page_owned(as->pd_phys / PAGE_SIZE, as->owner) &&
        (paging_current_cr3() == paging_kernel_pd_phys() ||
         paging_current_cr3() == as->pd_phys) &&
        !kctx_irq_depth && !kctx_exc_depth;
}

static int alias_cache(const struct ledger_surface *sf)
{
    u32 p, want = sf->cache == LEDGER_CACHE_UC ? PTE_PCD : 0;
    u32 *pd = P2V(paging_kernel_pd_phys());
    for (p = sf->first; p < sf->first + sf->npages; p++) {
        u32 pa = p * PAGE_SIZE, d = pd[pa >> 22], e;
        if (!(d & PTE_PRESENT) || (d & PTE_PS)) return 0;
        e = ((u32 *)P2V(d & ~0xfffUL))[p % PTE_COUNT];
        if (!(e & PTE_PRESENT) || (e & ~0xfffUL) != pa ||
            (e & (PTE_PCD | PTE_PWT)) != want) return 0;
    }
    return 1;
}

int lease_acquire(struct addrspace *as, const struct lease_authority *auth,
                  const struct surface_ref *refs, u32 count, u32 access,
                  struct lease_view *out)
{
    struct lease_mapping maps[MEM_LEASE_MAX];
    u32 slots[MEM_LEASE_MAX], i, j, k, base = MEM_LEASE_BASE;
    int rc;
    if (!context(as) || !auth || auth->owner != as->owner || !refs || !out ||
        !count || count > MEM_LEASE_MAX ||
        (access != LEDGER_PERM_RO && access != LEDGER_PERM_RW)) return LEASE_INVAL;
    if (!lease_next_token || count - 1 > 0xffffffffUL - lease_next_token) return LEASE_FULL;
    for (i = 0, k = 0; i < MEM_LEASE_MAX && k < count; i++)
        if (!as->leases[i].token) slots[k++] = i;
    if (k != count) return LEASE_FULL;
    for (i = 0; i < count; i++) {
        const struct ledger_surface *sf;
        if (refs[i].sid >= LEDGER_MAX_SURFACES) return LEASE_INVAL;
        sf = &ledger_surfaces[refs[i].sid];
        if (!sf->npages || sf->gen != refs[i].generation || sf->closing)
            return LEASE_STALE;
        if (sf->backend != auth->backend || sf->role != auth->role ||
            sf->perm_max == LEDGER_PERM_NONE ||
            (access == LEDGER_PERM_RW && sf->perm_max != LEDGER_PERM_RW) ||
            !ledger_surface_validate(sf) || !alias_cache(sf)) return LEASE_INVAL;
        for (j = 0, k = 1; j < i; j++) if (refs[j].sid == refs[i].sid) k++;
        if (sf->lease_count > 0xffffUL - k) return LEASE_FULL;
        /* First-fit whole-page span; previous prepared spans are below base. */
        for (;;) {
            u32 next = base;
            if (base >= MEM_LEASE_END || sf->npages > (MEM_LEASE_END - base) / PAGE_SIZE)
                return LEASE_FULL;
            for (j = 0; j < MEM_LEASE_MAX; j++) {
                const struct as_lease *l = &as->leases[j];
                if (l->token && base < l->base + l->npages * PAGE_SIZE &&
                    l->base < base + sf->npages * PAGE_SIZE)
                    if (next < l->base + l->npages * PAGE_SIZE) next = l->base + l->npages * PAGE_SIZE;
            }
            if (next == base) break;
            base = next;
        }
        maps[i].base = base;
        maps[i].phys = sf->first * PAGE_SIZE;
        maps[i].npages = sf->npages;
        maps[i].flags = PAGE_RO | PTE_USER |
            (access == LEDGER_PERM_RW ? PTE_RW : 0) |
            (sf->cache == LEDGER_CACHE_UC ? PTE_PCD : 0);
        maps[i].slot = slots[i];
        maps[i].sid = refs[i].sid;
        maps[i].generation = refs[i].generation;
        maps[i].token = lease_next_token + i;
        base += sf->npages * PAGE_SIZE;
    }
    /* No scheduling/callback in preparation. One short publication interval. */
#ifdef OS32_R1_FIXTURE
    if (r1_fixture_lease(as, auth->role)) return LEASE_FULL;
#endif
    rc = paging_lease_map(as, maps, count);
    if (!rc) lease_next_token += count;
    if (rc) return rc;
    for (i = 0; i < count; i++) {
        const struct ledger_surface *sf = &ledger_surfaces[refs[i].sid];
        out[i].token = as->leases[slots[i]].token;
        out[i].base = maps[i].base;
        out[i].bytes = maps[i].npages * PAGE_SIZE;
        for (j = 0; j < 4; j++) out[i].planes[j] = j < sf->planes ? maps[i].base + sf->plane_offset[j] : 0;
    }
    return 0;
}

int lease_release(struct addrspace *as, u32 token)
{
    u32 i;
    unsigned int saved;
    if (!context(as) || !token) return LEASE_INVAL;
    for (i = 0; i < MEM_LEASE_MAX; i++) if (as->leases[i].token == token) {
        struct as_lease *l = &as->leases[i];
        struct ledger_surface *sf = &ledger_surfaces[l->sid];
        saved = irq_save();
        if (paging_lease_unmap(as, l->base, l->npages)) {
            irq_restore(saved); return LEASE_INVAL;
        }
        l->token = 0;
        sf->lease_count--;
        if (sf->closing && !sf->lease_count) (void)ledger_surface_release(l->sid);
        irq_restore(saved);
        return 0;
    }
    return LEASE_INVAL;
}

/* A failed slot must not prevent independent slots from being revoked. */
int lease_revoke_sid(struct addrspace *as, u32 sid)
{
    u32 i;
    int failed = 0;
    if (!as || !as->pd_phys) {
        lease_revoke_fail_count++;
        return 1;
    }
    for (i = 0; i < MEM_LEASE_MAX; i++)
        if (as->leases[i].token &&
            (sid == LEDGER_MAX_SURFACES || as->leases[i].sid == sid) &&
            lease_release(as, as->leases[i].token)) {
            failed++;
            lease_revoke_fail_count++;
        }
    return failed;
}

/* The gfx caller owns the short master/IRQ interval, including ref checks. */
int lease_revoke_surface(u32 sid)
{
    int failed = 0;
    for (int id = APP_ID_SHELL; id <= APP_ID_MAX; id++) {
        AppSlot *a = appslot_at(id);
        if (a && a->as && a->as->pd_phys) failed += lease_revoke_sid(a->as, sid);
    }
    return failed;
}

int lease_revoke_all(struct addrspace *as)
{
    int failed = lease_revoke_sid(as, LEDGER_MAX_SURFACES);
    lease_revoke_all_fail_count += (u32)failed;
    return failed;
}

int lease_check(const struct addrspace *as)
{
    u32 va, i, expected, pte, *pd;
    if (!context(as)) return LEASE_INVAL;
    pd = P2V(as->pd_phys);
    for (i = 0; i < MEM_LEASE_MAX; i++) {
        const struct as_lease *l = &as->leases[i];
        if (!l->token) continue;
        if (l->sid >= LEDGER_MAX_SURFACES || !l->npages || l->base % PAGE_SIZE ||
            l->base < MEM_LEASE_BASE || l->base >= MEM_LEASE_END ||
            l->npages > (MEM_LEASE_END - l->base) / PAGE_SIZE) return LEASE_INVAL;
        const struct ledger_surface *sf = &ledger_surfaces[l->sid];
        u32 flags = l->flags & ~AS_LEASE_GFX_COMPAT;
        u32 cache = sf->cache == LEDGER_CACHE_UC ? PTE_PCD : 0;
        if (l->npages != sf->npages || !ledger_surface_validate(sf) ||
            (flags & ~PTE_RW) != (PTE_PRESENT | PTE_USER | cache) ||
            sf->perm_max == LEDGER_PERM_NONE ||
            ((flags & PTE_RW) && sf->perm_max != LEDGER_PERM_RW)) return LEASE_INVAL;
        for (u32 j = 0; j < i; j++) {
            const struct as_lease *o = &as->leases[j];
            if (o->token && (o->token == l->token ||
                (o->base < l->base + l->npages * PAGE_SIZE &&
                 l->base < o->base + o->npages * PAGE_SIZE))) return LEASE_INVAL;
        }
    }
    for (i = 0; i < MEM_LEASE_MAX_PDES; i++) {
        u32 d = pd[(MEM_LEASE_BASE >> 22) + i];
        if (as->lease_pt_phys[i]) {
            if (as->lease_pt_phys[i] % PAGE_SIZE ||
                !pgalloc_page_owned(as->lease_pt_phys[i] / PAGE_SIZE, as->owner)) return LEASE_INVAL;
            if ((d & ~(PTE_ACCESSED | PTE_DIRTY)) !=
                (as->lease_pt_phys[i] | PAGE_RW | PTE_USER)) return LEASE_INVAL;
        } else if (d) return LEASE_INVAL;
    }
    /* Only allocated tables can contain PTEs. Validate covered PDEs even
     * when a corrupt lease names an absent PT (which the scan would skip). */
    for (i = 0; i < MEM_LEASE_MAX; i++) {
        const struct as_lease *l = &as->leases[i];
        if (!l->token) continue;
        if (l->generation != ledger_surfaces[l->sid].gen ||
            !ledger_surfaces[l->sid].lease_count) return LEASE_INVAL;
        u32 first = (l->base - MEM_LEASE_BASE) >> 22;
        u32 last = (l->base + (l->npages - 1) * PAGE_SIZE - MEM_LEASE_BASE) >> 22;
        for (u32 di = first; di <= last; di++)
            if (!as->lease_pt_phys[di]) return LEASE_INVAL;
    }
    for (u32 di = 0; di < MEM_LEASE_MAX_PDES; di++) {
        if (!as->lease_pt_phys[di]) continue;
        const u32 *pt = P2V(as->lease_pt_phys[di]);
        for (u32 ti = 0; ti < PTE_COUNT; ti++) {
            va = MEM_LEASE_BASE + (di * PTE_COUNT + ti) * PAGE_SIZE;
            expected = 0;
            for (i = 0; i < MEM_LEASE_MAX; i++) {
                const struct as_lease *l = &as->leases[i];
                if (l->token && va >= l->base && (va - l->base) / PAGE_SIZE < l->npages) {
                    const struct ledger_surface *sf = &ledger_surfaces[l->sid];
                    if (!sf->npages || l->generation != sf->gen || !sf->lease_count) return LEASE_INVAL;
                    expected = sf->first * PAGE_SIZE + va - l->base;
                    expected |= l->flags & ~AS_LEASE_GFX_COMPAT;
                }
            }
            pte = pt[ti];
            if ((pte & ~(PTE_ACCESSED | PTE_DIRTY)) != expected) return LEASE_INVAL;
        }
    }
    return 0;
}

/* v70 USER lease entry. source is a kernel publisher value.
 * Normal context forbids AS scheduling/callbacks until copyout completes.
 * AS reclamation occurs only at safe points, so caller.as remains valid outside
 * IRQ-saved intervals. caller_access_get, authorization, publication and each
 * B1 helper save IRQs internally or at the call site, preserving entry IF.
 * Copy helpers recheck live identity each time; ABORT_PENDING rejects copyout
 * and takes rollback. acquire rechecks generation after refs. */
/* The kernel does not globally use -ffunction-sections. Keep this
 * entry in its own section; v70 KAPI wrappers now retain it. */
__attribute__((section(".text.surface_lease")))
int surface_lease(const struct surface_query_source *source,
                  const struct surface_ref *user_ref, u32 access,
                  struct lease_view *user_out)
{
    struct caller_access caller;
    struct surface_ref ref;
    struct lease_view view;
    struct lease_authority auth;
    struct as_lease old[MEM_LEASE_MAX];
    u32 next, i;
    unsigned int flags;
    int rc;
    if (kctx_irq_depth || kctx_exc_depth) return OS32_ERR_INVAL;
    if (!source || source->count != 1 ||
        (source->role == LEDGER_ROLE_DISPLAY && source->backend == LEDGER_SF_PC98))
        return OS32_ERR_INVAL;
    flags = irq_save();
    rc = !caller_access_get(&caller) || caller.origin != CALLER_USER;
    irq_restore(flags);
    if (rc || !copy_caller_bytes(&caller, user_ref, &ref, sizeof(ref)) ||
        !check_caller_write_range(&caller, user_out, sizeof(view)))
        return OS32_ERR_INVAL;
    flags = irq_save();
    rc = ((source->role == LEDGER_ROLE_CLIENT || source->role == LEDGER_ROLE_DISPLAY) &&
          source->backend != gfx_sf_backend()) ? OS32_ERR_INVAL :
         surface_query_authorize(source, &caller);
    if (!rc) rc = surface_query_refs(source, &ref, 1, access);
    irq_restore(flags);
    if (rc) return rc;
    auth = (struct lease_authority){caller.owner, source->backend, source->role};
    kmemcpy(old, caller.as->leases, sizeof(old));
    next = lease_next_token;
    rc = lease_acquire(caller.as, &auth, &ref, 1, access, &view);
    if (rc) return surface_query_error(rc);
    if (!copy_to_caller(&caller, user_out, &view, sizeof(view))) {
        /* B1 preflights the whole output before writing any byte. Only this
         * unpublished token is returned; restore even the unused slot bytes.
         * No intervening acquisition is possible in this normal-context path. */
        for (i = 0; i < MEM_LEASE_MAX; i++)
            if (caller.as->leases[i].token == view.token) break;
        if (!lease_release(caller.as, view.token)) {
            caller.as->leases[i] = old[i];
            lease_next_token = next;
        } else {
            /* Normally unreachable: an invariant was broken. Leave the
             * remaining lease for exit-time revoke and record the failure. */
            lease_rollback_fail_count++;
        }
        return OS32_ERR_INVAL;
    }
    return 0;
}

/* PC98 DISPLAY bundle; exposed by the v70 KAPI. No callback or AS switch between
 * input copy and copyout. Validate all refs before acquire's rechecks, in one
 * IRQ-saved interval, so a later INVAL takes precedence over an earlier STALE. */
__attribute__((section(".text.surface_lease_bundle")))
int surface_lease_bundle(const struct surface_query_source *source,
                      const struct surface_ref *user_refs, u32 count, u32 access,
                      struct surface_lease_result *user_out)
{
    struct caller_access caller;
    struct surface_ref refs[SURFACE_QUERY_MAX];
    struct surface_lease_result result = {0};
    struct lease_authority auth;
    struct as_lease previous[MEM_LEASE_MAX];
    u32 next_token, i, failed = 0;
    unsigned int saved;
    int rc;
    if (kctx_irq_depth || kctx_exc_depth || !source ||
        source->role != LEDGER_ROLE_DISPLAY || source->backend != LEDGER_SF_PC98 ||
        source->count != SURFACE_QUERY_MAX || count != SURFACE_QUERY_MAX)
        return OS32_ERR_INVAL;
    saved = irq_save();
    rc = !caller_access_get(&caller) || caller.origin != CALLER_USER;
    irq_restore(saved);
    if (rc || !copy_caller_bytes(&caller, user_refs, refs, sizeof(refs)) ||
        !check_caller_write_range(&caller, user_out, sizeof(result)))
        return OS32_ERR_INVAL;
    saved = irq_save();
    rc = ((source->role == LEDGER_ROLE_CLIENT || source->role == LEDGER_ROLE_DISPLAY) &&
          source->backend != gfx_sf_backend()) ? OS32_ERR_INVAL :
         surface_query_authorize(source, &caller);
    if (!rc) rc = surface_query_refs(source, refs, count, access);
    if (!rc) {
        auth = (struct lease_authority){caller.owner, source->backend, source->role};
        kmemcpy(previous, caller.as->leases, sizeof(previous));
        next_token = lease_next_token;
        rc = surface_query_error(lease_acquire(caller.as, &auth, refs, count,
                                               access, result.views));
    }
    irq_restore(saved);
    if (rc) return rc;
    result.count = count;
    if (!copy_to_caller(&caller, user_out, &result, sizeof(result))) {
        for (i = 0; i < count; i++) {
            if (lease_release(caller.as, result.views[i].token)) {
                lease_rollback_fail_count++;
                failed++;
            }
        }
        if (!failed) {
            kmemcpy(caller.as->leases, previous, sizeof(previous));
            lease_next_token = next_token;
        }
        return OS32_ERR_INVAL;
    }
    return 0;
}

/* Copy/compare every present table, excluding only CPU-owned A/D bits. */
static int tables_image(u32 root, u32 *image, int compare)
{
    u32 *pd = P2V(root), *walk = image + PTE_COUNT, i, j;
    for (i = 0; i < PDE_COUNT; i++) {
        if (compare) {
            if ((pd[i] & ~(PTE_ACCESSED | PTE_DIRTY)) !=
                (image[i] & ~(PTE_ACCESSED | PTE_DIRTY))) return 1;
        } else image[i] = pd[i];
    }
    for (i = 0; i < PDE_COUNT; i++) if (pd[i] & PTE_PRESENT) {
        u32 *pt = P2V(pd[i] & ~0xfffUL);
        for (j = 0; j < PTE_COUNT; j++) {
            if (compare) {
                if ((pt[j] & ~(PTE_ACCESSED | PTE_DIRTY)) !=
                    (walk[j] & ~(PTE_ACCESSED | PTE_DIRTY))) return 1;
            } else walk[j] = pt[j];
        }
        walk += PTE_COUNT;
    }
    return 0;
}

/* Boot component of §5-2: exact master PDE/PTE copies, S/T/U isolation. */
int lease_selftest(void)
{
    struct addrspace a = {0}, b = {0};
    struct ledger_surface sf = { .width = 1, .height = 1, .pitch = 1,
        .planes = 1, .backing = LEDGER_SB_RAM, .cache = LEDGER_CACHE_WB,
        .backend = LEDGER_SF_PC98, .role = LEDGER_ROLE_CLIENT,
        .perm_max = LEDGER_PERM_RW, .npages = 1 };
    struct surface_ref refs[3];
    struct lease_view av[2], bv[2];
    struct lease_authority auth;
    u32 owner = 0, oa = 0, ob = 0, i, n = 1, copy = 0, other = 0, frames[3] = {0};
    u32 made = 0, *pd = P2V(paging_kernel_pd_phys()), *snapshot;
    int rc = 1;
    if (paging_current_cr3() != paging_kernel_pd_phys()) goto done;
    if (!ledger_owner_new(LEDGER_KIND_AS, 0, "lease-sf", &owner) ||
        !ledger_owner_new(LEDGER_KIND_AS, 0, "lease-A", &oa) ||
        !ledger_owner_new(LEDGER_KIND_AS, 0, "lease-B", &ob)) goto done;
    for (i = 0; i < PDE_COUNT; i++) if (pd[i] & PTE_PRESENT) n++;
    copy = pgalloc_alloc_phys(owner, (int)n);
    if (!copy) goto done;
    snapshot = P2V(copy);
    tables_image(paging_kernel_pd_phys(), snapshot, 0);
    for (i = 0; i < 3; i++) {
        frames[i] = pgalloc_alloc_phys(owner, 1);
        if (!frames[i]) goto done;
        sf.first = frames[i] / PAGE_SIZE;
        sf.owner = (u8)owner;
        if (!ledger_surface_create(&sf, &refs[i].sid)) goto done;
        refs[i].generation = ledger_surfaces[refs[i].sid].gen;
        made++;
    }
    if (paging_addrspace_create_lease(&a, oa) || paging_addrspace_create_lease(&b, ob)) goto done;
    other = pgalloc_alloc_phys(owner, (int)n + 1);
    if (!other) goto done;
    tables_image(b.pd_phys, P2V(other), 0);
    auth.backend = sf.backend; auth.role = sf.role; auth.owner = oa;
    /* A gets S/U; B gets T/U. */
    {
        struct surface_ref r[2] = {refs[0], refs[2]};
        if (lease_acquire(&a, &auth, r, 2, LEDGER_PERM_RW, av)) goto done;
        if (tables_image(b.pd_phys, P2V(other), 1)) goto done;
        r[0] = refs[1]; auth.owner = ob;
        if (lease_acquire(&b, &auth, r, 2, LEDGER_PERM_RO, bv)) goto done;
    }
    if (paging_as_audit(&a, shlib_read_page) || paging_as_audit(&b, shlib_read_page) ||
        lease_check(&a) || lease_check(&b) ||
        (paging_lease_pte(&a, av[0].base) & ~0xfffUL) != frames[0] ||
        (paging_lease_pte(&b, bv[0].base) & ~0xfffUL) != frames[1] ||
        (paging_lease_pte(&a, av[1].base) & ~0xfffUL) != frames[2] ||
        (paging_lease_pte(&b, bv[1].base) & ~0xfffUL) != frames[2]) goto done;
#ifndef PHYSMEM_HOST_TEST
    /* Synthetic ASes retain identity supervisor backing in T2b. The actual
     * lease aliases are dereferenced only under their corresponding CR3. */
    {
        unsigned int saved = irq_save();
        u8 seen;
        paging_load_cr3(a.pd_phys);
        *(volatile u8 *)(uptr)av[1].planes[0] = 0x5a;
        paging_load_cr3(b.pd_phys);
        seen = *(volatile u8 *)(uptr)bv[1].planes[0];
        paging_load_cr3(paging_kernel_pd_phys());
        irq_restore(saved);
        if (seen != 0x5a) goto done;
    }
#endif
    tables_image(b.pd_phys, P2V(other), 0);
    if (lease_release(&a, bv[1].token) != LEASE_INVAL ||
        lease_release(&a, av[1].token) || lease_check(&b) ||
        ledger_surfaces[refs[2].sid].lease_count != 1 ||
        lease_release(&a, av[1].token) != LEASE_INVAL) goto done;
    if (tables_image(paging_kernel_pd_phys(), snapshot, 1) ||
        tables_image(b.pd_phys, P2V(other), 1)) goto done;
    rc = 0;
done:
    if (a.pd_phys) { if (lease_revoke_all(&a)) rc |= 2; paging_addrspace_destroy(&a); }
    if (b.pd_phys) { if (lease_revoke_all(&b)) rc |= 2; paging_addrspace_destroy(&b); }
    for (i = 0; i < made; i++) {
        if (!ledger_surface_release(refs[i].sid)) rc |= 4;
        frames[i] = 0;
    }
    for (i = 0; i < 3; i++) if (frames[i]) pgalloc_free_n_owner(owner, frames[i] / PAGE_SIZE, 1);
    if (copy) pgalloc_free_n_owner(owner, copy / PAGE_SIZE, (int)n);
    if (other) pgalloc_free_n_owner(owner, other / PAGE_SIZE, (int)n + 1);
    if (oa && !ledger_owner_retire(oa)) rc |= 8;
    if (ob && !ledger_owner_retire(ob)) rc |= 8;
    if (owner && !ledger_owner_retire(owner)) rc |= 8;
    lease_selftest_result = (u32)rc;
    return rc;
}

/* Audit all real ASes under a temporary master root, preserving caller IF/CR3.
 * appslot_at includes in-construction and pending-kill slots. */
int lease_audit_all(void)
{
    u32 refs[LEDGER_MAX_SURFACES] = {0};
    unsigned int flags;
    u32 cr3;
    int bad = 0;
    if (paging_v86_session_open()) return -1;
    if (kctx_irq_depth || kctx_exc_depth) return 1;
    /* Normal context has no scheduling/reclamation point here. IRQ handlers
     * cannot change AS/lease ownership. Restore IF and CR3 between ASes. */
    for (int id = APP_ID_SHELL; id <= APP_ID_MAX; id++) {
        AppSlot *a = appslot_at(id);
        if (!a || !a->as || !a->as->pd_phys) continue;
        flags = irq_save();
        cr3 = paging_current_cr3();
        paging_load_cr3(paging_kernel_pd_phys());
        bad += paging_as_audit(a->as, shlib_read_page) != 0;
        bad += lease_check(a->as) != 0;
        for (u32 i = 0; i < MEM_LEASE_MAX; i++) {
            const struct as_lease *l = &a->as->leases[i];
            if (l->token && l->sid < LEDGER_MAX_SURFACES) refs[l->sid]++;
        }
        paging_load_cr3(cr3);
        irq_restore(flags);
    }
    /* Alias/descriptor reads need no root switch or long IRQ exclusion. */
    for (u32 i = 0; i < LEDGER_MAX_SURFACES; i++) {
        const struct ledger_surface *sf = &ledger_surfaces[i];
        if (sf->npages && (!ledger_surface_validate(sf) || !alias_cache(sf))) bad++;
    }
    flags = irq_save();
    for (u32 i = 0; i < LEDGER_MAX_SURFACES; i++)
        if (ledger_surfaces[i].lease_count != refs[i]) bad++;
    irq_restore(flags);
    return bad;
}
