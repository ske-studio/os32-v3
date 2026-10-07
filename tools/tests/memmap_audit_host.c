/* Real paging/ledger/lease/gfx and the counted lifecycle check from kselftest. */
unsigned int host_arch_if = 0x202U;
#define E10C_REAL_AUDIT
#define run e4_run
#include "gfx_kernel_fb_host.c"
#undef run
static int ksel_fail, ksel_pass;
static void check(int ok, const char *why) { (void)why; if (ok) ksel_pass++; else ksel_fail++; }
static char serial_line[128];
static u32 serial_len;
static int serial_session;
int serial_gate_active(void) { return serial_session; }
void serial_puts_polled(const char *s)
{ while (*s && serial_len+1 < sizeof(serial_line)) serial_line[serial_len++]=*s++; }
char *kstrncpy(char *d, const char *s, u32 n)
{ char *ret=d; if (n) { while (--n && *s) *d++=*s++; *d=0; } return ret; }
#include "audit_slice.inc"
#include "system_surface.h"
int utf8_jis_table_ready(void) { return 1; }
int host_map_fail;
static u32 scan_total[2], scan_interval[2], scan_max[2];
void host_scan(int kind)
{
    scan_total[kind]++;
    if (!(host_arch_if & 0x200U)) scan_interval[kind]++;
}
void host_irq_end(unsigned int flags)
{
    if (!(flags & 0x200U)) return;
    for (int i = 0; i < 2; i++) {
        if (scan_interval[i] > scan_max[i]) scan_max[i] = scan_interval[i];
        scan_interval[i] = 0;
    }
}
static void reset_scans(void)
{
    for (int i = 0; i < 2; i++) scan_total[i] = scan_interval[i] = scan_max[i] = 0;
}
static void run(void) __attribute__((used));
static void run(void)
{
    map_host(0x10000, 0xFF0000);
    map_host(WAB_XE10_LINEARWIN_BASE, WAB_XE10_LINEARWIN_SIZE);
#ifdef TEST_MAP_FAILURE
    /* New boot: fail one device mapping while retaining its decode reservation. */
    boot(); gfx_set_backend_pref(GFX_PREF_AUTO); host_map_fail = 1;
    gfx_boot_reserve(); host_map_fail = 0;
    paging_reclaim_conventional();
    paging_set_not_present(MEM_SHM_GUARD_LO, MEM_SHM_GUARD_LO + PAGE_SIZE - 1);
    paging_set_not_present(MEM_SHM_GUARD_HI, MEM_SHM_GUARD_HI + PAGE_SIZE - 1);
    CHECK(!paging_boot_user_shared(exec_tramp_page_addr()));
    paging_prepare_legacy_clients();
    CHECK(!ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_DISPLAY));
    CHECK(!paging_master_audit(exec_tramp_page_addr()));
    gfx_prepare_backend();
    CHECK(gfx_sf_backend() == LEDGER_SF_CIRRUS && gfx_selected_selfcheck());
    CHECK(fixed_paging_valid()); /* Cirrus CLIENT permits the transitional USER PDE. */
    SAY("PASS failed-map/selected-Cirrus");
    die(0);
