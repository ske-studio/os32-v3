/* Private product table, two real SDK instances, real query/lease/ledger. */
#include "system_surface.h"
#include "unicode_file.h"
#include "unicode_pairs.inc"
#include "vfs.h"
extern u16 unicode_to_jis(u32 cp), shl_unicode_to_jis(u32 cp);
extern KernelAPI *shl_gfx_api;
extern const struct gfx_attach_port *shl_gfx_attach_port;
extern int shl_libos32gfx_attach_checked(void);
extern void shl_libos32gfx_detach(void);
KernelAPI *kapi = &api;
KernelAPI *shl_kapi = &api;
static u32 kapi_calls, reads;
static int missing = 1;
static u16 __cdecl convert(u32 cp) { kapi_calls++; return unicode_file_lookup(cp); }
int vfs_stat(const char *path, OS32_Stat *st)
{
    (void)path;
    if (missing) return VFS_ERR_NOTFOUND;
    st->st_size = sizeof(generated_pairs);
    return 0;
}
int vfs_read(const char *path, void *out, u32 bytes)
{
    (void)path; reads++;
    CHECK(bytes == sizeof(generated_pairs));
    kmemcpy(out, generated_pairs, bytes);
    return bytes;
}
static void protect_scratch(int prot)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "a"(125), "b"(0x4A000UL),
                     "c"(0x6A000UL-0x4A000UL), "d"(prot) : "memory");
    CHECK(!rc);
}
static void run(void) __attribute__((used));
static void run(void)
{
    u32 owner;
    CallerAccessFrame prev;
    map_host(0x10000,0xFF0000);
    map_host(WAB_XE10_LINEARWIN_BASE,WAB_XE10_LINEARWIN_SIZE);
    map_host(MEM_LEASE_BASE,0x200000);
    boot();gfx_boot_reserve();
    CHECK(!ledger_surface_find(0,LEDGER_ROLE_UNICODE));
    const struct ledger_surface *tv=ledger_surface_find(0,LEDGER_ROLE_TVRAM);
    CHECK(tv && tv->perm_max==LEDGER_PERM_RO && tv->cache==LEDGER_CACHE_UC &&
          tv->first==TVRAM_CHAR_BASE/PAGE_SIZE && tv->npages==4 &&
          tv->planes==2 && tv->plane_offset[1]==TVRAM_ATTR_BASE-TVRAM_CHAR_BASE &&
          tv->width==80 && tv->height==25 && tv->pitch==160);
    /* Slot reuse still clears TVRAM publisher state. */
    u32 sid=(u32)(tv-ledger_surfaces);
    struct ledger_surface saved=*tv;
    gfx_surface_unready|=1U<<sid;
    CHECK(ledger_surface_release(sid));
    CHECK(ledger_surface_create(&saved,&sid));
    int slot_ready=!(gfx_surface_unready&(1U<<sid));CHECK(slot_ready);
    u32 kernel_pages=ledger_owner_pages(LEDGER_OWNER_KERNEL);
    api.unicode_to_jis=convert;
    protect_scratch(0);
    unicode_file_load();
    CHECK(!unicode_file_debug_phys() && ledger_owner_pages(LEDGER_OWNER_KERNEL)==kernel_pages);
    CHECK(unicode_to_jis(0x3041)==0x2421 && shl_unicode_to_jis(0x30F6)==0x2576);
    CHECK(!unicode_to_jis(0x4E00) && !shl_unicode_to_jis(0x10000));
    missing=0; unicode_file_load();
    u32 phys=unicode_file_debug_phys();
    CHECK(phys && ledger_owner_pages(LEDGER_OWNER_KERNEL)==kernel_pages+7 && reads==1);
    for (u32 i=0;i<7;i++) CHECK(pgalloc_page_owned(phys/PAGE_SIZE+i,LEDGER_OWNER_KERNEL));
    CHECK(ledger_owner_new(LEDGER_KIND_AS,2,"unicode",&owner));
    CHECK(!paging_addrspace_create_lease(&space,owner));
    payload=pgalloc_alloc_phys(owner,1);
    CHECK(payload && !paging_addrspace_map_user(&space,MEM_EXEC_LOAD_ADDR,payload,PAGE_RW|PTE_USER));
    slot.state=APP_STATE_RUNNING;slot.cpl3=1;slot.as=&space;
    host_cr3=space.pd_phys;CHECK(caller_access_enter(&prev,CALLER_USER));
    struct surface_query_source src;
    CHECK(system_surface_source(LEDGER_ROLE_UNICODE,&src)==OS32_ERR_INVAL);
    CHECK(surface_api_query(LEDGER_ROLE_UNICODE,(void *)MEM_EXEC_LOAD_ADDR)==OS32_ERR_INVAL);
    CHECK(surface_api_lease(LEDGER_ROLE_UNICODE,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RO,
                           (void *)(MEM_EXEC_LOAD_ADDR+256))==OS32_ERR_INVAL);
    CHECK(!live());
    struct caller_access caller;CHECK(caller_access_get(&caller));
    u8 byte;
    CHECK(!copy_caller_bytes(&caller,(void *)phys,&byte,1));
    api.mem_alloc=sdk_alloc;api.version=KAPI_VERSION;
    api.gfx_get_framebuffer=legacy_fb;api.gfx_screen_info=screen;
    api.surface_query=public_query;api.surface_lease=public_lease;api.surface_unlease=public_unlease;
    gfx_api=&api;shl_gfx_api=&api;
    gfx_set_backend_pref(GFX_PREF_PC98);gfx_prepare_backend();gfx_init();
    CHECK(!libos32gfx_attach_checked() && !shl_libos32gfx_attach_checked());
    CHECK(live()==2 && public_queries==2 && public_leases==2);
    u32 calls=kapi_calls;
    CHECK(unicode_to_jis(0x4E9C)==0x3021 && shl_unicode_to_jis(0x9078)==0x412A);
    CHECK(kapi_calls==calls+2 && live()==2 && public_queries==2 && public_leases==2);
    CHECK(!libos32gfx_attach_checked() && !shl_libos32gfx_attach_checked() && live()==2);
    /* TVRAM foreground leases and GUI denial remain unchanged. */
    CHECK(!system_surface_source(LEDGER_ROLE_TVRAM,&src) && src.ready);
    CHECK(!surface_query(&src,(void *)MEM_EXEC_LOAD_ADDR));
    struct surface_ref tr=src.refs[0];
    *(struct surface_ref *)P2V(payload)=tr;
    CHECK(!surface_lease(&src,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RO,(void *)(MEM_EXEC_LOAD_ADDR+256)));
    CHECK(!lease_release(&space,((struct lease_view *)P2V(payload+256))->token));
    slot.state=APP_STATE_PARKED;
    CHECK(surface_query(&src,(void *)MEM_EXEC_LOAD_ADDR)==OS32_ERR_INVAL);
    slot.state=APP_STATE_RUNNING;host_gui=1;
    int tvram_gui_denied=surface_query(&src,(void *)MEM_EXEC_LOAD_ADDR)==OS32_ERR_INVAL;
    CHECK(tvram_gui_denied);
    *(struct surface_ref *)P2V(payload)=tr;
    CHECK(surface_lease(&src,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RO,
          (void *)(MEM_EXEC_LOAD_ADDR+256))==OS32_ERR_INVAL);
    CHECK(!system_surface_source(LEDGER_ROLE_TVRAM,&src) && !src.ready);
    host_gui=0;
    gfx_init();CHECK(!live());
    CHECK(unicode_to_jis(0x4E9C)==0x3021 && shl_unicode_to_jis(0x4E9C)==0x3021);
    struct gfx_attach_desc d;struct gfx_attach_view held[MEM_LEASE_MAX];CHECK(!port_query(&d));
    for(u32 i=0;i<MEM_LEASE_MAX;i++) CHECK(!port_lease(&d.ref,&held[i]));
    CHECK(unicode_to_jis(0x4E9C)==0x3021 && shl_unicode_to_jis(0x4E9C)==0x3021);
    CHECK(live()==MEM_LEASE_MAX);
    CHECK(!lease_revoke_all(&space) && !live());
    CHECK(unicode_file_debug_phys()==phys && reads==1);
    CHECK(!ledger_surface_find(0,LEDGER_ROLE_UNICODE));
    libos32gfx_detach();shl_libos32gfx_detach();
    protect_scratch(3);
    caller_access_leave(&prev);
    SAY("PASS unicode_surface (private 7 pages, two KAPI instances, no Unicode leases, TVRAM, teardown)");
    die(0);
}
