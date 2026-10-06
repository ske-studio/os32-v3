/* T2e e3: real B1, query, leases, paging, native exec map and V86 paths. */
#define HOST_CALLER_COPY_TEST
static void bundle_root_check(unsigned long root);
#define HOST_MMU_LOAD_CHECK(root) bundle_root_check(root)
#include "access_walk_host.c"
#include "surface_query.h"
#include "v86_mem.h"
void *kmemset(void *dst, int value, u32 size)
{
    for (u32 i = 0; i < size; i++) ((u8 *)dst)[i] = (u8)value;
    return dst;
}
static int armed, gui, gfx_owner = 2, reject_copy, break_rollback;
static u32 copyins, publications, release_calls, fail_release;
static u32 requested_access = LEDGER_PERM_RW;
int host_bundle_unmap(struct addrspace *as, u32 base, u32 npages)
{
    release_calls++;
    if (fail_release && release_calls == fail_release) return LEASE_INVAL;
    return paging_lease_unmap(as, base, npages);
}
static struct addrspace peer;
static struct surface_query_source source;
static struct surface_ref *input;
static struct surface_lease_result *output;
static u32 out_va = MEM_EXEC_LOAD_ADDR + PAGE_SIZE - 8;
static void bundle_root_check(unsigned long root)
{
    if (armed) CHECK(root == space.pd_phys);
}
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return gfx_owner; }
static u32 selected_backend = LEDGER_SF_PC98;
u32 gfx_sf_backend(void) { return selected_backend; }
void v86_bios_save_real(void) {}
void v86_bios_restore_real(void) {}
void v86_bios_setup(void) {}
void v86_io_apply_policy(void) {}
void v86_io_reset_policy(void) {}
void v86_runtime_end(void) { v86_session.active = 0; v86_session.running = 0; }
void v86_bios_detach_disk(void) {}
V86Gcap *v86_gcap_rec;
void v86_gcap_release(void) {}
void gfx_v86_return(void) {}
void kselftest_audit_v86_return(void) {}
/* Only the CPU write to V86 virtual RAM is redirected to its backing by the
 * harness. setup/teardown, map table and all PTE updates are real. */
