/* Real I1 units, real allocator/ledger/PTs; CPU MMU and device edges are fake. */
#include "types.h"
#include "kstring.h"
u32 host_cr3;
static void host_sync(u32 root);
#define HOST_MMU_LOAD_CHECK(root) host_sync(root)
#include "paging_host_source.c"
#include "pgalloc_host_source.c"
#include "pgalloc_host_fixture.h"
#include "gfx2_int.h"
#include "gfx2_exec.h"
#include "gfx2_sw.h"
#include "sync_int.h"
#include "palette.h"
#include "dma_contract.h"
#include "module_runtime.h"
#include "redir_access.h"
#include "appslot.h"
#include "lease.h"

#ifndef I1_APP_ID
#define I1_APP_ID APP_ID_SHELL
#endif
static AppSlot slot;
static struct addrspace as;
static u32 mirror[16], mirror_n, bind_n, timeout;
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
static void say(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define SAY(s) say(s "\n", sizeof(s "\n") - 1)
#define CHECK(x) do { if (!(x)) { SAY("FAIL " #x); die(1); } } while (0)

/* Model CR3's translations with a host window. Only this CPU edge copies;
 * PTEs, their owners, allocation, map/unmap and audits are product code. */
static void host_sync(u32 root)
{
    for (u32 i = 0; i < mirror_n; i++)
        kmemcpy(P2V(mirror[i]), (void *)(MEM_GFX2_MAP_BASE + i * PAGE_SIZE), PAGE_SIZE);
    mirror_n = 0;
    if (!root) return;
    u32 d = ((u32 *)P2V(root))[MEM_GFX2_MAP_BASE >> PDE_SHIFT];
    if (!(d & PTE_PRESENT)) return;
    u32 *pt = P2V(d & PTE_FRAME_MASK);
    for (u32 i = 0; i < 16 && (pt[i] & PTE_PRESENT); i++) {
        mirror[i] = pt[i] & PTE_FRAME_MASK;
        kmemcpy((void *)(MEM_GFX2_MAP_BASE + i * PAGE_SIZE), P2V(mirror[i]), PAGE_SIZE);
        mirror_n++;
    }
}

void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int legacy_lowmap_busy(void) { return 0; }
void exec_addrspace_abort(struct addrspace *space) { (void)space; }
AppSlot *appslot_at(int id) { return id == I1_APP_ID ? &slot : NULL; }
int shlib_read_page(u32 va, u32 frame) { (void)va; (void)frame; return 0; }
u32 exec_tramp_page_addr(void) { return 0; }
i32 gfx_screen_owner(void) { return I1_APP_ID; }
void gfx_policy_reeval(void) {}
int caller_access_get_user(struct caller_access *out)
{
    *out = (struct caller_access){ .origin = CALLER_USER, .app_id = I1_APP_ID,
        .as = &as, .owner = as.owner, .pd_phys = as.pd_phys, .generation = as.generation };
    return as.pd_phys != 0;
}
static int gart_bind(void *ctx, const u32 *pfn, u32 n, u32 *off)
{
    (void)ctx;
    CHECK(n && pfn); bind_n = n; *off = 0; return 0;
}
static int gart_unbind(void *ctx, u32 off, u32 n)
{ (void)ctx; CHECK(!off && n == bind_n); bind_n = 0; return 0; }
static int alloc(void *ctx, struct gfx2_surf *s) { (void)ctx; (void)s; return 0; }
static int wait_fence(void *ctx, u32 f, u32 dl)
{ (void)ctx; (void)f; (void)dl; return timeout ? OS32_ERR_TIMEOUT : 0; }
static int pending(void *ctx, u32 *done, u32 *fail)
{ (void)ctx; *done = *fail = 0; return 0; }
static GfxBackend v1 = { .bb_format = GFX_BB_PACKED8 };
static GfxBackendV2 be;

static void begin(void)
{
    u32 owner;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 0, "I1-test", &owner));
    CHECK(!paging_addrspace_create_lease(&as, owner));
    slot.as = &as;
}
static void finish(void)
{
    u32 owner = as.owner, left;
    CHECK(!gfx2_owner_release(&as));
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++)
        CHECK(gfx2_surfs[i].map_as != &as &&
              (gfx2_surfs[i].state == GFX2_ST_FREE || gfx2_surfs[i].owner != owner));
    paging_addrspace_destroy(&as);
    CHECK(ledger_reclaim_owner(owner, &left));
    CHECK(!left && !ledger_owner_pages(owner));
    CHECK(ledger_owner_retire(owner));
    slot.as = NULL;
    kmemset(&as, 0xcc, sizeof(as));
    gfx2_closing_sweep(gfx2_adapter_get(0));
}
static struct gfx2_surf *surface(u8 pool, u16 h)
{
    u32 sid;
    int rc;
    CHECK(!gfx2_surface_create(0, 64, h, GFX2_FMT_XRGB8888, pool, GFX2_USE_RENDER2D, &sid));
    struct gfx2_surf *s = gfx2_surf_get(sid, as.owner, &rc);
    CHECK(s && !rc); return s;
}
static void mapped_write(struct gfx2_surf *s)
{
    void *va;
    u32 pitch;
    CHECK(!gfx2_lock(s->sid, GFX2_LOCK_DISCARD, &va, &pitch));
    CHECK((u32)va == MEM_GFX2_MAP_BASE && pitch == s->pitch);
    CHECK(!lease_check(&as) && !gfx2_map_audit(&as));
    paging_load_cr3(as.pd_phys);
    CHECK(gfx2_cpu_visible(s));
    for (u32 i = 0; i < s->bytes / sizeof(u32); i++) ((volatile u32 *)va)[i] = i ^ 0x12345678UL;
    paging_load_cr3(paging_kernel_pd_phys());
    for (u32 i = 0; i < s->bytes / sizeof(u32); i++) {
        u32 pa, attr;
        CHECK(!gfx2_surf_page(s, i * sizeof(u32) / PAGE_SIZE, &pa, &attr));
        CHECK(*(u32 *)P2V(pa + i * sizeof(u32) % PAGE_SIZE) == (i ^ 0x12345678UL));
    }
    CHECK(!gfx2_unlock(s->sid));
    CHECK(!lease_check(&as) && !gfx2_map_audit(&as));
}
void _start(void)
{
    u32 args[6] = {KERNEL_LOAD_ADDR, MEM_POOL_BASE + 0x4000000 - KERNEL_LOAD_ADDR,
                  3, 0x32, 0xffffffffUL, 0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == KERNEL_LOAD_ADDR);
    args[0] = MEM_GFX2_MAP_BASE; args[1] = 16 * PAGE_SIZE;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == MEM_GFX2_MAP_BASE);
    host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    CHECK(!gfx2_map_selftest() && gfx2_map_selftest_runs == 1);
    palette_shadow_reset(); gfx2_pal_reset16();
    CHECK(gfx2_pal[1] == 0x77 && gfx2_pal[15] == 0xffffffUL && !gfx2_pal[16]);
    const u8 rgb[3] = {15, 8, 1};
    gfx2_pal_set_v1(255, 1, rgb);
    CHECK(gfx2_pal[255] == 0xff8811UL);
    CHECK(gfx2_sw_table[GFX2_OP_FILL] == gfx2_sw_fill);
    CHECK(!gfxsync_ctx_pool[0].used);
    CHECK(!gfx2_adapter_register(&v1, GFX2_KIND_PC98, GFX2_AF_ACTIVE));
    struct gfx2_adapter *a = gfx2_adapter_get(0);
    u32 gen, before = pgalloc_free_pages();
    CHECK(!gfx2_adapter_gen(0, &gen) && gen == a->gen);
    CHECK(a->caps.max_w >= 64 && a->caps.max_h >= 64);
    begin();
    struct gfx2_surf *s = surface(GFX2_POOL_SYSMEM, 64);
    mapped_write(s);
    CHECK(!gfx2_surface_free(s->sid));
    s = surface(GFX2_POOL_SYSMEM, 64);
    void *va; u32 pitch;
    CHECK(!gfx2_lock(s->sid, 0, &va, &pitch)); /* leave locked for AS teardown */
    finish();
    CHECK(pgalloc_free_pages() == before);

    /* Exercise C4/C6/C7/C15 together; only the device hooks are fake. */
    be = gfx2_v1_shim;
    be.alloc = alloc; be.gart_bind = gart_bind; be.gart_unbind = gart_unbind;
    be.fence_wait = wait_fence; be.fence_state = pending;
    a->be = &be;
    begin();
    u32 holes[6];
    for (u32 i = 0; i < 6; i++)
        CHECK(pgalloc_alloc_n_owner(as.owner, 1, 0, DMA_PHYS_LIMIT / PAGE_SIZE,
                                   LEDGER_BOTTOM_UP, &holes[i]));
    for (u32 i = 0; i < 6; i += 2) CHECK(pgalloc_free_n_owner(as.owner, holes[i], 1));
    s = surface(GFX2_POOL_GART, 48); /* three noncontiguous mapped pages */
    const u32 *pfns; u32 n;
    CHECK(!gfx2_gart_pfns(s, &pfns, &n) && n == 3);
    CHECK(pfns[0] == holes[0] && pfns[1] == holes[2] && pfns[2] == holes[4]);
    for (u32 i = 1; i < 6; i += 2) CHECK(pgalloc_free_n_owner(as.owner, holes[i], 1));
    mapped_write(s);
    CHECK(!gfx2_cpu_visible(s)); /* kernel root cannot dereference a GART VA */
    s->last_write = a->fence.issued = 1;
    timeout = 1;
    finish(); /* failure-completed GART backing moves to the quarantine owner */
    CHECK(!bind_n && pgalloc_free_pages() == before);

    begin(); s = surface(GFX2_POOL_SYSMEM, 64);
    CHECK(!gfx2_lock(s->sid, 0, &va, &pitch));
    u32 owner = as.owner;
    u32 di = (s->map_va - MEM_LEASE_BASE) >> PDE_SHIFT;
    ((u32 *)P2V(as.pd_phys))[LEASE_PDE + di] = 0; /* real unmap validation fails */
    CHECK(gfx2_owner_release(&as) == OS32_ERR_IO);
    paging_addrspace_poison(&as); /* X1's existing caller contract */
    CHECK(!s->map_as && s->owner == GFX2_QUAR_OWNER && (s->flags & GFX2_SF_KEEP));
    paging_addrspace_destroy(&as);
    kmemset(&as, 0xcc, sizeof(as)); slot.as = NULL;
    gfx2_closing_sweep(a);
    CHECK(ledger_owner_pages(owner) > 0 && s->state == GFX2_ST_QUAR);
    CHECK(ledger_selfcheck("I1"));
    SAY("GREEN I1 map selftest, registry/caps, SYSMEM/GART maps, locks, audits, AS teardown/quarantine");
    die(0);
}
