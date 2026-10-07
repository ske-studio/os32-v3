/* T2e e4: real gfx lifecycle/three backends and ledger/query/lease/B1.
 * Hardware discovery and I/O are host fixtures. No device/emulator access. */
#include "types.h"
static u32 host_cr3;
#include "paging_host_source.c"
#include "pgalloc_host_source.c"
#include "sys_host_source.c"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void die(int code)
{
    if (!code && host_arch_if != 0x202U) code = 2;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define SAY(s) report(s "\n", sizeof(s "\n") - 1)
static u32 checks;
#define CHECK(x) do { checks++; if (!(x)) { SAY("FAIL " #x); die(1); } } while (0)


#include "gfx_internal.h"
#include "gfx_hal.h"
#include "pegc.h"
#include "wab_xe10.h"
#include "wab_glue.h"
#include "surface_query.h"
#include "appslot.h"
#define CFG_KB 17408
void *kmemset(void *d, int c, u32 n) { u8 *p=d; while(n--) *p++=c; return d; }
void *kmemcpy(void *d, const void *s, u32 n) { u8 *p=d; const u8 *q=s; while(n--) *p++=*q++; return d; }
void __cdecl kprintf(u8 a,const char *f,...) { (void)a;(void)f; }
static u32 arena_top_pfn;   /* 凍結した exec 上端 (PFN) */

static u32 ws_pages(u32 limit)
{
    u32 pages = 1;
    if (limit > PAGING_BOOT_MAP_SIZE / PAGE_SIZE)
        pages += (limit + PTE_COUNT - 1) / PTE_COUNT - PAGING_BOOT_PT_COUNT;
    return pages;
}

static void boot(void)
{
    struct physmem m;
    struct pgalloc_layout l;
    u32 low_kb = CFG_KB > 15360 ? 15360 : CFG_KB;
    u32 limit = CFG_KB / 4, top;
    {
        u32 low_args[6] = {MEM_GFX_BB_BASE, MEM_GFX_BB_SIZE, 3, 0x32, 0xffffffffUL, 0};
        u32 low_result;
        __asm__ volatile("int $0x80" : "=a"(low_result) : "a"(90), "b"(low_args) : "memory");
        CHECK(low_result == MEM_GFX_BB_BASE);
    }
    host_map_fixed_paging();
    paging_init(CFG_KB);
    physmem_bootstrap_legacy(&m, low_kb);
    if (CFG_KB > 16384)
        CHECK(physmem_add_trusted(&m, 4096, limit, PHYSMEM_SOURCE_SYNTHETIC));
    top = physmem_legacy_end(&m);
    l.capacity = pgalloc_metadata_bytes(&m);
    if (CFG_KB > 12288) {
        l.kind = PGALLOC_BACKING_ARENA_TOP;
        l.metadata_first = top - l.capacity / PAGE_SIZE;
        l.metadata = (void *)(l.metadata_first * PAGE_SIZE);
        l.workspace_end = l.metadata_first;
        l.workspace_first = l.workspace_end - ws_pages(limit);
        arena_top_pfn = l.workspace_first;
    } else {
        CHECK(paging_map_ledger_backing());
        l.kind = PGALLOC_BACKING_FIXED;
        l.metadata_first = MEM_LEDGER_META_BASE / PAGE_SIZE;
        l.metadata = (void *)MEM_LEDGER_META_BASE;
        l.workspace_first = l.metadata_first + 1;
        l.workspace_end = MEM_LEDGER_META_END / PAGE_SIZE;
        arena_top_pfn = top;
    }
    CHECK(sys_memory_bootstrap_model(&m, &l, paging_verify_identity));
    CHECK(sys_memory_stage_online());
    CHECK(paging_boot_context());
    /* Production memory_boot also registers the native VRAM FIXED/UC band. */
    CHECK(ledger_register_region(LEDGER_R_FIXED, LEDGER_OWNER_KERNEL,
          MEM_CONV_END / PAGE_SIZE, KERNEL_LOAD_ADDR / PAGE_SIZE,
          LEDGER_CACHE_UC, 0));
    /* ③ の区間のうち ⑥ に効くもの: planar BB の固定面と背景 2 本 */
    CHECK(ledger_register_region(LEDGER_R_SURFACE_BACKING, LEDGER_OWNER_BOOT,
                                 MEM_GFX_BB_BASE / PAGE_SIZE,
                                 (MEM_GFX_BB_BASE + MEM_GFX_BB_SIZE) / PAGE_SIZE,
                                 LEDGER_CACHE_WB, 0));
    CHECK(ledger_register_region(LEDGER_R_BACKGROUND, LEDGER_OWNER_KERNEL,
                                 MEM_SYSTEM_SPACE_BASE / PAGE_SIZE,
                                 MEM_HIGH_RAM_BASE / PAGE_SIZE, LEDGER_CACHE_UC, 0));
    CHECK(ledger_register_region(LEDGER_R_BACKGROUND, LEDGER_OWNER_KERNEL,
                                 MEM_PHYS_RAM_CEILING / PAGE_SIZE, PHYSMEM_MAX_PFN,
                                 LEDGER_CACHE_UC, 0));
    CHECK(pgalloc_arena_end() == arena_top_pfn);
    CHECK(sys_usable_mem_end() == arena_top_pfn * PAGE_SIZE);
}


static void map_host(u32 base, u32 size)
{
    u32 a[6]={base,size,3,0x32,0xffffffffU,0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(a) : "memory");
    CHECK(result == base);
}
int host_pegc_available = 1, host_cirrus_available = 1;
static int fail_after_init;
int host_select_and_init(void);
static AppSlot host_slots[APP_SLOT_COUNT];
#define slot host_slots[2]
static struct addrspace space;
static u32 payload;
volatile int ring3_wm_depth;
int appslot_cur(void) { return 2; }
int res_owner_get(void) { return 2; }
static int host_user = 1, host_gui, host_gfx_owner = 2;
int ring3_call_from_user(void) { return host_user; }
AppSlot *appslot_at(int id) { return id >= 0 && id < APP_SLOT_COUNT ? &host_slots[id] : 0; }
AppSlot *appslot_get(int id) { return id == 2 ? &slot : 0; }
int appslot_gfx_owner(void) { return host_gfx_owner; }
int appslot_gfx_claim(int gui) { (void)gui;return 0; }
int con_sink_is_enabled(void) { return host_gui; }
#ifndef E10C_REAL_AUDIT
int kselftest_run_audit(const char *tag) { return !ledger_selfcheck(tag); }
#endif
u32 exec_tramp_page_addr(void) { return 0x170000; }
void shell_print(const char *s,u8 c) {(void)s;(void)c;}
void console_hw_cursor_enable(void) { }
void cpu_delay_us(u32 n) {(void)n;}
void palette_init(void) { }
void palette_set(int i,u8 r,u8 g,u8 b) {(void)i;(void)r;(void)g;(void)b;}
void palette_shadow_set(int i,u8 r,u8 g,u8 b) {(void)i;(void)r;(void)g;(void)b;}
const PaletteEntry *palette_get_all(void) {static PaletteEntry p[16];return p;}
static void relay(int on) {(void)on;}
static void linear(int on) {(void)on;}
WabGlue wab_glue_xe10 = {.win_base=WAB_XE10_WIN_BASE,.win_size=WAB_XE10_WIN_SIZE,
 .lin_base=WAB_XE10_LINEARWIN_BASE,.lin_size=WAB_XE10_LINEARWIN_SIZE,
 .relay=relay,.linear_enable=linear};
int wab_cirrus_setup_8bpp(WabGlue *g,int w,int h,u32 p,u32 s) {(void)g;(void)w;(void)h;(void)p;(void)s;if (fail_after_init) host_cirrus_available=0;return 0;}
void wab_cirrus_shutdown(WabGlue *g) {(void)g;}
void wab_cirrus_dac_set(WabGlue *g,int i,u8 r,u8 gg,u8 b) {(void)g;(void)i;(void)r;(void)gg;(void)b;}
void wab_cirrus_set_fill_pattern(WabGlue *g,u32 o) {(void)g;(void)o;}
int wab_cirrus_fill(WabGlue *g,u32 d,u32 p,int w,int h,u8 c) {(void)g;(void)d;(void)p;(void)w;(void)h;(void)c;return 0;}
int wab_cirrus_copy(WabGlue *g,u32 d,u32 s,u32 dp,u32 sp,int w,int h) {(void)g;(void)d;(void)s;(void)dp;(void)sp;(void)w;(void)h;return 0;}
int wab_cirrus_wait_idle(WabGlue *g) {(void)g;return 0;}

static void view_check(u32 backend, u32 height, int started)
{
    struct gfx_kernel_fb fb;
    GFX_Framebuffer public_fb;
    GFX_ScreenInfo si;
    struct surface_query_source source;
    const struct ledger_surface *sf=ledger_surface_find(backend,LEDGER_ROLE_CLIENT);
    CHECK(!gfx_kernel_framebuffer(&fb));
    host_user = 0;
    gfx_get_framebuffer(&public_fb);
    host_user = 1;
    if (payload) {
        GFX_Framebuffer *user_fb=P2V(payload+512);
        gfx_get_framebuffer((void *)(MEM_EXEC_LOAD_ADDR+512));
        if (started) {
            CHECK(user_fb->width == public_fb.width && user_fb->height == public_fb.height);
            CHECK((u32)user_fb->planes[0] >= MEM_LEASE_BASE);
            for (u32 i=0;i<4;i++) {
                CHECK((user_fb->planes[i]!=0) == (public_fb.planes[i]!=0));
                if (user_fb->planes[i]) CHECK(user_fb->planes[i]-user_fb->planes[0] == public_fb.planes[i]-public_fb.planes[0]);
            }
            CHECK(!lease_release(&space,space.leases[0].token));
        } else CHECK(!user_fb->width && !user_fb->planes[0]);
    }
    gfx_screen_info(&si);
    const struct ledger_surface *client=ledger_surface_find(gfx_sf_backend(),LEDGER_ROLE_CLIENT);
    GFX_ScreenInfo selected_info;
    CHECK(!g_backend->query(&selected_info));
    /* pegcchk/hal_test query before gfx_init to decide the backend format. */
    CHECK(si.format == selected_info.format && si.format == client->format);
    CHECK(si.width == selected_info.width && si.height == selected_info.height);
    CHECK(fb.width == sf->width && fb.height == height);
    CHECK(fb.pitch == sf->pitch && fb.format == sf->format);
    CHECK(public_fb.width == si.width && public_fb.height == si.height && public_fb.pitch == client->pitch);
    u32 phys, bytes;
    gfx_bb_phys_range(&phys,&bytes);
    CHECK(phys == client->first*PAGE_SIZE && bytes == client->npages*PAGE_SIZE);
    for (u32 i=0;i<4;i++) {
        u8 *want=i<sf->planes ? (u8 *)P2V(sf->first*PAGE_SIZE)+sf->plane_offset[i] : 0;
        u8 *public_want=i<client->planes ? (u8 *)P2V(client->first*PAGE_SIZE)+client->plane_offset[i] : 0;
        CHECK(fb.planes[i] == want);
        CHECK(public_fb.planes[i] == public_want);
        if (started) CHECK(bb[i] == want);
    }
    if (started) {
        CHECK(g_backend->bb_base == fb.planes[0]);
        CHECK(bb_b == bb[0] && bb_r == bb[1] && bb_g == bb[2] && bb_i == bb[3]);
        CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT,&source));
        CHECK(source.ready && source.backend == backend && source.count == 1);
        CHECK(source.refs[0].sid == (u32)(sf-ledger_surfaces));
        CHECK(!surface_query_refs(&source,source.refs,1,LEDGER_PERM_RW));
    } else {
        CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&source) == OS32_ERR_INVAL && !source.ready);
    }
}
static void display_check(u32 backend)
{
    struct surface_query_source source;
    int rc=gfx_surface_source(LEDGER_ROLE_DISPLAY,&source);

    CHECK(!rc && source.ready);
    CHECK(source.count == (backend == LEDGER_SF_PC98 ? 4U : 1U));
    CHECK(!surface_query_refs(&source,source.refs,source.count,LEDGER_PERM_RW));
    if (backend == LEDGER_SF_PC98) {
        const u32 planes[4]={VRAM_PLANE_B,VRAM_PLANE_R,VRAM_PLANE_G,VRAM_PLANE_I};
        for(u32 i=0;i<4;i++) CHECK(ledger_surfaces[source.refs[i].sid].first*PAGE_SIZE == planes[i]);
    } else {
        const struct ledger_surface *sf=&ledger_surfaces[source.refs[0].sid];
        CHECK(sf->first*PAGE_SIZE == (backend == LEDGER_SF_CIRRUS ? WAB_XE10_LINEARWIN_BASE : PEGC_LINEAR_BASE));
        CHECK(sf->cache == LEDGER_CACHE_UC && sf->backing == (backend == LEDGER_SF_CIRRUS ? LEDGER_SB_MMIO : LEDGER_SB_VRAM) && sf->perm_max == LEDGER_PERM_RW);
        struct lease_view *out=P2V(payload+64);
        *(struct surface_ref *)P2V(payload)=source.refs[0];
        slot.hdr_flags=0;
        CHECK(surface_lease(&source,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RW,(void *)(MEM_EXEC_LOAD_ADDR+64)) == OS32_ERR_INVAL);
        slot.hdr_flags=OS32X_FLAG_GFX; host_gui=1;host_gfx_owner=3;
        CHECK(surface_lease(&source,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RW,(void *)(MEM_EXEC_LOAD_ADDR+64)) == OS32_ERR_INVAL);
        host_gui=0;host_gfx_owner=2;
        CHECK(!surface_lease(&source,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RW,(void *)(MEM_EXEC_LOAD_ADDR+64)));
        CHECK(out->token && out->bytes == PEGC_FB_SIZE_480);
        CHECK((paging_lease_pte(&space,out->base) & (PTE_PCD|PTE_RW|PTE_USER)) == (PTE_PCD|PTE_RW|PTE_USER));
        CHECK(!lease_release(&space,out->token));
    }
}
static void bridge_check(int started)
{
    GFX_Framebuffer *fb=P2V(payload+512);
    u32 failures=gfx_bridge_fail_count, users=gfx_bridge_user_count;
    u32 saved_hdr = slot.hdr_flags;
    if (!started) slot.hdr_flags=0; /* CUI loading a GUI shlib before gfx_init. */
    gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
    slot.hdr_flags=saved_hdr;
    if (!started) {
        CHECK(!slot.abort_req && gfx_bridge_fail_count == failures+1);
        CHECK(!fb->width && !fb->planes[0]);
        slot.abort_req=0;
        return;
    }
    CHECK(!slot.abort_req && gfx_bridge_fail_count == failures);
    CHECK(gfx_bridge_user_count == users+1 && gfx_bridge_last_va == (u32)fb->planes[0]);
    CHECK((u32)fb->planes[0] >= MEM_LEASE_BASE && (u32)fb->planes[0] < MEM_LEASE_END);
    CHECK((paging_lease_pte(&space,(u32)fb->planes[0]) & (PTE_USER|PTE_RW)) == (PTE_USER|PTE_RW));
    u32 token=space.leases[0].token;
    CHECK(token && (space.leases[0].flags & AS_LEASE_GFX_COMPAT));
    gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
    CHECK(space.leases[0].token == token && !space.leases[1].token);
    /* GUI initialization failures must disable drawing without killing it. */
    slot.hdr_flags=OS32X_FLAG_GFX; host_gui=1; host_gfx_owner=3;
    gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
    CHECK(!slot.abort_req && !fb->width && !fb->planes[0]);
    host_gui=0; host_gfx_owner=2;
    gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+PAGE_SIZE-4));
    CHECK(slot.abort_req); /* Only bad copyout requests termination. */
    slot.abort_req=0;
    gfx_v86_return();
    CHECK(space.leases[0].token == token && !lease_check(&space));
    CHECK(!lease_release(&space,token));
}
static u8 stack[65536] __attribute__((aligned(16),used));
static void run(void) __attribute__((used));
__asm__(".text\n.globl _start\n_start: lea stack+65536,%esp\ncall run\n");
static void run(void)
{
    struct gfx_kernel_fb fb;
    struct surface_query_source source;
    u32 owner;
    CallerAccessFrame previous;
    CHECK(!bb_b && !bb_r && !bb_g && !bb_i && !gfx_backend_pc98.bb_base);
    CHECK(gfx_kernel_framebuffer(&fb) == OS32_ERR_INVAL && !fb.planes[0]);
    gfx_present(); gfx_present_dirty(); gfx_present_nosync(); gfx_present_raster(0);
    gfx_add_dirty_rect(0,0,32,1); gfx_hardware_scroll(1);
    CHECK(!dirty_queue.count && !gfx_counters.commits);
    map_host(0x10000,0xFF0000);
    map_host(WAB_XE10_LINEARWIN_BASE,WAB_XE10_LINEARWIN_SIZE);
    map_host(MEM_EXEC_LOAD_ADDR,PAGE_SIZE); /* observable old direct copyout */
    boot();
    gfx_boot_reserve();
    CHECK(bb_b == (u8 *)P2V(MEM_GFX_BB_BASE));
    CHECK(bb_r-bb_b == GFX_PLANE_SZ && bb_g-bb_b == 2*GFX_PLANE_SZ && bb_i-bb_b == 3*GFX_PLANE_SZ);
    CHECK(ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_DISPLAY) != 0);
    struct ledger_resource *rr=&ledger_resources[0];
    u32 mf=rr->map_first, me=rr->map_end;
    CHECK(!ledger_resource_set_map(LEDGER_MAX_RESOURCES,0,0));
    CHECK(!ledger_resource_set_map(LEDGER_MAX_RESOURCES-1,0,0));
    CHECK(!ledger_resource_set_map(0,rr->decode_first-1,rr->decode_end));
    CHECK(!ledger_resource_set_map(0,rr->decode_first,rr->decode_end+1));
    CHECK(!ledger_resource_set_map(0,me,mf));
    CHECK(!ledger_resource_set_map(0,mf,mf));
    CHECK(rr->map_first == mf && rr->map_end == me);
    CHECK(ledger_resource_set_map(0,0,0));
    CHECK(!rr->map_first && !rr->map_end);
    CHECK(ledger_resource_set_map(0,mf,me));
    view_check(LEDGER_SF_PC98,GFX_HEIGHT,0);
    CHECK(ledger_owner_new(LEDGER_KIND_AS,2,"e4",&owner));
    CHECK(!paging_addrspace_create_lease(&space,owner));
    payload=pgalloc_alloc_phys(owner,1);
    CHECK(payload && !paging_addrspace_map_user(&space,MEM_EXEC_LOAD_ADDR,payload,PAGE_RW|PTE_USER));
    slot.state=APP_STATE_RUNNING;slot.cpl3=1;slot.as=&space;slot.hdr_flags=OS32X_FLAG_GFX;
    host_cr3=space.pd_phys;
    CHECK(caller_access_enter(&previous,CALLER_USER));
    /* Run the real selection/lifecycle for every preference, plus AUTO. */
    for(int pref=GFX_PREF_PC98;pref<=GFX_PREF_CIRRUS;pref++) {
        u32 selected=pref == GFX_PREF_PC98 ? LEDGER_SF_PC98 :
          pref == GFX_PREF_PEGC ? LEDGER_SF_PEGC : LEDGER_SF_CIRRUS;
        gfx_set_backend_pref(pref);
        gfx_prepare_backend();
        CHECK(gfx_sf_backend() == selected);
        view_check(LEDGER_SF_PC98,GFX_HEIGHT,0);
        bridge_check(0);
        gfx_init();
        view_check(selected,selected == LEDGER_SF_PC98 ? GFX_HEIGHT : MEM_GFX_BB8_HEIGHT,1);
        bridge_check(1);
        display_check(selected);
        /* Poison aliases: init_200 must rebind, even on the same backend. */
        bb[0]=bb_b=0;
        gfx_init_200();
        CHECK(ledger_surface_find(LEDGER_SF_PC98,LEDGER_ROLE_CLIENT)->height == GFX_HEIGHT);
        CHECK(ledger_surface_find(LEDGER_SF_PC98,LEDGER_ROLE_CLIENT)->plane_offset[1] == GFX_PLANE_SZ);
        view_check(selected,selected == LEDGER_SF_PC98 ? GFX_HEIGHT_200 : MEM_GFX_BB8_HEIGHT,1);
        bridge_check(1);
        display_check(selected);
        gfx_shutdown();
        view_check(LEDGER_SF_PC98,GFX_HEIGHT,0);
        bridge_check(0);
    }
    gfx_set_backend_pref(GFX_PREF_AUTO);gfx_prepare_backend();
    CHECK(gfx_sf_backend() == LEDGER_SF_CIRRUS);
    CHECK(gfx_surface_source(LEDGER_ROLE_TVRAM,&source) == OS32_ERR_INVAL);
    host_cirrus_available=0;
    gfx_prepare_backend(); CHECK(gfx_sf_backend() == LEDGER_SF_PEGC);
    gfx_init(); view_check(LEDGER_SF_PEGC,MEM_GFX_BB8_HEIGHT,1); gfx_shutdown();
    host_pegc_available=0;
    gfx_prepare_backend(); CHECK(gfx_sf_backend() == LEDGER_SF_PC98);
    for(int pref=GFX_PREF_PEGC;pref<=GFX_PREF_CIRRUS;pref++) {
        gfx_set_backend_pref(pref); gfx_init();
        CHECK(gfx_sf_backend() == LEDGER_SF_PC98);
        view_check(LEDGER_SF_PC98,GFX_HEIGHT,1);gfx_shutdown();
    }
    /* Switch without prepare: old PC98 aliases must not survive packed init. */
    host_cirrus_available=1;
    gfx_set_backend_pref(GFX_PREF_CIRRUS);
    gfx_init(); view_check(LEDGER_SF_CIRRUS,MEM_GFX_BB8_HEIGHT,1);
    gfx_shutdown();
    /* Observe immediately after post-init probe failure, before gfx_init's
     * later planar bind could hide a missing fallback bind. */
    fail_after_init=1;
    CHECK(!host_select_and_init());
    CHECK(gfx_sf_backend() == LEDGER_SF_PC98);
    CHECK(bb[0] == (u8 *)P2V(MEM_GFX_BB_BASE) && bb[1] == bb[0]+GFX_PLANE_SZ);
    CHECK(bb[2] == bb[0]+2*GFX_PLANE_SZ && bb[3] == bb[0]+3*GFX_PLANE_SZ);
    view_check(LEDGER_SF_PC98,GFX_HEIGHT,0);
    gfx_init(); view_check(LEDGER_SF_PC98,GFX_HEIGHT,1); gfx_shutdown();
    fail_after_init=0;
    caller_access_leave(&previous);
    slot.abort_req=0;
    gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
    CHECK(slot.abort_req); /* USER with no saved caller must fail closed. */
    slot.abort_req=0;
    CHECK(caller_access_enter(&previous,CALLER_USER));
    /* Same failure state as a rejected CLIENT registration: no usable entry. */
    struct ledger_surface *pc=ledger_surface_find(LEDGER_SF_PC98,LEDGER_ROLE_CLIENT);
    pc->npages=0;
    gfx_set_backend_pref(GFX_PREF_PC98);gfx_init_200();
    CHECK(!bb_b && !bb_r && !bb_g && !bb_i && !gfx_backend_pc98.bb_base);
    CHECK(gfx_kernel_framebuffer(&fb) == OS32_ERR_INVAL && !fb.planes[0]);
    u32 commits=gfx_counters.commits;
    int scroll=vram_scroll_y;
    gfx_present();gfx_present_dirty();gfx_present_nosync();gfx_present_raster(0);
    gfx_add_dirty_rect(0,0,32,1);gfx_hardware_scroll(1);
    CHECK(commits == gfx_counters.commits && scroll == vram_scroll_y);
    CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&source) == OS32_ERR_INVAL && !source.ready);
    {
        char number[12]; u32 n=checks, len=0;
        do {number[len++]=(char)('0'+n%10);n/=10;} while(n);
        report("PASS checks=",12);
        while(len) report(&number[--len],1);
        SAY("; gfx kernel fb: 3 backends, lifecycle, fallback, missing CLIENT, DISPLAY lease");
    }
    die(0);
}

/* Paging-only fixture has no exec slots; abort delivery is appmem_map_host. */
void exec_addrspace_abort(struct addrspace *as) { (void)as; }