#include "v86_host_source.c"
static void exec_native_map(struct addrspace *as)
{
    struct exec_map_context { struct addrspace *as; } context = {as}, *ctx = &context;
#include "exec_host_source.c"
}
int host_lease_copyin(const struct caller_access *c, const void *s, void *d, u32 n)
{
    copyins++;
    return copy_caller_bytes(c, s, d, n);
}
int host_lease_copyout(const struct caller_access *c, void *d, const void *s, u32 n)
{
    u32 *pt = P2V(space.app_pt_phys[0]);
    u32 ix = ((MEM_EXEC_LOAD_ADDR + PAGE_SIZE) >> PAGE_SHIFT) % PTE_COUNT;
    u32 save = pt[ix];
    publications++;
    if (reject_copy) pt[ix] &= ~PTE_RW;
    if (break_rollback) ((u32 *)P2V(space.pd_phys))[MEM_LEASE_BASE >> 22] |= PTE_PS;
    int rc = copy_to_caller(c, d, s, n);
    pt[ix] = save;
    return rc;
}
static u32 hash_tables(u32 root)
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
static void same_bytes(const void *a, const void *b, u32 n)
{
    for (u32 i = 0; i < n; i++) CHECK(((const u8 *)a)[i] == ((const u8 *)b)[i]);
}
static void native_cache(void)
{
    u32 *pd = P2V(paging_kernel_pd_phys()), *pt = P2V(pd[0] & ~0xfffU);
    for (u32 pa = 0; pa < MEM_1MB; pa += PAGE_SIZE) {
        /* Independent expected boundaries (also catches a bad shared macro). */
        u32 want = ((pa >= 0xa0000 && pa < 0xa4000) ||
                    (pa >= 0xa8000 && pa < 0xc0000) ||
                    (pa >= 0xe0000 && pa < 0xe8000)) ? PTE_PCD : 0;
        CHECK((pt[pa / PAGE_SIZE] & (PTE_PCD | PTE_PWT)) == want);
    }
}
static u32 get_bundle(int expected, u32 count, const void *refs, void *out)
{
    struct addrspace before = space, other = peer;
    struct ledger_surface ledger[LEDGER_MAX_SURFACES];
    u32 pages = used, flags = host_arch_if, root = host_cr3;
    u32 master = hash_tables(paging_kernel_pd_phys()), own = hash_tables(space.pd_phys);
    u32 foreign = hash_tables(peer.pd_phys);
    kmemcpy(ledger, ledger_surfaces, sizeof(ledger));
    kmemset(output, 0x55, sizeof(*output));
    armed = 1;
    CHECK(surface_lease_bundle(&source, refs, count, requested_access, out) == expected);
    CHECK(host_arch_if == flags && host_cr3 == root);
    CHECK(hash_tables(paging_kernel_pd_phys()) == master);
    CHECK(hash_tables(peer.pd_phys) == foreign);
    same_bytes(&peer, &other, sizeof(peer));
    if (expected) {
        CHECK(used == pages && hash_tables(space.pd_phys) == own);
        same_bytes(&space, &before, sizeof(space));
        same_bytes(ledger_surfaces, ledger, sizeof(ledger));
        for (u32 i = 0; i < sizeof(*output); i++) CHECK(((u8 *)output)[i] == 0x55);
        return 0;
    }
    CHECK(output->count == 4);
    for (u32 i = 0; i < 4; i++) {
        const struct lease_view *v = &output->views[i];
        const struct ledger_surface *sf = &ledger_surfaces[source.refs[i].sid];
        CHECK(v->token && v->bytes == GVRAM_PLANE_SIZE && v->planes[0] == v->base);
        for (u32 j = 1; j < 4; j++) CHECK(!v->planes[j]);
        for (u32 j = 0; j < 8; j++)
            CHECK(paging_lease_pte(&space, v->base + j * PAGE_SIZE) ==
                  ((sf->first + j) * PAGE_SIZE | PAGE_RO | PTE_USER | PTE_PCD |
                   (requested_access == LEDGER_PERM_RW ? PTE_RW : 0)));
        if (i) CHECK(v->token == output->views[i - 1].token + 1);
    }
    CHECK(!lease_check(&space));
    return output->views[0].token;
}
#define GET(rc) get_bundle(rc, 4, (void *)MEM_EXEC_LOAD_ADDR, (void *)out_va)
static void release_bundle(void)
{
    for (u32 i = 0; i < 4; i++) CHECK(!lease_release(&space, output->views[i].token));
}
static void caller_copy_tests(void)
{
    CallerAccessFrame frame;
    u32 owner, sid, filler_sid;
    const u32 planes[4] = {GVRAM_PLANE_B, GVRAM_PLANE_R, GVRAM_PLANE_G, GVRAM_PLANE_I};
    host_cr3 = paging_kernel_pd_phys();
    limit = 4096;
    native_cache();
    exec_native_map(&space); native_cache();
    CHECK(!v86_mem_setup(space.owner)); native_cache();
    v86_mem_teardown(); native_cache();
    CHECK(!v86_restore_mismatch);
    exec_native_map(&space); native_cache();
    /* access_walk has already made an AS; supply memory_boot's fixed record
     * directly, as lease_host does (registration is boot-context-only). */
    ledger_regions[ledger_region_count++] = (struct ledger_region){
        .type = LEDGER_R_FIXED, .owner = LEDGER_OWNER_KERNEL,
        .first = MEM_CONV_END / PAGE_SIZE, .end = KERNEL_LOAD_ADDR / PAGE_SIZE,
        .cache = LEDGER_CACHE_UC};
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 3, "peer", &owner));
    CHECK(!paging_addrspace_create_lease(&peer, owner));
    CHECK(!paging_addrspace_map_user(&space, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                                    payload + PAGE_SIZE, PAGE_RW | PTE_USER));
    source = (struct surface_query_source){LEDGER_ROLE_DISPLAY, LEDGER_SF_PC98, 1, 4, {{0}}};
    struct ledger_surface sf = {.npages = 8, .owner = LEDGER_OWNER_KERNEL,
        .backing = LEDGER_SB_VRAM, .backend = LEDGER_SF_PC98, .role = LEDGER_ROLE_DISPLAY,
        .cache = LEDGER_CACHE_UC, .width = 640, .height = 400, .pitch = 80,
        .planes = 1, .perm_max = LEDGER_PERM_RW};
    for (u32 i = 0; i < 4; i++) {
        sf.first = planes[i] / PAGE_SIZE;
        CHECK(ledger_surface_create(&sf, &sid));
        source.refs[i] = (struct surface_ref){sid, ledger_surfaces[sid].gen};
    }
    CHECK(!v86_mem_setup(space.owner));
    v86_mem_teardown();
    CHECK(!v86_restore_mismatch);
    /* A ledger cache mismatch must be observed independently of the table. */
    ledger_surfaces[source.refs[3].sid].cache = LEDGER_CACHE_WB;
    CHECK(!v86_mem_setup(space.owner));
    v86_mem_teardown();
    CHECK(v86_restore_mismatch != 0);
    ledger_surfaces[source.refs[3].sid].cache = LEDGER_CACHE_UC;
    /* Filler ends immediately before the fourth plane crosses a PDE. */
    u32 backing = pgalloc_alloc_phys(space.owner, PTE_COUNT - 24);
    sf = (struct ledger_surface){.first = backing / PAGE_SIZE, .npages = PTE_COUNT - 24,
        .owner = space.owner, .backing = LEDGER_SB_RAM, .backend = LEDGER_SF_PC98,
        .role = LEDGER_ROLE_CLIENT, .width = 1, .height = 1, .pitch = 1,
        .planes = 1, .perm_max = LEDGER_PERM_RW};
    CHECK(backing && ledger_surface_create(&sf, &filler_sid));
    struct surface_ref fill = {filler_sid, ledger_surfaces[filler_sid].gen};
    struct lease_authority auth = {space.owner, LEDGER_SF_PC98, LEDGER_ROLE_CLIENT};
    struct lease_view fv, pv;
    CHECK(!lease_acquire(&space, &auth, &fill, 1, LEDGER_PERM_RO, &fv));
    auth.owner = peer.owner;
    CHECK(!lease_acquire(&peer, &auth, &fill, 1, LEDGER_PERM_RO, &pv));
    input = P2V(payload); kmemcpy(input, source.refs, sizeof(source.refs));
    output = P2V(payload + PAGE_SIZE - 8);
    slot.hdr_flags = OS32X_FLAG_GFX;
    host_cr3 = space.pd_phys;
    CHECK(caller_access_enter(&frame, CALLER_USER));
    for (u32 on = 0; on < 2; on++) {
        host_arch_if = on ? 0x202 : 2;
        u32 token = GET(0); release_bundle();
        limit = used; GET(OS32_ERR_NOSPC); limit = 4096;
        CHECK(GET(0) == token + 4); release_bundle();
        for (u32 i = 0; i < MEM_LEASE_MAX; i++) if (!space.leases[i].token) {
            kmemset(&space.leases[i], 0x6a, sizeof(space.leases[i]));
            space.leases[i].token = 0;
        }
        reject_copy = 1; GET(OS32_ERR_INVAL); reject_copy = 0;
        CHECK(GET(0) == token + 8); release_bundle();
    }
    requested_access = LEDGER_PERM_RO; GET(0); release_bundle();
    requested_access = LEDGER_PERM_NONE; GET(OS32_ERR_INVAL);
    requested_access = LEDGER_PERM_RW;
    input[0].generation++;
    input[3].sid = LEDGER_MAX_SURFACES; GET(OS32_ERR_INVAL);
    input[3] = source.refs[0]; GET(OS32_ERR_INVAL);
    input[3] = source.refs[3]; input[3].generation = 0; GET(OS32_ERR_INVAL);
    input[3] = source.refs[3]; GET(OS32_ERR_STALE);
    kmemcpy(input, source.refs, sizeof(source.refs));
    input[3].generation++; GET(OS32_ERR_STALE); input[3] = source.refs[3];
    input[3] = fill; GET(OS32_ERR_INVAL); input[3] = source.refs[3];
    struct surface_ref swap = input[0]; input[0] = input[3]; input[3] = swap;
    GET(OS32_ERR_INVAL); kmemcpy(input, source.refs, sizeof(source.refs));
    for (u32 n = 0; n <= 5; n++) if (n != 4) {
        u32 before = copyins;
        get_bundle(OS32_ERR_INVAL, n, (void *)MEM_EXEC_LOAD_ADDR, (void *)out_va);
        CHECK(copyins == before);
    }
    /* Keep PC98 source/refs intact while the selected backend changes. */
    selected_backend = LEDGER_SF_PEGC; GET(OS32_ERR_INVAL);
    selected_backend = LEDGER_SF_CIRRUS; GET(OS32_ERR_INVAL);
    selected_backend = LEDGER_SF_PC98; GET(0); release_bundle();
    source.count = 3; GET(OS32_ERR_INVAL); source.count = 4;
    u32 before_copy = copyins;
    source.backend = LEDGER_SF_PEGC; GET(OS32_ERR_INVAL); source.backend = LEDGER_SF_PC98;
    CHECK(copyins == before_copy);
    source.role = LEDGER_ROLE_CLIENT; GET(OS32_ERR_INVAL); source.role = LEDGER_ROLE_DISPLAY;
    CHECK(copyins == before_copy);
    source.ready = 0; GET(OS32_ERR_INVAL); source.ready = 1;
    gui = 1; gfx_owner = 1; GET(OS32_ERR_INVAL);
    gfx_owner = 2; GET(0); release_bundle(); gui = 0;
    slot.hdr_flags = 0; GET(OS32_ERR_INVAL); slot.hdr_flags = OS32X_FLAG_GFX;
    for (u32 i = 0; i < 2; i++) {
        u32 before = copyins;
        if (i) kctx_exc_depth = 1; else kctx_irq_depth = 1;
        GET(OS32_ERR_INVAL); CHECK(copyins == before);
        kctx_exc_depth = kctx_irq_depth = 0;
    }
    get_bundle(OS32_ERR_INVAL, 4, 0, (void *)out_va);
    get_bundle(OS32_ERR_INVAL, 4, (void *)0xfffffff0U, (void *)out_va);
    get_bundle(OS32_ERR_INVAL, 4, (void *)MEM_EXEC_LOAD_ADDR, (void *)0xfffffff0U);
    u32 *pt = P2V(space.app_pt_phys[0]), ix = (MEM_EXEC_LOAD_ADDR / PAGE_SIZE + 1) % PTE_COUNT;
    u32 saved_pte = pt[ix], before_publications = publications;
    pt[ix] &= ~PTE_RW; GET(OS32_ERR_INVAL); pt[ix] = saved_pte;
    CHECK(publications == before_publications);
    /* Last plane has a corrupt master alias: no earlier plane is published. */
    u32 *master_pt = P2V(((u32 *)P2V(paging_kernel_pd_phys()))[0] & ~0xfffU);
    u32 ei = GVRAM_PLANE_I / PAGE_SIZE, saved_e = master_pt[ei];
    master_pt[ei] &= ~PTE_PCD; GET(OS32_ERR_INVAL); master_pt[ei] = saved_e;
    /* One preexisting filler + four bundle slots leave only three: FULL. */
    GET(0); u32 tokens[4];
    for (u32 i = 0; i < 4; i++) tokens[i] = output->views[i].token;
    GET(OS32_ERR_FULL);
    for (u32 i = 0; i < 4; i++) CHECK(!lease_release(&space, tokens[i]));
    CHECK(!lease_release(&space, fv.token));
    /* A broken managed PDE prevents all four releases: count each failure,
     * preserve their tokens/slots for exit recovery, leave output untouched. */
    u32 token = GET(0); release_bundle();
    u32 diag = lease_rollback_fail_count, copies = publications;
    reject_copy = break_rollback = 1;
    kmemset(output, 0x55, sizeof(*output));
    CHECK(surface_lease_bundle(&source, (void *)MEM_EXEC_LOAD_ADDR, 4,
                            LEDGER_PERM_RW, (void *)out_va) == OS32_ERR_INVAL);
    CHECK(publications == copies + 1 && lease_rollback_fail_count == diag + 4);
    for (u32 i = 0; i < 4; i++) CHECK(space.leases[i].token == token + 4 + i);
    for (u32 i = 0; i < sizeof(*output); i++) CHECK(((u8 *)output)[i] == 0x55);
    reject_copy = break_rollback = 0;
    ((u32 *)P2V(space.pd_phys))[MEM_LEASE_BASE >> 22] &= ~PTE_PS;
    CHECK(!lease_revoke_all(&space));
    CHECK(GET(0) == token + 8); release_bundle();
    /* One release fails between successful releases. Do not restore old
     * slots/token allocation, and diagnose only the one that remains live. */
    diag = lease_rollback_fail_count;
    release_calls = 0; fail_release = 2; reject_copy = 1;
    kmemset(output, 0x55, sizeof(*output));
    CHECK(surface_lease_bundle(&source, (void *)MEM_EXEC_LOAD_ADDR, 4,
                            LEDGER_PERM_RW, (void *)out_va) == OS32_ERR_INVAL);
    CHECK(release_calls == 4 && lease_rollback_fail_count == diag + 1);
    CHECK(!space.leases[0].token && space.leases[1].token &&
          !space.leases[2].token && !space.leases[3].token);
    CHECK(ledger_surfaces[source.refs[1].sid].lease_count == 1);
    for (u32 i = 0; i < sizeof(*output); i++) CHECK(((u8 *)output)[i] == 0x55);
    fail_release = reject_copy = 0;
    CHECK(!lease_revoke_all(&space));
    caller_access_leave(&frame);
    armed = 0; host_cr3 = paging_kernel_pd_phys();
    CHECK(!lease_revoke_all(&peer)); paging_addrspace_destroy(&peer);
    for (u32 i = 0; i < 4; i++) CHECK(ledger_surface_release(source.refs[i].sid));
    CHECK(ledger_surface_release(filler_sid));
    /* Restore the surrounding access_walk fixture's one-page expectation. */
    ((u32 *)P2V(space.app_pt_phys[0]))[ix] = 0;
    slot.hdr_flags = 0;
    SAY("surface bundle + native exec/V86 cache PASS");
    die(0);
}
