#include "types.h"
static u32 host_cr3;
#include "paging_host_source.c"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static u32 limit = 128, calls;
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *text, u32 len)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len) : "memory");
}
#define SAY(s) report(s "\n", sizeof(s "\n") - 1)
#define CHECK(x) do { if (!(x)) { SAY("FAIL: " #x); die(1); } } while (0)
#define pgalloc_alloc_phys actual_alloc_phys
#define pgalloc_alloc_pt actual_alloc_pt
#define pgalloc_free_pt actual_free_pt
#include "pgalloc_host_source.c"
#undef pgalloc_alloc_phys
#undef pgalloc_alloc_pt
#undef pgalloc_free_pt
/* pgalloc_host_source.c は irq_save() を 0 に置き換えてある。 */
#define HOST_POOL_IRQ_SAVE() 0U
#define HOST_POOL_IRQ_RESTORE(f) ((void)(f))
#include "pgalloc_host_fixture.h"
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
/* master の動的 PT は workspace からだけ来る (T1a で legacy の探索を撤去)。
 * 使用中の workspace ページ数を数え、used に含める。 */
static u32 ws_used;
/* AS の PD / PT (paging_addrspace_create_n) の確保。枯渇の注入だけで、
 * calls (master の PT の要求回数) には数えない。 */
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    if (used_pages + ws_used >= limit) return 0;
    return actual_alloc_phys(owner, n);
}
u32 pgalloc_alloc_pt(void)
{
    u32 r;
    calls++;
    if (used_pages + ws_used >= limit) return 0;
    r = actual_alloc_pt();
    if (r) ws_used++;
    return r;
}
void pgalloc_free_pt(u32 phys)
{
    u32 p = phys / PAGE_SIZE;
    if (p >= workspace_first && p < workspace_end && bit(workspace_used, p)) ws_used--;
    actual_free_pt(phys);
}
#define used (used_pages + ws_used)
/* 試験用の池の workspace: 16MiB の末尾 16 ページ (恒等写像済み、下で mmap)。 */
#define HOST_WS_FIRST (0x1000000UL / PAGE_SIZE - 16)
#define HOST_WS_END   (0x1000000UL / PAGE_SIZE)

#include "shlib_host_source.c"
#include "access_walk_host_source.c"
#include "redir_access_host_source.c"
static AppSlot slot;
static int current = 2;
volatile int ring3_wm_depth;
int appslot_cur(void) { return current; }
int res_owner_get(void) { return current; }
int ring3_call_from_user(void) { return 1; }
AppSlot *appslot_get(int id) { return id == 2 && slot.state ? &slot : 0; }
u32 exec_tramp_page_addr(void) { return 0x170000; }
void *kmemcpy(void *d, const void *s, u32 n) {
    u8 *out = d; const u8 *in = s; while (n--) *out++ = *in++; return d;
}
static struct addrspace space;
static struct caller_access caller;
static u32 payload;
static void probe(u32 va, int write, int ok, u32 expected)
{
    u32 pa = 0x12345678, root = host_cr3, f = host_arch_if;
    unsigned int saved = irq_save();
    CHECK(caller_access_page(&caller, va, write, &pa) == ok);
    irq_restore(saved);
    CHECK(pa == (ok ? expected : 0x12345678));
    CHECK(host_cr3 == root && host_arch_if == f);
}
#ifdef HOST_CALLER_COPY_TEST
static void caller_copy_tests(void);
#endif
void _start(void)
{
    u32 args[6] = {0x100000, 0xF00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, owner, other, *pd, *pt, original, fake, i;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x100000);
    host_map_fixed_paging();
    paging_init(16384);
    host_pool_boot_ws(16384, HOST_WS_FIRST, HOST_WS_END);
    CHECK(ledger_register_region(LEDGER_R_SURFACE_BACKING, LEDGER_OWNER_KERNEL,
          MEM_GFX_BB_BASE / PAGE_SIZE, MEM_GFX_BB_BASE / PAGE_SIZE + 1, LEDGER_CACHE_WB, 0));
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "d3", &owner));
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 3, "other", &other));
    CHECK(!paging_addrspace_create_lease(&space, owner));
    payload = pgalloc_alloc_phys(owner, 2);
    fake = pgalloc_alloc_phys(other, 1);
    CHECK(payload && fake);
    CHECK(!paging_addrspace_map_user(&space, MEM_EXEC_LOAD_ADDR, payload, PAGE_RW | PTE_USER));
    slot.state = APP_STATE_RUNNING; slot.cpl3 = 1; slot.as = &space;
    host_cr3 = space.pd_phys;
    caller = (struct caller_access){CALLER_USER, 2, &space, space.pd_phys, owner, space.generation};
    pd = P2V(space.pd_phys); pt = P2V(space.app_pt_phys[0]);
