#include "types.h"
#include "tvram.h"
static u32 host_cr3;
static void host_sync(u32 root);
static void host_pte_clear(u32 va);
static void *host_surface_pointer(u32 pa);
#define HOST_MMU_LOAD_CHECK(root) host_sync(root)
static u32 host_lease_alloc(u32 owner, int n);
static int host_lease_free(u32 owner, u32 pfn, int n);
#include "paging_host_source.c"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
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
#include "pgalloc_host_source.c"
/* pgalloc_host_source.c は irq_save() を 0 に置き換えてある。 */
#define HOST_POOL_IRQ_SAVE() 0U
#define HOST_POOL_IRQ_RESTORE(f) ((void)(f))
#include "pgalloc_host_fixture.h"
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
#define used used_pages


#include "lease_host_source.c"
void *kmemcpy(void *dst, const void *src, u32 n) {
    u8 *d = dst; const u8 *s = src; while (n--) *d++ = *s++; return dst;
}
/* One timeline for CR3 synchronization, PTE removal and both kinds of free.
 * Seed stale translation when active+closing+last lease is released. */
enum host_event_kind { EV_SYNC, EV_CLEAR, EV_PT_FREE, EV_BACKING_FREE };
static struct { u32 kind, value; } events[PTE_COUNT + 16];
static u32 event_count, watch_pt, watch_backing;
static int watching, stale_translation;
static void event(u32 kind, u32 value) {
    if (!watching) return;
    CHECK(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count].kind = kind; events[event_count++].value = value;
}
static void host_sync(u32 root) {
    event(EV_SYNC, root);
    /* 386 CR3 reload discards all nonglobal translations. */
    stale_translation = 0;
}
static void host_pte_clear(u32 va) { event(EV_CLEAR, va); }
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n) {
    if (watching && (pfn == watch_pt || pfn == watch_backing)) {
        event(pfn == watch_pt ? EV_PT_FREE : EV_BACKING_FREE, pfn);
        if (stale_translation) {
            SAY("ORDER: free before TLB synchronization");
            if (pfn == watch_pt) SAY("ORDER kind: PT");
            else SAY("ORDER kind: backing");
            die(2);
        }
    }
    return host_pgalloc_free_n_owner(owner, pfn, n);
}
/* Emulate the active alias for the padding store only; ledger PFNs remain
 * physical. This makes a removed master guard actually hit the other page. */
static void *host_surface_pointer(u32 pa) {
    u32 *pd = P2V(host_cr3), d = pd[pa >> 22];
    CHECK(d & PTE_PRESENT);
    u32 e = ((u32 *)P2V(d & ~0xfffUL))[(pa >> PAGE_SHIFT) % PTE_COUNT];
    CHECK(e & PTE_PRESENT);
    return P2V((e & ~0xfffUL) | (pa & (PAGE_SIZE - 1)));
}
static u32 watched_roots[2];
static int host_lease_free(u32 owner, u32 pfn, int n) {
    for (u32 a = 0; a < 2; a++) if (watched_roots[a]) {
        u32 *pd = P2V(watched_roots[a]);
        for (u32 k = 0; k < MEM_LEASE_MAX_PDES; k++) {
            u32 d = pd[(MEM_LEASE_BASE >> 22) + k];
            CHECK(!(d & PTE_PRESENT) || (d & ~0xfffUL) != pfn * PAGE_SIZE);
        }
    }
    return pgalloc_free_n_owner(owner, pfn, n);
}
static int fail_after = -1;
static u32 host_lease_alloc(u32 owner, int n) {
    if (!fail_after) return 0;
    if (fail_after > 0) fail_after--;
    return pgalloc_alloc_phys(owner, n);
}
#define SNAP_WORDS (PTE_COUNT * 40)
static u32 images[3][SNAP_WORDS];
static void image_pd(u32 root, u32 bank, int compare) {
    u32 *pd = P2V(root), *image = images[bank], i, j;
    for (i = 0; i < PDE_COUNT; i++) {
        if (compare) CHECK(*image == pd[i]); else *image = pd[i];
        image++;
    }
    for (i = 0; i < PDE_COUNT; i++) if (pd[i] & PTE_PRESENT) {
        u32 *pt = P2V(pd[i] & ~0xfffUL);
        for (j = 0; j < PTE_COUNT; j++) {
            CHECK(image < images[bank] + SNAP_WORDS);
            if (compare) CHECK(*image == pt[j]); else *image = pt[j];
            image++;
        }
    }
}
static u8 ledger_snapshot[sizeof(ledger_owners) + sizeof(ledger_regions) +
    sizeof(ledger_resources) + sizeof(ledger_surfaces) + sizeof(host_pool_storage) +
    PHYSMEM_LEGACY_MAX_PFN];
