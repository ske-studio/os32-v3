/* v70 wraps over real saved caller, B1, lease and managed page tables. */
#define HOST_CALLER_COPY_TEST
#include "access_walk_host.c"
#include "surface_query.h"
static struct surface_query_source source;
static int gui;
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return 2; }
u32 gfx_sf_backend(void) { return LEDGER_SF_PC98; }
int gfx_surface_source(u32 role, struct surface_query_source *out)
{
    *out = source; out->role = role; return 0;
}
int system_surface_source(u32 role, struct surface_query_source *out)
{
    (void)role; (void)out; return OS32_ERR_INVAL;
}
int host_lease_copyin(const struct caller_access *c, const void *s, void *d, u32 n)
{ return copy_caller_bytes(c, s, d, n); }
int host_lease_copyout(const struct caller_access *c, void *d, const void *s, u32 n)
{ return copy_to_caller(c, d, s, n); }
static void *jump[5];
static u32 identity[3];
void ring3_fault_kill(void) { __builtin_longjmp(jump, 1); }
static int writable(u32 p, u32 n)
{
    struct caller_access c;
    if (!n) return 1;
    /* Identity uses host storage: the wrap directly writes the already checked
     * current VA. The surface paths below use the real managed B1 mapping. */
    if (p >= (u32)identity && p < (u32)(identity + 3) && n <= (u32)(identity + 3) - p) return 1;
    return caller_access_get(&c) && check_caller_write_range(&c, (void *)p, n);
}
int ring3_user_ranges_writable(u32 a, u32 n, u32 b, u32 m)
{ struct caller_access c;
  if (caller_access_get(&c) && c.origin == CALLER_TRUSTED) return 1;
  return writable(a, n) && writable(b, m); }
