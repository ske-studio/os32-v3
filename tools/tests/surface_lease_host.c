#define HOST_CALLER_COPY_TEST
static void lease_root_check(unsigned long root);
#define HOST_MMU_LOAD_CHECK(root) lease_root_check(root)
#include "access_walk_host.c"
#include "surface_query.h"
static int armed, reject_copy, break_rollback, abort_copy, gui, gfx_owner = 2;
static u32 copyin_calls;
int host_lease_copyin(const struct caller_access *c, const void *src, void *dst, u32 n)
{
    copyin_calls++;
    return copy_caller_bytes(c, src, dst, n);
}
static u32 requested_access = LEDGER_PERM_RO;
static u32 output_va = MEM_EXEC_LOAD_ADDR + PAGE_SIZE - 8;
static struct surface_query_source source;
static struct surface_ref *input;
static struct lease_view *output;
static struct addrspace peer;
static void lease_root_check(unsigned long root)
{
    if (armed) CHECK(root == space.pd_phys); /* Includes transient master hops. */
}
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return gfx_owner; }
/* Inject a last-page RO change after publication, then run the real B1
 * preflight/copy. No fake success/failure implementation of B1. */
int host_lease_copyout(const struct caller_access *c, void *dst, const void *src, u32 n)
{
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 index = ((MEM_EXEC_LOAD_ADDR + PAGE_SIZE) >> PAGE_SHIFT) % PTE_COUNT;
    u32 saved = pt[index];
    if (reject_copy) pt[index] &= ~PTE_RW;
    if (abort_copy) slot.state = APP_STATE_ABORT_PENDING;
    if (break_rollback) ((u32 *)P2V(space.pd_phys))[MEM_LEASE_BASE >> 22] |= PTE_PS;
    int rc = copy_to_caller(c, dst, src, n);
    pt[index] = saved;
    return rc;
}
static u32 table_hash(u32 root)
{
    u32 *pd = P2V(root), h = 2166136261U;
    for (u32 i = 0; i < PDE_COUNT; i++) {
        h = (h ^ pd[i]) * 16777619U;
        if (!(pd[i] & PTE_PRESENT)) continue;
        CHECK(!(pd[i] & PTE_PS));
        u32 *pt = P2V(pd[i] & ~0xfffU);
        for (u32 j = 0; j < PTE_COUNT; j++) h = (h ^ pt[j]) * 16777619U;
    }
    return h;
}
static u32 get_lease(int expected, const struct surface_ref *ref, struct lease_view *out)
{
    struct addrspace before = space, other = peer;
    struct ledger_surface ledger[LEDGER_MAX_SURFACES];
    u32 pages = used, flags = host_arch_if, root = host_cr3;
    u32 master = table_hash(paging_kernel_pd_phys()), own = table_hash(space.pd_phys);
    u32 foreign = table_hash(peer.pd_phys);
    kmemcpy(ledger, ledger_surfaces, sizeof(ledger));
    for (u32 i = 0; i < sizeof(*output); i++) ((u8 *)output)[i] = 0x55;
    armed = 1;
    CHECK(surface_lease(&source, ref, requested_access, out) == expected);
    CHECK(host_arch_if == flags && host_cr3 == root);
    CHECK(table_hash(paging_kernel_pd_phys()) == master);
    CHECK(table_hash(peer.pd_phys) == foreign);
    for (u32 i = 0; i < sizeof(peer); i++) CHECK(((u8 *)&peer)[i] == ((u8 *)&other)[i]);
    if (expected) {
        CHECK(used == pages);
        CHECK(table_hash(space.pd_phys) == own);
        for (u32 i = 0; i < sizeof(space); i++) CHECK(((u8 *)&space)[i] == ((u8 *)&before)[i]);
        for (u32 i = 0; i < sizeof(ledger); i++) CHECK(((u8 *)ledger_surfaces)[i] == ((u8 *)ledger)[i]);
        for (u32 i = 0; i < sizeof(*output); i++) CHECK(((u8 *)output)[i] == 0x55);
        return 0;
    }
    CHECK(output->token && output->base >= MEM_LEASE_BASE);
    CHECK(output->bytes == ledger_surfaces[source.refs[0].sid].npages * PAGE_SIZE);
    CHECK(output->planes[0] == output->base);
    CHECK((paging_lease_pte(&space, output->base) & ~0xfffU) ==
          ledger_surfaces[source.refs[0].sid].first * PAGE_SIZE);
    CHECK(!!(paging_lease_pte(&space, output->base) & PTE_RW) ==
          (requested_access == LEDGER_PERM_RW));
    CHECK(!lease_check(&space));
    return output->token;
}
#define GET(rc) get_lease(rc, (void *)MEM_EXEC_LOAD_ADDR, (void *)output_va)
static void caller_copy_tests(void)
{
    CallerAccessFrame previous;
    u32 owner, sid, alien, tokens[MEM_LEASE_MAX];
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 3, "peer", &owner));
    host_cr3 = paging_kernel_pd_phys();
    CHECK(!paging_addrspace_create_lease(&peer, owner));
    CHECK(!paging_addrspace_map_user(&space, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                                   payload + PAGE_SIZE, PAGE_RW | PTE_USER));
    limit = 4096;
    u32 backing = pgalloc_alloc_phys(space.owner, PTE_COUNT);
    struct ledger_surface sf = {.first = backing / PAGE_SIZE, .npages = PTE_COUNT,
        .owner = space.owner, .backing = LEDGER_SB_RAM, .backend = LEDGER_SF_PC98,
        .role = LEDGER_ROLE_CLIENT, .width = 64, .height = 16, .pitch = 8,
        .planes = 1, .perm_max = LEDGER_PERM_RW};
    CHECK(backing && ledger_surface_create(&sf, &sid));
    sf.first = pgalloc_alloc_phys(space.owner, 1) / PAGE_SIZE; sf.npages = 1;
    CHECK(sf.first && ledger_surface_create(&sf, &alien));
    source = (struct surface_query_source){LEDGER_ROLE_CLIENT, LEDGER_SF_PC98, 1, 1,
        {{sid, ledger_surfaces[sid].gen}}};
    struct lease_authority peer_auth = {peer.owner, source.backend, source.role};
    struct lease_view peer_view;
    CHECK(!lease_acquire(&peer, &peer_auth, source.refs, 1, LEDGER_PERM_RO, &peer_view));
    input = P2V(payload); *input = source.refs[0];
    output = P2V(payload + PAGE_SIZE - 8);
    host_cr3 = space.pd_phys;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    for (u32 depth = 0; depth < 2; depth++) {
        u32 calls = copyin_calls;
        if (depth) kctx_exc_depth = 1; else kctx_irq_depth = 1;
        GET(OS32_ERR_INVAL);
        CHECK(copyin_calls == calls);
        kctx_irq_depth = kctx_exc_depth = 0;
    }
    requested_access = LEDGER_PERM_RW;
    tokens[0] = GET(0); CHECK(!lease_release(&space, tokens[0]));
    requested_access = LEDGER_PERM_RO;
    for (u32 on = 0; on < 2; on++) {
        host_arch_if = on ? 0x202 : 2;
        tokens[0] = GET(0);
        CHECK(!lease_release(&space, tokens[0]));
        u32 last = tokens[0];
        tokens[0] = GET(0); CHECK(tokens[0] == last + 1);
        reject_copy = 1; GET(OS32_ERR_INVAL); reject_copy = 0;
        tokens[1] = GET(0); CHECK(tokens[1] == tokens[0] + 1);
        CHECK(!lease_release(&space, tokens[1]));
        limit = used; GET(OS32_ERR_NOSPC); limit = 4096;
        tokens[1] = GET(0); CHECK(tokens[1] == tokens[0] + 2);
        for (u32 i = 2; i < MEM_LEASE_MAX; i++) tokens[i] = GET(0);
        GET(OS32_ERR_FULL);
        CHECK(!lease_release(&space, tokens[7]));
        u32 next = GET(0); CHECK(next == tokens[7] + 1);
        CHECK(!lease_revoke_all(&space));
        CHECK(host_arch_if == (on ? 0x202U : 2U));
    }
    input->generation++; GET(OS32_ERR_STALE); *input = source.refs[0];
    *input = (struct surface_ref){alien, ledger_surfaces[alien].gen};
    GET(OS32_ERR_INVAL); *input = source.refs[0];
    input->sid = LEDGER_MAX_SURFACES; GET(OS32_ERR_INVAL); *input = source.refs[0];
    source.ready = 0; GET(OS32_ERR_INVAL); source.ready = 1;
    slot.state = APP_STATE_PARKED; GET(OS32_ERR_INVAL); slot.state = APP_STATE_RUNNING;
    caller_frame.access.owner++; GET(OS32_ERR_INVAL); caller_frame.access.owner--;
    source.role = LEDGER_ROLE_DISPLAY; slot.hdr_flags = OS32X_FLAG_GFX;
    ledger_surfaces[sid].role = LEDGER_ROLE_DISPLAY;
    GET(OS32_ERR_INVAL); /* Even a forged count=1 cannot lease planar DISPLAY. */
    source.count = 4; GET(OS32_ERR_INVAL); source.count = 1;
    source.backend = LEDGER_SF_PEGC; ledger_surfaces[sid].backend = LEDGER_SF_PEGC;
    tokens[0] = GET(0); CHECK(!lease_release(&space, tokens[0]));
    gui = 1; gfx_owner = 1; GET(OS32_ERR_INVAL); gfx_owner = 2; gui = 0;
    source.role = LEDGER_ROLE_UNICODE; source.backend = 0;
    ledger_surfaces[sid].role = LEDGER_ROLE_UNICODE; ledger_surfaces[sid].backend = 0;
    ledger_surfaces[sid].perm_max = LEDGER_PERM_RO;
    tokens[0] = GET(0); CHECK(!lease_release(&space, tokens[0]));
    requested_access = LEDGER_PERM_RW; GET(OS32_ERR_INVAL);
    requested_access = LEDGER_PERM_RO;
    source.role = LEDGER_ROLE_TVRAM; ledger_surfaces[sid].role = LEDGER_ROLE_TVRAM;
    tokens[0] = GET(0); CHECK(!lease_release(&space, tokens[0]));
    gui = 1; GET(OS32_ERR_INVAL); gui = 0;
    get_lease(OS32_ERR_INVAL, 0, (void *)output_va);
    get_lease(OS32_ERR_INVAL, (void *)0xfffffffcU, (void *)output_va);
    get_lease(OS32_ERR_INVAL, (void *)MEM_EXEC_LOAD_ADDR, 0);
    get_lease(OS32_ERR_INVAL, (void *)MEM_EXEC_LOAD_ADDR, (void *)0xfffffffcU);
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 ix = ((MEM_EXEC_LOAD_ADDR + PAGE_SIZE) >> PAGE_SHIFT) % PTE_COUNT;
    u32 pte = pt[ix]; pt[ix] = 0; GET(OS32_ERR_INVAL); pt[ix] = pte;
    pt[ix] &= ~PTE_RW; GET(OS32_ERR_INVAL); pt[ix] = pte;
    /* Managed table rejection: PS, wrong PFN, wrong owner, unaligned PD. */
    u32 *pd = P2V(space.pd_phys), d = pd[MEM_LEASE_BASE >> 22];
    pd[MEM_LEASE_BASE >> 22] |= PTE_PS;
    struct lease_authority auth = {space.owner, source.backend, source.role};
    struct lease_view view;
    CHECK(lease_acquire(&space, &auth, input, 1, LEDGER_PERM_RO, &view) == LEASE_INVAL);
    pd[MEM_LEASE_BASE >> 22] = d;
    u32 own_pt = space.lease_pt_phys[0];
    space.lease_pt_phys[0] = peer.lease_pt_phys[0];
    pd[MEM_LEASE_BASE >> 22] = peer.lease_pt_phys[0] | PAGE_RW | PTE_USER;
    CHECK(lease_acquire(&space, &auth, input, 1, LEDGER_PERM_RO, &view) == LEASE_INVAL);
    CHECK(!paging_lease_pte(&space, MEM_LEASE_BASE));
    space.lease_pt_phys[0] = own_pt;
    pd[MEM_LEASE_BASE >> 22] = d & ~PTE_USER;
    CHECK(lease_acquire(&space, &auth, input, 1, LEDGER_PERM_RO, &view) == LEASE_INVAL);
    CHECK(!paging_lease_pte(&space, MEM_LEASE_BASE));
    pd[MEM_LEASE_BASE >> 22] = d;
    space.lease_pt_phys[0]++;
    CHECK(lease_check(&space) == LEASE_INVAL);
    space.lease_pt_phys[0]--;
    /* PTE_USER is within the page: OR-ing rights hides this misalignment
     * from the exact PDE comparison, isolating each alignment guard. */
    space.lease_pt_phys[0] = own_pt + PTE_USER;
    CHECK(lease_acquire(&space, &auth, input, 1, LEDGER_PERM_RO, &view) == LEASE_INVAL);
    u32 *lease_pt = P2V(own_pt);
    CHECK(!lease_pt[1]);
    lease_pt[1] = PAGE_RW | PTE_USER;
    CHECK(!paging_lease_pte(&space, MEM_LEASE_BASE));
    lease_pt[1] = 0;
    space.lease_pt_phys[0] = own_pt;
    /* map also checks the PFN of each used PT. Put the same unaligned
     * managed frame in an unused PDE to isolate lease_context's full scan. */
    CHECK(!space.lease_pt_phys[1] && !pd[(MEM_LEASE_BASE >> 22) + 1]);
    space.lease_pt_phys[1] = own_pt + PTE_USER;
    pd[(MEM_LEASE_BASE >> 22) + 1] = own_pt | PAGE_RW | PTE_USER;
    CHECK(lease_acquire(&space, &auth, input, 1, LEDGER_PERM_RO, &view) == LEASE_INVAL);
    space.lease_pt_phys[1] = 0;
    pd[(MEM_LEASE_BASE >> 22) + 1] = 0;
    space.pd_phys++;
    CHECK(lease_acquire(&space, &auth, input, 1, LEDGER_PERM_RO, &view) == LEASE_INVAL);
    space.pd_phys--;
    /* B1 input may be RO; use a separate RW output page. */
    u32 first_ix = (MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT;
    pt[first_ix] &= ~PTE_RW;
    output_va = MEM_EXEC_LOAD_ADDR + PAGE_SIZE + 64;
    output = P2V(payload + PAGE_SIZE + 64);
    tokens[0] = GET(0); CHECK(!lease_release(&space, tokens[0]));
    pt[first_ix] |= PTE_RW;
    CHECK(caller_access_enter(&previous, CALLER_TRUSTED)); GET(OS32_ERR_INVAL);
    CHECK(caller_access_enter(&previous, CALLER_USER));
    /* The acquire-side recheck distinguishes a generation changed after refs. */
    CHECK(!surface_query_refs(&source, input, 1, LEDGER_PERM_RO));
    input->generation++;
    CHECK(lease_acquire(&space, &auth, input, 1, LEDGER_PERM_RO, &view) == LEASE_STALE);
    *input = source.refs[0];
    /* A pending abort after publication refuses the real B1 copyout, then
     * releases the unpublished lease without a rollback diagnostic. */
    CHECK(!lease_rollback_fail_count);
    u32 pages_before = used;
    abort_copy = 1;
    CHECK(surface_lease(&source, (void *)MEM_EXEC_LOAD_ADDR, requested_access,
                        (void *)output_va) == OS32_ERR_INVAL);
    abort_copy = 0; slot.state = APP_STATE_RUNNING;
    CHECK(!lease_rollback_fail_count && used == pages_before);
    CHECK(ledger_surfaces[sid].lease_count == 1 && !lease_check(&space));
    /* Deliberately break a managed PDE after publication: rollback cannot
     * release it. Diagnose once and retain the lease for exit-time revoke. */
    break_rollback = reject_copy = 1;
    CHECK(surface_lease(&source, (void *)MEM_EXEC_LOAD_ADDR, requested_access,
                        (void *)output_va) == OS32_ERR_INVAL);
    break_rollback = reject_copy = 0;
    CHECK(lease_rollback_fail_count == 1 && ledger_surfaces[sid].lease_count == 2);
    /* peer already holds one reference. */
    CHECK(space.leases[0].token);
    pd[MEM_LEASE_BASE >> 22] &= ~PTE_PS;
    CHECK(!lease_revoke_all(&space));
    CHECK(ledger_surfaces[sid].lease_count == 1 && !lease_check(&space));
    SAY("PASS: e2 single/B1/rollback/token/FULL/PT/STALE/managed tables/IF/CR3");
    die(0);
}