static void snapshot_bytes(u32 *pos, const void *ptr, u32 n, int compare) {
    const u8 *bytes = ptr;
    for (u32 i = 0; i < n; i++, (*pos)++) {
        CHECK(*pos < sizeof(ledger_snapshot));
        if (compare) CHECK(ledger_snapshot[*pos] == bytes[i]);
        else ledger_snapshot[*pos] = bytes[i];
    }
}
static void snapshot_ledger(int compare) {
    u32 pos = 0;
#define SNAP(table) snapshot_bytes(&pos, table, sizeof(table), compare)
    SNAP(ledger_owners); SNAP(ledger_regions); SNAP(ledger_resources);
    SNAP(ledger_surfaces); SNAP(host_pool_storage);
#undef SNAP
    snapshot_bytes(&pos, owner_map, limit_pfn, compare);
}
void _start(void) {
    u32 args[6] = {MEM_POOL_BASE, 0x4000000, 3, 0x32, 0xffffffffUL, 0}, result;
    struct addrspace a, b;
    u32 oa, ob, owner, sid, pa, before, i, count;
    struct ledger_surface sf = {.width=1, .height=1, .pitch=1, .planes=1,
        .npages=1, .backing=LEDGER_SB_RAM, .cache=LEDGER_CACHE_WB,
        .backend=LEDGER_SF_PC98, .role=LEDGER_ROLE_CLIENT, .perm_max=LEDGER_PERM_RW};
    struct surface_ref refs[MEM_LEASE_MAX];
    struct lease_view v[MEM_LEASE_MAX], out[MEM_LEASE_MAX];
    struct lease_authority auth;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == MEM_POOL_BASE);
    host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    before = used_pages;
    CHECK(!lease_selftest()); CHECK(used_pages == before);
    CHECK(ledger_owner_new(LEDGER_KIND_AS,0,"A",&oa));
    CHECK(ledger_owner_new(LEDGER_KIND_AS,0,"B",&ob));
    CHECK(ledger_owner_new(LEDGER_KIND_AS,0,"SF",&owner));
    /* Initial PT exhaustion rolls back PD/app PT too. */
    fail_after=1;
    CHECK(paging_addrspace_create_lease(&a,oa)); CHECK(used_pages==before);
    fail_after=-1;
    CHECK(!paging_addrspace_create_lease(&a,oa));
    CHECK(!paging_addrspace_create_lease(&b,ob));
    watched_roots[0]=a.pd_phys; watched_roots[1]=b.pd_phys;
    CHECK(ledger_owner_pages(oa)==2 && a.lease_pt_phys[0]);
    pa=pgalloc_alloc_phys(owner,1); CHECK(pa); sf.owner=(u8)owner; sf.first=pa/PAGE_SIZE;
    for(i=0;i<PAGE_SIZE;i++) ((u8 *)P2V(pa))[i]=0xab;
    /* Nonidentity active AS: pa aliases another owned page. Rejection leaves
     * backing, alias, output, all ledger tables and allocator metadata intact. */
    u32 alias = pgalloc_alloc_phys(owner, 1); CHECK(alias);
    for (i = 0; i < PAGE_SIZE; i++) ((u8 *)P2V(alias))[i] = 0xcd;
    CHECK(!paging_addrspace_map_user(&a, pa, alias, PAGE_RW | PTE_USER));
    snapshot_ledger(0); count = used_pages; sid = 0x11223344;
    paging_load_cr3(a.pd_phys);
    CHECK(!ledger_surface_create(&sf, &sid));
    CHECK(paging_current_cr3() == a.pd_phys && sid == 0x11223344);
    snapshot_ledger(1); CHECK(used_pages == count);
    for (i = 0; i < PAGE_SIZE; i++) {
        CHECK(((u8 *)P2V(pa))[i] == 0xab);
        CHECK(((u8 *)P2V(alias))[i] == 0xcd);
    }
    paging_load_cr3(paging_kernel_pd_phys());
    CHECK(!paging_addrspace_map_user(&a, pa, pa, PAGE_RW));
    CHECK(pgalloc_free_n_owner(owner, alias / PAGE_SIZE, 1));
    CHECK(ledger_surface_create(&sf,&sid));
    CHECK(((u8 *)P2V(pa))[0]==0xab && ((u8 *)P2V(pa))[PAGE_SIZE-1]==0);
    CHECK(!pgalloc_free_n_owner(owner,pa/PAGE_SIZE,1));
    sf.plane_offset[0]=PAGE_SIZE; CHECK(!ledger_surface_validate(&sf)); sf.plane_offset[0]=0;
    sf.cache=LEDGER_CACHE_UC; CHECK(!ledger_surface_validate(&sf)); sf.cache=LEDGER_CACHE_WB;
    auth.owner=oa; auth.backend=sf.backend; auth.role=sf.role;
    for(i=0;i<MEM_LEASE_MAX;i++) refs[i]=(struct surface_ref){sid,ledger_surfaces[sid].gen};
    image_pd(paging_kernel_pd_phys(),0,0); image_pd(b.pd_phys,1,0);
    for(i=0;i<sizeof(out)/(sizeof(u32));i++) ((u32 *)out)[i]=0x11223344;
    refs[0].generation--; CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_INVAL); refs[0].generation++;
    auth.role=LEDGER_ROLE_DISPLAY; CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_INVAL); auth.role=sf.role;
    CHECK(((u32 *)out)[0]==0x11223344);
    ledger_surfaces[sid].perm_max=LEDGER_PERM_RO;
    CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_INVAL);
    ledger_surfaces[sid].perm_max=LEDGER_PERM_RW;
    ledger_surfaces[sid].lease_count=0xffff;
    CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_FULL);
    CHECK(ledger_surfaces[sid].lease_count==0xffff); ledger_surfaces[sid].lease_count=0;
    CHECK(!lease_acquire(&a,&auth,refs,MEM_LEASE_MAX,LEDGER_PERM_RO,v));
    CHECK(ledger_surfaces[sid].lease_count==MEM_LEASE_MAX);
    CHECK(!(paging_lease_pte(&a,v[0].base)&PTE_RW));
    image_pd(a.pd_phys,2,0); count=used_pages;
    CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_FULL);
    CHECK(used_pages==count); image_pd(a.pd_phys,2,1);
    image_pd(paging_kernel_pd_phys(),0,1); image_pd(b.pd_phys,1,1);
    CHECK(!lease_check(&a));
    CHECK(!pgalloc_free_n_owner(owner,pa/PAGE_SIZE,1));
    CHECK(lease_release(&b,v[0].token)==LEASE_INVAL);
    CHECK(ledger_surface_release(sid)); /* pending return keeps backing */
    CHECK(ledger_surfaces[sid].closing && pgalloc_page_owned(pa/PAGE_SIZE,owner));
    CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_FULL);
    CHECK(!lease_revoke_all(&a)); CHECK(!ledger_surfaces[sid].npages);
    CHECK(!pgalloc_page_owned(pa/PAGE_SIZE,owner));
    /* Four discontinuous surface references form one all-or-nothing bundle. */
    pa=pgalloc_alloc_phys(owner,8); CHECK(pa);
    for(i=0;i<4;i++) {
        sf.first=pa/PAGE_SIZE+2*i;
        CHECK(ledger_surface_create(&sf,&refs[i].sid));
        refs[i].generation=ledger_surfaces[refs[i].sid].gen;
    }
    image_pd(a.pd_phys,2,0); count=used_pages;
    refs[3].generation--;
    CHECK(lease_acquire(&a,&auth,refs,4,LEDGER_PERM_RW,out)==LEASE_INVAL);
    CHECK(used_pages==count); image_pd(a.pd_phys,2,1); refs[3].generation++;
    CHECK(!lease_acquire(&a,&auth,refs,4,LEDGER_PERM_RW,v));
    for(i=0;i<4;i++) CHECK((paging_lease_pte(&a,v[i].base)&~0xfffUL)==pa+2*i*PAGE_SIZE);
    CHECK(!lease_revoke_all(&a));
    for(i=0;i<4;i++) {
        CHECK(ledger_surface_release(refs[i].sid));
        CHECK(pgalloc_free_n_owner(owner,pa/PAGE_SIZE+2*i+1,1));
    }
    /* More than one PT; second prepared plane crosses the following PDE. */
    pa=pgalloc_alloc_phys(owner,PTE_COUNT+1); CHECK(pa);
    sf.npages=PTE_COUNT+1; sf.first=pa/PAGE_SIZE;
    CHECK(ledger_surface_create(&sf,&sid));
    refs[0]=(struct surface_ref){sid,ledger_surfaces[sid].gen}; refs[1]=refs[0];
    image_pd(a.pd_phys,2,0); count=used_pages;
    fail_after=1;
    CHECK(lease_acquire(&a,&auth,refs,2,LEDGER_PERM_RW,out)==LEASE_NOMEM);
    fail_after=-1;
    CHECK(used_pages==count && ledger_surfaces[sid].lease_count==0);
    image_pd(a.pd_phys,2,1); image_pd(paging_kernel_pd_phys(),0,1); image_pd(b.pd_phys,1,1);
    CHECK(!lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,v));
    CHECK(a.lease_pt_phys[1] && !lease_check(&a));
    paging_load_cr3(a.pd_phys);
    CHECK(!lease_check(&a)); CHECK(paging_current_cr3()==a.pd_phys);
    CHECK(ledger_surface_release(sid));
    CHECK(ledger_surfaces[sid].closing && ledger_surfaces[sid].lease_count == 1);
    watch_pt = a.lease_pt_phys[1] / PAGE_SIZE; watch_backing = pa / PAGE_SIZE;
    event_count = 0; watching = stale_translation = 1;
    CHECK(!lease_release(&a,v[0].token)); CHECK(paging_current_cr3()==a.pd_phys);
    watching = 0;
    CHECK(event_count == PTE_COUNT + 5);
    CHECK(events[0].kind == EV_SYNC && events[0].value == paging_kernel_pd_phys());
    for (i = 0; i < PTE_COUNT + 1; i++) {
        CHECK(events[i + 1].kind == EV_CLEAR);
        CHECK(events[i + 1].value == v[0].base + i * PAGE_SIZE);
    }
    CHECK(events[PTE_COUNT + 2].kind == EV_PT_FREE);
    CHECK(events[PTE_COUNT + 2].value == watch_pt);
    CHECK(events[PTE_COUNT + 3].kind == EV_BACKING_FREE);
    CHECK(events[PTE_COUNT + 3].value == watch_backing);
    CHECK(events[PTE_COUNT + 4].kind == EV_SYNC && events[PTE_COUNT + 4].value == a.pd_phys);
    CHECK(!ledger_surfaces[sid].npages && !ledger_surfaces[sid].lease_count);
    CHECK(!pgalloc_page_owned(watch_backing, owner));
    SAY("active closing last lease: sync -> PTE clear -> PT free -> backing free -> CR3 restore PASS");
    paging_load_cr3(paging_kernel_pd_phys());
    CHECK(!a.lease_pt_phys[1] && a.lease_pt_phys[0]);
    /* UC backing and mismatch with supervisor alias. */
    sf.first=MEM_LEASE_END/PAGE_SIZE; sf.npages=1; sf.backing=LEDGER_SB_MMIO; sf.cache=LEDGER_CACHE_UC;
    ledger_resources[0].map_first=sf.first; ledger_resources[0].map_end=sf.first+1;
    ledger_regions[ledger_region_count++]=(struct ledger_region){.first=sf.first,.end=sf.first+1,
        .type=LEDGER_R_DEVICE,.owner=LEDGER_OWNER_GFX,.cache=LEDGER_CACHE_UC,.res_mask=1};
    CHECK(!paging_map_phys(MEM_LEASE_END,MEM_LEASE_END,1,PAGE_RW|PTE_PCD));
    CHECK(ledger_surface_create(&sf,&sid)); refs[0]=(struct surface_ref){sid,ledger_surfaces[sid].gen};
    CHECK(!lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,v));
    CHECK(paging_lease_pte(&a,v[0].base)&PTE_PCD); CHECK(!lease_check(&a));
    CHECK(!lease_revoke_all(&a));
    page_tables[PAGING_APERTURE_PDI][0]&=~PTE_PCD;
    CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_INVAL);
    page_tables[PAGING_APERTURE_PDI][0]|=PTE_PCD;
    CHECK(ledger_surface_release(sid));
    ledger_surfaces[sid].gen=0xffffffffUL;
    i=sid; CHECK(ledger_surface_create(&sf,&sid)); CHECK(sid!=i && ledger_surfaces[sid].gen);
    CHECK(ledger_surface_release(sid));
    /* Native VRAM uses the permanent FIXED/UC ledger, not a PCI resource. */
    sf.owner=LEDGER_OWNER_KERNEL; sf.first=TVRAM_BASE/PAGE_SIZE;
    sf.backing=LEDGER_SB_VRAM; sf.role=LEDGER_ROLE_DISPLAY; auth.role=sf.role;
    ledger_regions[ledger_region_count++]=(struct ledger_region){.first=sf.first,.end=sf.first+1,
        .type=LEDGER_R_FIXED,.owner=LEDGER_OWNER_KERNEL,.cache=LEDGER_CACHE_UC};
    CHECK(ledger_surface_create(&sf,&sid)); refs[0]=(struct surface_ref){sid,ledger_surfaces[sid].gen};
    CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_INVAL); /* old alias is WB */
    CHECK(!paging_map_phys(TVRAM_BASE,TVRAM_BASE,1,PAGE_RW|PTE_PCD));
    CHECK(!lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,v));
    CHECK(!lease_revoke_all(&a)); CHECK(ledger_surface_release(sid));
    sf.owner=(u8)owner; sf.role=LEDGER_ROLE_CLIENT; auth.role=sf.role;
    lease_next_token=0xffffffffUL;
    pa=pgalloc_alloc_phys(owner,1); sf.first=pa/PAGE_SIZE; sf.backing=LEDGER_SB_RAM; sf.cache=LEDGER_CACHE_WB;
    CHECK(ledger_surface_create(&sf,&sid)); refs[0]=(struct surface_ref){sid,ledger_surfaces[sid].gen}; refs[1]=refs[0];
    CHECK(lease_acquire(&a,&auth,refs,2,LEDGER_PERM_RW,out)==LEASE_FULL);
    CHECK(!lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,v)); CHECK(!lease_next_token);
    CHECK(!lease_revoke_all(&a)); CHECK(lease_acquire(&a,&auth,refs,1,LEDGER_PERM_RW,out)==LEASE_FULL);
    CHECK(ledger_surface_release(sid));
    watched_roots[0]=watched_roots[1]=0;
    paging_addrspace_destroy(&a); paging_addrspace_destroy(&b);
    CHECK(ledger_owner_retire(oa) && ledger_owner_retire(ob) && ledger_owner_retire(owner));
    CHECK(used_pages==before);
    SAY("T2b lease runtime PASS"); die(0);
}