#define KAPI_OUT_LEN(p,n) ((u32)(n))
#define KAPI_HIT(slot) ((void)0)
#include "public_wrap_host_source.c"
#define REJECT(call) do { if (!__builtin_setjmp(jump)) { (void)(call); SAY("FAIL: guard " #call); die(1); } } while (0)
static void caller_copy_tests(void)
{
    CallerAccessFrame previous;
    u32 sid[4];
    host_cr3 = paging_kernel_pd_phys();
    CHECK(!paging_addrspace_map_user(&space, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                                   payload + PAGE_SIZE, PAGE_RW | PTE_USER));
    for (u32 i = 0; i < 4; i++) {
        u32 backing = pgalloc_alloc_phys(space.owner, 1);
        struct ledger_surface sf = {.first = backing / PAGE_SIZE, .npages = 1,
            .owner = space.owner, .backing = LEDGER_SB_RAM, .backend = LEDGER_SF_PC98,
            .role = LEDGER_ROLE_CLIENT, .width = 64, .height = 16, .pitch = 8,
            .planes = 1, .perm_max = LEDGER_PERM_RW};
        CHECK(backing && ledger_surface_create(&sf, &sid[i]));
        source.refs[i] = (struct surface_ref){sid[i], ledger_surfaces[sid[i]].gen};
    }
    source.role = LEDGER_ROLE_CLIENT; source.backend = LEDGER_SF_PC98;
    source.ready = source.count = 1;
    host_cr3 = space.pd_phys;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    OS32_SurfaceQueryResult *query = (void *)(MEM_EXEC_LOAD_ADDR + 256);
    OS32_LeaseView *view = (void *)(MEM_EXEC_LOAD_ADDR + 512);
    OS32_LeaseResult *bundle = (void *)(MEM_EXEC_LOAD_ADDR + 768);
    OS32_SurfaceRef *refs = (void *)MEM_EXEC_LOAD_ADDR;
    struct surface_ref *input = P2V(payload);
    kmemcpy(input, source.refs, sizeof(source.refs));
    REJECT(wrap_surface_query(source.role, 0));
    REJECT(wrap_surface_query(source.role, (void *)0xffffffffUL));
    REJECT(wrap_surface_lease(source.role, refs, LEDGER_PERM_RO, 0));
    REJECT(wrap_surface_lease(source.role, refs, LEDGER_PERM_RO, (void *)0xffffffffUL));
    REJECT(wrap_gfx_surface_lease(source.role, refs, 4, LEDGER_PERM_RO, 0));
    REJECT(wrap_gfx_surface_lease(source.role, refs, 4, LEDGER_PERM_RO, (void *)0xffffffffUL));
    CHECK(!wrap_surface_query(source.role, query));
    OS32_SurfaceQueryResult *q = P2V(payload + 256);
    CHECK(q->count == 1 && q->desc[0].ref.sid == sid[0] && q->desc[0].bytes == PAGE_SIZE);
    CHECK(!wrap_surface_lease(source.role, refs, LEDGER_PERM_RO, view));
    OS32_LeaseView *v = P2V(payload + 512);
    u32 token = v->token;
    CHECK(token && v->base >= MEM_LEASE_BASE);
    CHECK(!wrap_surface_unlease(token));
    CHECK(wrap_surface_unlease(token) == OS32_ERR_INVAL);
    /* Valid token in another AS cannot be released by the saved caller. */
    struct addrspace peer; u32 owner;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 3, "peer", &owner));
    host_cr3 = paging_kernel_pd_phys();
    CHECK(!paging_addrspace_create_lease(&peer, owner));
    struct lease_authority auth = {owner, source.backend, source.role};
    struct lease_view pv;
    CHECK(!lease_acquire(&peer, &auth, source.refs, 1, LEDGER_PERM_RO, &pv));
    host_cr3 = space.pd_phys;
    CHECK(wrap_surface_unlease(pv.token) == OS32_ERR_INVAL);
    CHECK(peer.leases[0].token == pv.token);
    input[0].generation++;
    CHECK(wrap_surface_lease(source.role, refs, LEDGER_PERM_RO, view) == OS32_ERR_STALE);
    input[0] = source.refs[0];
    source.role = LEDGER_ROLE_DISPLAY; source.count = 4;
    for (u32 i = 0; i < 4; i++) ledger_surfaces[sid[i]].role = source.role;
    slot.hdr_flags = 0;
    CHECK(wrap_surface_query(source.role, query) == OS32_ERR_INVAL);
    CHECK(wrap_gfx_surface_lease(source.role, refs, 4, LEDGER_PERM_RO, bundle) == OS32_ERR_INVAL);
    slot.hdr_flags = OS32X_FLAG_GFX;
    CHECK(!wrap_surface_query(source.role, query)); CHECK(q->count == 4);
    CHECK(!wrap_gfx_surface_lease(source.role, refs, 4, LEDGER_PERM_RO, bundle));
    OS32_LeaseResult *b = P2V(payload + 768); CHECK(b->count == 4);
    for (u32 i = 0; i < 4; i++) CHECK(!wrap_surface_unlease(b->views[i].token));
    input[3].generation++;
    CHECK(wrap_gfx_surface_lease(source.role, refs, 4, LEDGER_PERM_RO, bundle) == OS32_ERR_STALE);
    struct caller_identity actual = caller_identity_get();
    CHECK(!wrap_caller_identity(&identity[0], &identity[1], &identity[2]));
    CHECK(identity[0] == (u32)actual.app_id && identity[1] == actual.owner && identity[2] == actual.generation);
    for (u32 i = 0; i < 3; i++) {
        u32 *outputs[3] = {identity, identity + 1, identity + 2};
        outputs[i] = 0;
        REJECT(wrap_caller_identity(outputs[0], outputs[1], outputs[2]));
        outputs[i] = (void *)0xffffffffUL;
        REJECT(wrap_caller_identity(outputs[0], outputs[1], outputs[2]));
        CHECK(identity[0] == (u32)actual.app_id && identity[1] == actual.owner && identity[2] == actual.generation);
    }
    ring3_wm_enter();
    CHECK(!__builtin_setjmp(jump));
    CHECK(wrap_caller_identity(identity, identity + 1, identity + 2) == OS32_ERR_INVAL);
    CHECK(wrap_caller_identity(0, 0, 0) == OS32_ERR_INVAL);
    /* Internal USER identity remains usable by WM's delegated operations. */
    struct caller_access saved;
    CHECK(caller_access_get_user(&saved) && saved.origin == CALLER_USER);
    CHECK(caller_identity_get().owner == actual.owner);
    ring3_wm_leave();
    CHECK(identity[0] == (u32)actual.app_id && identity[1] == actual.owner && identity[2] == actual.generation);
    CHECK(!wrap_caller_identity(identity, identity + 1, identity + 2));
    CallerAccessFrame trusted_previous;
    CHECK(caller_access_enter(&trusted_previous, CALLER_TRUSTED));
    CHECK(wrap_caller_identity(identity, identity + 1, identity + 2) == OS32_ERR_INVAL);
    caller_access_leave(&trusted_previous);
    caller_access_invalidate();
    CHECK(wrap_caller_identity(identity, identity + 1, identity + 2) == OS32_ERR_INVAL);
    CHECK(identity[0] == (u32)actual.app_id && identity[1] == actual.owner && identity[2] == actual.generation);
    SAY("PASS: v70 generated wraps, output guards, query/lease/bundle/identity, foreign token, authorization, STALE");
    die(0);
}