#endif
    boot(); gfx_set_backend_pref(GFX_PREF_PC98); gfx_boot_reserve();
    paging_reclaim_conventional();
    paging_set_not_present(MEM_SHM_GUARD_LO, MEM_SHM_GUARD_LO + PAGE_SIZE - 1);
    paging_set_not_present(MEM_SHM_GUARD_HI, MEM_SHM_GUARD_HI + PAGE_SIZE - 1);
    CHECK(!paging_boot_user_shared(exec_tramp_page_addr()));
    paging_prepare_legacy_clients();
    CHECK(!paging_master_audit(exec_tramp_page_addr()));
    u32 *pt = page_tables[TVRAM_CHAR_BASE >> 22];
    u32 idx = TVRAM_CHAR_BASE / PAGE_SIZE, saved = pt[idx];
    /* b1: low font and native VRAM must never regain USER. */
    for (u32 a = MEM_FONT_CACHE_BASE; a < MEM_UNICODE_TABLE_BASE; a += PAGE_SIZE) {
        pt[a / PAGE_SIZE] |= PTE_USER;
        CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
        pt[a / PAGE_SIZE] &= ~PTE_USER;
    }
    for (u32 a = TVRAM_CHAR_BASE; a < GVRAM_BRG_END; a += PAGE_SIZE) {
        pt[a / PAGE_SIZE] |= PTE_USER;
        CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
        pt[a / PAGE_SIZE] &= ~PTE_USER;
    }
    pt[idx] &= ~PTE_PCD;
    CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
    pt[idx] = saved;
    pt[idx] ^= PAGE_SIZE;
    CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
    pt[idx] = saved;
    pt[MEM_DMA_POOL_BASE / PAGE_SIZE] |= PTE_USER;
    CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
    pt[MEM_DMA_POOL_BASE / PAGE_SIZE] &= ~PTE_USER;
    CHECK(!paging_master_audit(exec_tramp_page_addr()));
    page_directory[PAGING_APERTURE_PDI] |= PTE_USER;
    CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
    page_directory[PAGING_APERTURE_PDI] &= ~PTE_USER;
    gfx_prepare_backend();
    CHECK(!kselftest_run_post_probe());
    u8 *saved_bb = bb[0]; bb[0] = 0;
    CHECK(!gfx_selected_selfcheck()); bb[0] = saved_bb;
    struct ledger_surface *selected = ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_DISPLAY);
    gfx_surface_unready |= 1U << (selected - ledger_surfaces);
    CHECK(!gfx_selected_selfcheck());
    gfx_surface_unready &= ~(1U << (selected - ledger_surfaces));
    u32 owner;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "audit", &owner));
    CHECK(!paging_addrspace_create_lease(&space, owner));
    slot.as = &space;
    reset_scans();
    CHECK(!lease_audit_all());
    CHECK(scan_max[0] == PTE_COUNT); /* empty AS has only the mandatory lease PT */
    /* Exercise all allocated PTs, including unexpected PTEs outside a lease. */
    u32 *allpd = P2V(space.pd_phys);
    for (u32 i = 0; i < MEM_APP_BAND_MAX_PDES; i++) {
        space.app_pt_phys[i] = pgalloc_alloc_phys(owner, 1);
        CHECK(space.app_pt_phys[i]);
        kmemset(P2V(space.app_pt_phys[i]), 0, PAGE_SIZE);
        allpd[APP_BAND_PDE+i] = space.app_pt_phys[i] | PAGE_RW | PTE_USER;
    }
    for (u32 i = 1; i < MEM_LEASE_MAX_PDES; i++) {
        space.lease_pt_phys[i] = pgalloc_alloc_phys(owner, 1);
        CHECK(space.lease_pt_phys[i]);
        kmemset(P2V(space.lease_pt_phys[i]), 0, PAGE_SIZE);
        allpd[(MEM_LEASE_BASE >> 22)+i] = space.lease_pt_phys[i] | PAGE_RW | PTE_USER;
    }
    host_slots[3].as = &space; /* second AS scan must get a distinct IRQ interval */
    reset_scans();
    CHECK(!lease_audit_all());
    CHECK(scan_max[0] == 122880U);
    CHECK(scan_max[0] == (MEM_APP_BAND_MAX_PDES + MEM_LEASE_MAX_PDES) * PTE_COUNT);
    host_slots[3].as = 0;
    u32 *tail = P2V(space.lease_pt_phys[MEM_LEASE_MAX_PDES-1]);
    tail[PTE_COUNT-1] = PAGE_RO | PTE_USER;
    CHECK(lease_check(&space) == LEASE_INVAL);
    tail[PTE_COUNT-1] = 0;
    reset_scans();
    CHECK(ledger_selfcheck("bounded"));
    CHECK(scan_max[1] == pgalloc_limit_pfn() && scan_max[1] <= PHYSMEM_MAX_PFN);
    CHECK(!paging_addrspace_map_user_range(&space, MEM_UNICODE_TABLE_BASE,
          MEM_GFX_BB_BASE + MEM_GFX_BB_SIZE, PAGE_RW | PTE_USER));
    CHECK(!paging_master_audit(exec_tramp_page_addr()));
    CHECK(!paging_shm_set_rw(MEM_SHM_BASE, MEM_SHM_BASE + PAGE_SIZE, 0));
    CHECK(!paging_master_audit(exec_tramp_page_addr()));
    pt[MEM_SHM_BASE / PAGE_SIZE] |= PTE_RW;
    CHECK(paging_master_audit(exec_tramp_page_addr()) > 0);
    pt[MEM_SHM_BASE / PAGE_SIZE] &= ~PTE_RW;
    CHECK(!paging_shm_set_rw(MEM_SHM_BASE, MEM_SHM_BASE + PAGE_SIZE, 1));
    u32 frame = pgalloc_alloc_phys(owner, 1);
    CHECK(frame != 0);
    CHECK(!paging_addrspace_map_user(&space, MEM_EXEC_LOAD_ADDR, frame, PAGE_RW | PTE_USER));
    CHECK(!lease_audit_all());
    u32 *apppt = P2V(space.app_pt_phys[0]);
    u32 ai = (MEM_EXEC_LOAD_ADDR >> PAGE_SHIFT) % PTE_COUNT;
    saved = apppt[ai]; apppt[ai] = MEM_DMA_POOL_BASE | PAGE_RW | PTE_USER;
    CHECK(lease_audit_all() > 0);
    apppt[ai] = saved;
    u32 unregistered = pgalloc_alloc_phys(LEDGER_OWNER_SHLIB, 1);
    CHECK(unregistered != 0);
    apppt[0] = unregistered | PAGE_RO | PTE_USER;
    CHECK(lease_audit_all() > 0);
    apppt[0] = 0;
    CHECK(pgalloc_free_n_owner(LEDGER_OWNER_SHLIB, unregistered / PAGE_SIZE, 1));
    u32 *pd = P2V(space.pd_phys);
    saved = pd[0]; pd[0] ^= PTE_RW;
    CHECK(lease_audit_all() > 0);
    pd[0] = saved;
    pd[APP_BAND_PDE + MEM_APP_BAND_MAX_PDES] = PAGE_RW;
    CHECK(lease_audit_all() > 0);
    pd[APP_BAND_PDE + MEM_APP_BAND_MAX_PDES] = 0;
    CHECK(!lease_audit_all());
    /* Audit master while another CR3 is current; preserve CR3 and IF. */
    host_cr3 = space.pd_phys;
    int boot_pass = ksel_pass, boot_fail = ksel_fail;
    CHECK(!kselftest_run_audit("host-AS"));
    CHECK(ksel_pass == boot_pass && ksel_fail == boot_fail);
    CHECK(audit_runs && !audit_fail);
    CHECK(host_cr3 == space.pd_phys && host_arch_if == 0x202U);
    host_cr3 = paging_kernel_pd_phys();
    struct ledger_surface *tv = ledger_surface_find(0, LEDGER_ROLE_TVRAM);
    CHECK(tv != 0);
    struct surface_query_source tvsrc;
    gfx_surface_unready |= 1U << (tv-ledger_surfaces);
    CHECK(!system_surface_source(LEDGER_ROLE_TVRAM, &tvsrc) && !tvsrc.ready);
    gfx_surface_unready &= ~(1U << (tv-ledger_surfaces));
    CHECK(!system_surface_source(LEDGER_ROLE_TVRAM, &tvsrc) && tvsrc.ready);
    struct surface_ref ref = {(u32)(tv-ledger_surfaces), tv->gen};
    struct lease_view view;
    struct lease_authority auth = {owner, tv->backend, tv->role};
    CHECK(!lease_acquire(&space, &auth, &ref, 1, LEDGER_PERM_RO, &view));
    CHECK(!lease_audit_all());
    pt[idx] &= ~PTE_PCD;
    CHECK(lease_audit_all() > 0);
    pt[idx] |= PTE_PCD;
    tv->lease_count++;
    CHECK(lease_audit_all() > 0);
    tv->lease_count--;
    gfx_client_to_gshell();
    CHECK(tv->gen == ref.generation + 1 && !tv->lease_count);
    CHECK(lease_release(&space, view.token) == LEASE_INVAL);
    ref.generation = tv->gen;
    CHECK(!lease_acquire(&space, &auth, &ref, 1, LEDGER_PERM_RO, &view));
    gfx_client_to_gshell();
    CHECK(tv->gen == ref.generation + 1 && !tv->lease_count);
    gfx_init();
    struct ledger_surface *display = ledger_surface_find(LEDGER_SF_PC98, LEDGER_ROLE_DISPLAY);
    u32 gen = display->gen;
    struct surface_ref dref = {(u32)(display-ledger_surfaces), gen};
    struct lease_view dview;
    struct lease_authority dauth = {owner, display->backend, display->role};
    CHECK(!lease_acquire(&space, &dauth, &dref, 1, LEDGER_PERM_RW, &dview));
    ref.generation = tv->gen;
    CHECK(!lease_acquire(&space, &auth, &ref, 1, LEDGER_PERM_RO, &view));
    struct ledger_surface *client = ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_CLIENT);
    struct surface_ref cref = {(u32)(client-ledger_surfaces), client->gen};
    CallerAccessFrame previous;
    slot.state=APP_STATE_RUNNING; slot.cpl3=1; slot.hdr_flags=OS32X_FLAG_GFX;
    payload=frame; host_cr3=space.pd_phys;
    CHECK(caller_access_enter(&previous,CALLER_USER));
    cref.generation=client->gen;
    gfx_framebuffer_bridge((void *)MEM_EXEC_LOAD_ADDR);
    u32 compat=space.leases[2].token;
    CHECK(compat && (space.leases[2].flags & AS_LEASE_GFX_COMPAT));
    gfx_v86_return();
    CHECK(client->gen == cref.generation && client->lease_count == 1);
    CHECK(space.leases[2].token == compat);
    CHECK(surface_api_unlease(compat) == OS32_ERR_INVAL);
    CHECK(space.leases[2].token == compat);
    CHECK(!lease_release(&space, compat));
    CHECK(surface_api_unlease(compat) == OS32_ERR_INVAL);
    CHECK(!surface_api_query(LEDGER_ROLE_CLIENT,(void *)MEM_EXEC_LOAD_ADDR));
    struct surface_query_result *q=P2V(payload);
    CHECK(q->count == 1 && q->desc[0].role == LEDGER_ROLE_CLIENT);
    *(struct surface_ref *)P2V(payload+512)=q->desc[0].ref;
    CHECK(!surface_api_lease(LEDGER_ROLE_CLIENT,(void *)(MEM_EXEC_LOAD_ADDR+512),LEDGER_PERM_RW,(void *)(MEM_EXEC_LOAD_ADDR+256)));
    u32 token=((struct lease_view *)P2V(payload+256))->token;
    host_cr3=paging_kernel_pd_phys();
    CHECK(surface_api_unlease(token) == OS32_ERR_INVAL);
    host_cr3=space.pd_phys;
    u32 generation=space.generation, saved_owner=space.owner;
    space.generation++;
    CHECK(surface_api_unlease(token) == OS32_ERR_INVAL);
    space.generation=generation; space.owner++;
    CHECK(surface_api_unlease(token) == OS32_ERR_INVAL);
    space.owner=saved_owner;
    ring3_wm_depth=1;
    CHECK(surface_api_unlease(token) == OS32_ERR_INVAL);
    ring3_wm_depth=0;
    kctx_irq_depth=1;
    CHECK(surface_api_unlease(token) == OS32_ERR_INVAL);
    kctx_irq_depth=0;
    CHECK(!surface_api_unlease(token));
    CHECK(!surface_api_query(LEDGER_ROLE_TVRAM,(void *)MEM_EXEC_LOAD_ADDR));
    CHECK(q->count == 1 && q->desc[0].role == LEDGER_ROLE_TVRAM);
    CHECK(surface_api_query(0,(void *)MEM_EXEC_LOAD_ADDR) == OS32_ERR_INVAL);
    CHECK(!surface_api_query(LEDGER_ROLE_DISPLAY,(void *)MEM_EXEC_LOAD_ADDR));
    CHECK(q->count == 4);
    for (u32 i=0;i<4;i++) ((struct surface_ref *)P2V(payload+512))[i]=q->desc[i].ref;
    CHECK(!surface_api_bundle(LEDGER_ROLE_DISPLAY,(void *)(MEM_EXEC_LOAD_ADDR+512),4,LEDGER_PERM_RW,(void *)(MEM_EXEC_LOAD_ADDR+256)));
    for (u32 i=0;i<4;i++)
        CHECK(!surface_api_unlease(((struct surface_lease_result *)P2V(payload+256))->views[i].token));
    caller_access_leave(&previous);
    CHECK(surface_api_unlease(token) == OS32_ERR_INVAL);
    host_cr3=paging_kernel_pd_phys();
    kselftest_audit_v86_return();
    CHECK(tv->gen == ref.generation + 1 && !tv->lease_count && display->gen == gen + 1);
    CHECK(lease_release(&space, dview.token) == LEASE_INVAL);
    dref.generation = display->gen;
    CHECK(!lease_acquire(&space, &dauth, &dref, 1, LEDGER_PERM_RW, &dview));
    CHECK(!lease_release(&space, dview.token));
    ref.generation = tv->gen;
    CHECK(!lease_acquire(&space, &auth, &ref, 1, LEDGER_PERM_RO, &view));
    CHECK(!lease_release(&space, view.token));
    CHECK(!memmap_audit_fail && !as_audit_fail && !v86_return_audit_fail && v86_return_audit_runs == 1);
    extern volatile u32 lease_revoke_all_fail_count;
    u32 failed = lease_revoke_all_fail_count;
    CHECK(lease_revoke_all(0) == 1 && lease_revoke_all_fail_count == failed + 1);
    struct v86_session_state session = {0};
    v86_map_session = &session;
    u32 runs = memmap_audit_runs;
    CHECK(!kselftest_run_audit("skip"));
    CHECK(memmap_audit_runs == runs && memmap_audit_skip == 1);
    v86_map_session = 0;
    pt[idx] &= ~PTE_PCD;
    CHECK(kselftest_run_audit("host-first") > 0);
    CHECK(audit_first_fail_tag[0] == 'h' && audit_first_fail_tag[5] == 'f');
    CHECK(serial_len >= 2 && serial_line[serial_len-2] == '\r' && serial_line[serial_len-1] == '\n');
    u32 first_len=serial_len;
    CHECK(kselftest_run_audit("host-second") > 0);
    CHECK(serial_len == first_len && audit_first_fail_tag[5] == 'f');
    pt[idx] |= PTE_PCD;
    CHECK(!kselftest_run_audit("recovered"));
    CHECK(serial_len == first_len);
    /* A fresh first failure during SerialFS records only the tag. */
    audit_fail=0; serial_len=0; serial_session=1;
    pt[idx] &= ~PTE_PCD;
    CHECK(kselftest_run_audit("serial-session") > 0);
    CHECK(audit_first_fail_tag[0] == 's' && !serial_len);
    serial_session=0;
    CHECK(kselftest_run_audit("after-session") > 0);
    CHECK(audit_first_fail_tag[0] == 's' && !serial_len);
    pt[idx] |= PTE_PCD;
    CHECK(!lease_selftest());
    CHECK(!lease_audit_all());
    SAY("PASS e10c master/AS/lifecycle/TVRAM/DISPLAY/IRQ-bounds");
    die(0);
}