#ifdef HOST_CALLER_COPY_TEST
    caller_copy_tests();
#endif
    for (i = 0; i < 2; i++) {
        host_arch_if = i ? 0x202 : 2;
        probe(MEM_EXEC_LOAD_ADDR + 13, 0, 1, payload + 13);
        probe(MEM_EXEC_LOAD_ADDR + 13, 1, 1, payload + 13);
        probe(MEM_EXEC_LOAD_ADDR + PAGE_SIZE, 0, 0, 0);
    }
    original = pt[(MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT];
    pt[(MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT] &= ~PTE_RW;
    probe(MEM_EXEC_LOAD_ADDR, 0, 1, payload);
    probe(MEM_EXEC_LOAD_ADDR, 1, 0, 0);
    pt[(MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT] = original;
    for (i = 0; i < 3; i++) {
        u32 bit = i == 0 ? PTE_PRESENT : i == 1 ? PTE_USER : PTE_RW;
        pd[APP_BAND_PDE] &= ~bit;
        probe(MEM_EXEC_LOAD_ADDR, 1, 0, 0);
        probe(MEM_EXEC_LOAD_ADDR, 0, i == 2, payload);
        pd[APP_BAND_PDE] |= bit;
        pt[(MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT] &= ~bit;
        probe(MEM_EXEC_LOAD_ADDR, 1, 0, 0);
        pt[(MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT] = original;
    }
    pd[APP_BAND_PDE] |= PTE_PS;
    probe(MEM_EXEC_LOAD_ADDR, 0, 0, 0);
    pd[APP_BAND_PDE] &= ~PTE_PS;
    /* Fake table contains a valid PTE and is present RAM. */
    ((u32 *)P2V(fake))[(MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT] = original;
    pd[APP_BAND_PDE] = fake | PAGE_RW | PTE_USER;
    probe(MEM_EXEC_LOAD_ADDR, 0, 0, 0);
    space.app_pt_phys[0] = fake;
    probe(MEM_EXEC_LOAD_ADDR, 0, 0, 0);
    space.app_pt_phys[0] = V2P(pt);
    pd[APP_BAND_PDE] = V2P(pt) | PAGE_RW | PTE_USER;
    CHECK(ledger_transfer(space.pd_phys / PAGE_SIZE, 1, owner, other));
    probe(MEM_EXEC_LOAD_ADDR, 0, 0, 0);
    CHECK(ledger_transfer(space.pd_phys / PAGE_SIZE, 1, other, owner));
    for (i = 0; i < 4; i++) {
        u32 bad = i == 0 ? fake : i == 1 ? space.pd_phys :
                  i == 2 ? space.app_pt_phys[0] : space.lease_pt_phys[0];
        pt[(MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT] = bad | PAGE_RW | PTE_USER;
        probe(MEM_EXEC_LOAD_ADDR, 0, 0, 0);
    }
    pt[(MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT] = original;
    /* Shared SHM/trampoline are identity only; arbitrary kernel/VRAM denied. */
    host_cr3 = paging_kernel_pd_phys();
    CHECK(!paging_addrspace_map_user(&space, MEM_SHM_BASE, MEM_SHM_BASE, PAGE_RW | PTE_USER));
    CHECK(!paging_addrspace_map_user(&space, exec_tramp_page_addr(), exec_tramp_page_addr(), PAGE_RO | PTE_USER));
    CHECK(!paging_addrspace_map_user(&space, 0xA0000, 0xA0000, PAGE_RW | PTE_USER));
    /* Cirrus identity USER window is above SHM_END, but is not copy payload.
     * Keep a valid shared master/registered PT so only the band rejects it. */
    CHECK(MEM_DEVICE_APERTURE_BASE > MEM_SHM_END);
    CHECK(!paging_addrspace_map_user(&space, MEM_DEVICE_APERTURE_BASE,
          MEM_DEVICE_APERTURE_BASE, PAGE_RW | PTE_USER));
    host_cr3 = space.pd_phys;
    CHECK(!as_va_to_pa(space.pd_phys, MEM_DEVICE_APERTURE_BASE, &result));
    CHECK(result == MEM_DEVICE_APERTURE_BASE);
    probe(MEM_DEVICE_APERTURE_BASE, 0, 0, 0);
    probe(MEM_DEVICE_APERTURE_BASE, 1, 0, 0);
    probe(MEM_SHM_BASE + 7, 1, 1, MEM_SHM_BASE + 7);
    probe(exec_tramp_page_addr() + 17, 0, 1, exec_tramp_page_addr() + 17);
    probe(exec_tramp_page_addr(), 1, 0, 0);
    probe(0xA0000, 0, 0, 0);
    original = pd[0]; pd[0] = fake | PAGE_RW | PTE_USER;
    probe(MEM_SHM_BASE, 0, 0, 0); pd[0] = original;
    /* Master PDE is not itself a registry: reject even a forged pair. */
    original = page_directory[0];
    page_directory[0] = pd[0] = fake | PAGE_RW | PTE_USER;
    ((u32 *)P2V(fake))[(MEM_SHM_BASE >> PAGE_SHIFT) % PTE_COUNT] = MEM_SHM_BASE | PAGE_RW | PTE_USER;
    probe(MEM_SHM_BASE, 0, 0, 0);
    page_directory[0] = original; pd[0] = original | PTE_USER;
    probe(MEM_SHM_BASE, 0, 1, MEM_SHM_BASE);
    {
        u32 *shared = page_tables[0], idx = (MEM_SHM_BASE >> PAGE_SHIFT) % PTE_COUNT;
        u32 old = shared[idx]; shared[idx] = payload | PAGE_RW | PTE_USER;
        probe(MEM_SHM_BASE, 0, 0, 0); shared[idx] = old;
        idx = exec_tramp_page_addr() / PAGE_SIZE;
        shared[idx] |= PTE_RW;
        probe(exec_tramp_page_addr(), 0, 0, 0);
        probe(exec_tramp_page_addr(), 1, 0, 0);
        shared[idx] &= ~PTE_RW;
    }
    /* Real shlib registry: exact page, live ledger, RO input only. */
    g_loaded = 1; g_text_pages = 2;
    g_pages[0] = pgalloc_alloc_phys(LEDGER_OWNER_SHLIB, 1);
    g_pages[1] = pgalloc_alloc_phys(LEDGER_OWNER_SHLIB, 1);
    pt[0] = g_pages[0] | PAGE_RO | PTE_USER;
    probe(MEM_SHLIB_BASE, 0, 1, g_pages[0]);
    probe(MEM_SHLIB_BASE, 1, 0, 0);
    pt[0] |= PTE_RW; probe(MEM_SHLIB_BASE, 1, 0, 0);
    pt[0] = g_pages[1] | PAGE_RO | PTE_USER;
    probe(MEM_SHLIB_BASE, 0, 0, 0);
    /* RAM lease: exact surface generation/backing, even when closing. */
    {
        struct ledger_surface sf = {.first = fake / PAGE_SIZE, .npages = 1,
            .owner = other, .backing = LEDGER_SB_RAM, .width = 1, .height = 1,
            .pitch = 1, .planes = 1, .backend = LEDGER_SF_PC98,
            .role = LEDGER_ROLE_CLIENT, .perm_max = LEDGER_PERM_RW};
        u32 sid;
        host_cr3 = paging_kernel_pd_phys();
        CHECK(ledger_surface_create(&sf, &sid));
        host_cr3 = space.pd_phys;
        struct ledger_surface *live = &ledger_surfaces[sid];
        struct as_lease *l = &space.leases[0];
        *l = (struct as_lease){1, sid, live->gen, MEM_LEASE_BASE, 1, PAGE_RO | PTE_USER};
        live->lease_count = 1;
        u32 *lp = P2V(space.lease_pt_phys[0]);
        lp[0] = fake | PAGE_RO | PTE_USER;
        probe(MEM_LEASE_BASE, 0, 1, fake);
        probe(MEM_LEASE_BASE, 1, 0, 0);
        lp[0] |= PTE_RW; probe(MEM_LEASE_BASE, 1, 0, 0);
        l->flags |= PTE_RW; probe(MEM_LEASE_BASE, 1, 1, fake);
        live->closing = 1; probe(MEM_LEASE_BASE, 0, 1, fake);
        live->gen++; probe(MEM_LEASE_BASE, 0, 0, 0); live->gen--;
        l->token = 0; probe(MEM_LEASE_BASE, 0, 0, 0); l->token = 1;
        {
            struct ledger_surface keep = *live;
            u32 mmio = MEM_DEVICE_APERTURE_BASE / PAGE_SIZE;
            ledger_regions[ledger_region_count++] = (struct ledger_region){
                .first = mmio, .end = mmio + 1, .type = LEDGER_R_DEVICE,
                .owner = LEDGER_OWNER_GFX, .cache = LEDGER_CACHE_UC, .res_mask = 1};
            ledger_resources[0].map_first = mmio;
            ledger_resources[0].map_end = mmio + 1;
            live->first = mmio; live->owner = LEDGER_OWNER_GFX;
            live->cache = LEDGER_CACHE_UC;
            lp[0] = mmio * PAGE_SIZE | PAGE_RW | PTE_USER | PTE_PCD;
            for (u32 kind = LEDGER_SB_VRAM; kind <= LEDGER_SB_MMIO; kind++) {
                live->backing = kind;
                CHECK(ledger_surface_validate(live));
                probe(MEM_LEASE_BASE, 0, 0, 0);
                probe(MEM_LEASE_BASE, 1, 0, 0);
            }
            *live = keep;
        }
        lp[0] = payload | PAGE_RW | PTE_USER; probe(MEM_LEASE_BASE, 0, 0, 0);
        live->backing = LEDGER_SB_FIXED_RAM; live->owner = LEDGER_OWNER_KERNEL;
        live->first = MEM_GFX_BB_BASE / PAGE_SIZE;
        lp[0] = MEM_GFX_BB_BASE | PAGE_RW | PTE_USER;
        probe(MEM_LEASE_BASE, 0, 1, MEM_GFX_BB_BASE);
        probe(MEM_LEASE_BASE, 1, 1, MEM_GFX_BB_BASE);
        live->perm_max = LEDGER_PERM_RO; probe(MEM_LEASE_BASE, 1, 0, 0);
        probe(MEM_LEASE_BASE, 0, 1, MEM_GFX_BB_BASE);
        live->first++; lp[0] += PAGE_SIZE; probe(MEM_LEASE_BASE, 0, 0, 0);
    }
    /* Current caller is stricter than a live registrant under another root. */
    host_cr3 = paging_kernel_pd_phys();
    for (i = 0; i < 2; i++) {
        u32 root = host_cr3;
        host_arch_if = i ? 0x202 : 2;
        probe(MEM_EXEC_LOAD_ADDR, 0, 0, 0);
        probe(MEM_EXEC_LOAD_ADDR, 1, 0, 0);
        CHECK(redir_access_check(&caller, MEM_EXEC_LOAD_ADDR, 1, 0));
        CHECK(redir_access_check(&caller, MEM_EXEC_LOAD_ADDR, 1, 1));
        CHECK(host_cr3 == root && host_arch_if == (i ? 0x202U : 2U));
    }
    host_cr3 = space.pd_phys;
    current = 3; probe(MEM_EXEC_LOAD_ADDR, 0, 0, 0); current = 2;
    caller.generation++; probe(MEM_EXEC_LOAD_ADDR, 0, 0, 0);
    SAY("PASS: d3 real managed walk, PFN classes, caller/registrant, IF/CR3/output");
    die(0);
}
