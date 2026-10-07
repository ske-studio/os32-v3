/* T2e e8a: real kernel publisher/query/lease and two real C utf8 instances. */
#include "system_surface.h"
#include "endian_le.h"
extern void k_utf8_set_jis_table_ready(int ready);
extern int k_utf8_validate_jis_table(void);
extern u16 unicode_to_jis(u32 cp), shl_unicode_to_jis(u32 cp);
extern const u8 *utf8_host_pointer(void), *shl_utf8_host_pointer(void);
extern int utf8_host_ready(void), shl_utf8_host_ready(void);
extern void utf8_set_jis_table(const u8 *p), utf8_set_jis_table_ready(int ready);
extern KernelAPI *shl_gfx_api;
extern const struct gfx_attach_port *shl_gfx_attach_port, *shl_gfx_unicode_port;
extern void shl_libos32gfx_unicode_init(void);
extern int shl_libos32gfx_attach_checked(void);
extern void shl_libos32gfx_detach(void);
static int deny_unicode, corrupt_unicode_view;
static u32 unicode_queries;
static int unicode_query(struct gfx_attach_desc *out)
{
    struct surface_query_result *q = P2V(payload);
    unicode_queries++;
    if (deny_unicode) return OS32_ERR_INVAL;
    int rc = surface_api_query(LEDGER_ROLE_UNICODE, (void *)MEM_EXEC_LOAD_ADDR);
    if (rc) return rc;
    struct surface_desc *d = &q->desc[0];
    *out = (struct gfx_attach_desc){0};
    out->ref = (struct gfx_attach_ref){d->ref.sid, d->ref.generation};
    out->bytes=d->bytes;out->planes=d->planes;
    out->width=d->width;out->height=d->height;out->pitch=d->pitch;out->format=d->format;
    return 0;
}
static int unicode_lease(const struct gfx_attach_ref *ref, struct gfx_attach_view *out)
{
    struct lease_view *v=P2V(payload+256);
    int rc;
    *(struct surface_ref *)P2V(payload)=(struct surface_ref){ref->sid,ref->generation};
    rc=surface_api_lease(LEDGER_ROLE_UNICODE,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RO,
                     (void *)(MEM_EXEC_LOAD_ADDR+256));
    if (rc) return rc;
    out->token=v->token;out->base=v->base;out->bytes=v->bytes;
    for(u32 i=0;i<4;i++) out->planes[i]=v->planes[i];
    /* Host MMU footings: copy physical bytes into distinct host lease VA.
     * Kernel PTEs and all B1 access checks still use the real RO mapping. */
    kmemcpy((void *)v->base,P2V(MEM_UNICODE_TABLE_BASE),MEM_UNICODE_TABLE_SIZE);
    if(corrupt_unicode_view) out->planes[0]++;
    return 0;
}
static const struct gfx_attach_port unicode_port={unicode_query,unicode_lease,port_unlease};
static void known_table(void)
{
    const u32 cp[]={0x4E9C,0x4E00,0x5BFE,0x9078};
    const u16 jis[]={0x3021,0x306C,0x4250,0x412A};
    kmemset(P2V(MEM_UNICODE_TABLE_BASE),0,MEM_UNICODE_TABLE_SIZE);
    for(u32 i=0;i<4;i++) le16_wr((u8 *)P2V(MEM_UNICODE_TABLE_BASE)+cp[i]*2,jis[i]);
}
static void protect_low(int prot)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "a"(125), "b"(MEM_UNICODE_TABLE_BASE),
                     "c"(MEM_UNICODE_TABLE_SIZE), "d"(prot) : "memory");
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
    CHECK(ledger_register_region(LEDGER_R_SURFACE_BACKING,LEDGER_OWNER_KERNEL,
          MEM_UNICODE_TABLE_BASE/PAGE_SIZE,
          (MEM_UNICODE_TABLE_BASE+MEM_UNICODE_TABLE_SIZE)/PAGE_SIZE,LEDGER_CACHE_WB,0));
    k_utf8_set_jis_table_ready(0);
    CHECK(system_unicode_register());
    const struct ledger_surface *sf=ledger_surface_find(0,LEDGER_ROLE_UNICODE);
    int unicode_ro=sf && sf->perm_max==LEDGER_PERM_RO;
    CHECK(unicode_ro);
    CHECK(sf->backing==LEDGER_SB_FIXED_RAM && sf->owner==LEDGER_OWNER_KERNEL &&
          sf->cache==LEDGER_CACHE_WB && !sf->backend && sf->width==4096 &&
          sf->pitch==4096 && sf->height==32 && sf->planes==1);
    const struct ledger_surface *tv=ledger_surface_find(0,LEDGER_ROLE_TVRAM);
    CHECK(tv && tv->perm_max==LEDGER_PERM_RO && tv->cache==LEDGER_CACHE_UC &&
          tv->first==TVRAM_CHAR_BASE/PAGE_SIZE && tv->npages==4 &&
          tv->planes==2 && tv->plane_offset[1]==TVRAM_ATTR_BASE-TVRAM_CHAR_BASE &&
          tv->width==80 && tv->height==25 && tv->pitch==160);
    u32 ns=0;for(u32 i=0;i<LEDGER_MAX_SURFACES;i++) ns+=ledger_surfaces[i].npages!=0;
    CHECK(ns==11);
    struct surface_query_source src;
    CHECK(!system_surface_source(LEDGER_ROLE_UNICODE,&src) && !src.ready);
    /* Slot reuse after an unavailable generation must clear publisher state. */
    u32 sid=src.refs[0].sid;
    struct ledger_surface saved=*sf;
    gfx_surface_unready|=1U<<sid;
    CHECK(ledger_surface_release(sid));
    CHECK(ledger_surface_create(&saved,&sid));
    int slot_ready=!(gfx_surface_unready&(1U<<sid));CHECK(slot_ready);
    CHECK(ledger_owner_new(LEDGER_KIND_AS,2,"e8a",&owner));
    CHECK(!paging_addrspace_create_lease(&space,owner));
    payload=pgalloc_alloc_phys(owner,1);
    CHECK(payload && !paging_addrspace_map_user(&space,MEM_EXEC_LOAD_ADDR,payload,PAGE_RW|PTE_USER));
    slot.state=APP_STATE_RUNNING;slot.cpl3=1;slot.as=&space;
    host_cr3=space.pd_phys;CHECK(caller_access_enter(&prev,CALLER_USER));
    api.mem_alloc=sdk_alloc;api.version=69;api.gfx_get_framebuffer=legacy_fb;api.gfx_screen_info=screen;
    gfx_api=&api;shl_gfx_api=&api;gfx_attach_port=&port;shl_gfx_attach_port=&port;
    gfx_unicode_port=&unicode_port;shl_gfx_unicode_port=&unicode_port;
    /* gfx is not initialized: system publisher still works, unready Unicode
     * initialization must not probe stale low bytes. */
    known_table();
    libos32gfx_unicode_init();shl_libos32gfx_unicode_init();
    int uninitialized_null=!utf8_host_pointer() && !shl_utf8_host_pointer();CHECK(uninitialized_null);
    protect_low(0);
    CHECK(!utf8_host_ready() && !shl_utf8_host_ready());
    CHECK(!unicode_to_jis(0x4E9C) && !shl_unicode_to_jis(0x4E9C));
    protect_low(3);CHECK(k_utf8_validate_jis_table());k_utf8_set_jis_table_ready(1);
    CHECK(!system_surface_source(LEDGER_ROLE_UNICODE,&src) && src.ready);
    gfx_set_backend_pref(GFX_PREF_PC98);gfx_prepare_backend();gfx_init();
    /* Static init path and shlib's explicit FFI entry own separate leases. */
    CHECK(!libos32gfx_attach_checked());
    shl_libos32gfx_unicode_init();
    CHECK(!shl_libos32gfx_attach_checked());
    int two_instances=live()==4 && utf8_host_pointer()!=shl_utf8_host_pointer() &&
        (u32)utf8_host_pointer()>=MEM_LEASE_BASE && (u32)shl_utf8_host_pointer()>=MEM_LEASE_BASE;
    CHECK(two_instances);
    protect_low(0);
    CHECK(unicode_to_jis(0x4E9C)==0x3021 && shl_unicode_to_jis(0x9078)==0x412A);
    CHECK(!libos32gfx_attach_checked() && !shl_libos32gfx_attach_checked() && live()==4);
    protect_low(3);
    struct caller_access caller;CHECK(caller_access_get(&caller));
    u8 value=0x5A;
    int ro_output_rejected=copy_to_caller(&caller,(void *)utf8_host_pointer(),&value,1)==0;
    CHECK(ro_output_rejected);
    CHECK(!lease_check(&space));
    CHECK(copy_to_caller(&caller,gfx_fb.planes[0],&value,1));
    u8 readback[2];
    CHECK(copy_caller_bytes(&caller,utf8_host_pointer()+0x4E9C*2,readback,2));
    CHECK(le16_rd(readback)==0x3021);
    CHECK(surface_query_refs(&src,src.refs,1,LEDGER_PERM_RW)==OS32_ERR_INVAL);
    /* TVRAM: foreground only; GUI fails both publisher and authorization. */
    CHECK(!system_surface_source(LEDGER_ROLE_TVRAM,&src) && src.ready);
    CHECK(!surface_query(&src,(void *)MEM_EXEC_LOAD_ADDR));
    struct surface_ref tr=src.refs[0];
    *(struct surface_ref *)P2V(payload)=tr;
    CHECK(!surface_lease(&src,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RO,(void *)(MEM_EXEC_LOAD_ADDR+256)));
    CHECK(!lease_release(&space,((struct lease_view *)P2V(payload+256))->token));
    slot.state=APP_STATE_PARKED;
    CHECK(surface_query(&src,(void *)MEM_EXEC_LOAD_ADDR)==OS32_ERR_INVAL);
    slot.state=APP_STATE_RUNNING;host_gui=1;
    /* Saved ready source verifies the shared authorization gate independently. */
    int tvram_gui_denied=surface_query(&src,(void *)MEM_EXEC_LOAD_ADDR)==OS32_ERR_INVAL;
    CHECK(tvram_gui_denied);
    *(struct surface_ref *)P2V(payload)=tr;
    CHECK(surface_lease(&src,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RO,
          (void *)(MEM_EXEC_LOAD_ADDR+256))==OS32_ERR_INVAL);
    CHECK(!system_surface_source(LEDGER_ROLE_TVRAM,&src) && !src.ready);
    CHECK(!system_surface_source(LEDGER_ROLE_UNICODE,&src) && src.ready);
    CHECK(!surface_query(&src,(void *)MEM_EXEC_LOAD_ADDR));host_gui=0;
    const u32 probes[]={0x4E9C,0x4E00,0x5BFE,0x9078};
    for(u32 i=0;i<4;i++) {
        u8 *p=(u8 *)utf8_host_pointer()+probes[i]*2;
        *p^=1;libos32gfx_unicode_init();
        CHECK(!utf8_host_ready() && !unicode_to_jis(0x4E9C));
        *p^=1;libos32gfx_unicode_init();CHECK(utf8_host_ready());
    }
    /* Actual failure clears both pointer and ready, without low reads. */
    deny_unicode=1;libos32gfx_unicode_init();shl_libos32gfx_unicode_init();
    int failure_null=!utf8_host_pointer() && !shl_utf8_host_pointer();CHECK(failure_null);
    int failure_unready=!utf8_host_ready() && !shl_utf8_host_ready();CHECK(failure_unready);
    protect_low(0);CHECK(!unicode_to_jis(0x4E9C) && !shl_unicode_to_jis(0x9078));protect_low(3);
    CHECK(live()==2);
    deny_unicode=0;corrupt_unicode_view=1;libos32gfx_unicode_init();
    CHECK(!utf8_host_pointer() && !utf8_host_ready() && live()==2);
    corrupt_unicode_view=0;libos32gfx_unicode_init();shl_libos32gfx_unicode_init();
    CHECK(live()==4);
    /* Graphics reinit leaves system leases and generations intact. */
    gfx_init();CHECK(live()==2);
    CHECK(unicode_to_jis(0x4E9C)==0x3021 && shl_unicode_to_jis(0x4E9C)==0x3021);
    /* Fill remaining six: each C instance still uses the same token. */
    struct gfx_attach_desc d;struct gfx_attach_view held[6];CHECK(!port_query(&d));
    for(u32 i=0;i<6;i++) CHECK(!port_lease(&d.ref,&held[i]));
    libos32gfx_unicode_init();shl_libos32gfx_unicode_init();CHECK(live()==8);
    deny_unicode=1;libos32gfx_unicode_init();shl_libos32gfx_unicode_init();deny_unicode=0;
    for(u32 i=0;i<2;i++) {struct gfx_attach_view v;CHECK(!port_lease(&d.ref,&v));}
    libos32gfx_unicode_init();CHECK(!utf8_host_pointer() && !utf8_host_ready() && live()==8);
    CHECK(!lease_revoke_all(&space) && !live());
    libos32gfx_unicode_init();shl_libos32gfx_unicode_init();CHECK(live()==2);
    CHECK(!lease_revoke_all(&space) && !live());
    CHECK(!sf->lease_count);
    /* NULL/CPL0 retain the low automatic probe path in a fresh user instance
     * (explicit setter here restores the fixture's pre-acquisition state). */
    utf8_set_jis_table(P2V(MEM_UNICODE_TABLE_BASE));
    u32 q=unicode_queries;gfx_unicode_port=0;libos32gfx_unicode_init();
    CHECK(unicode_queries==q && unicode_to_jis(0x4E9C)==0x3021);
    gfx_unicode_port=&unicode_port;host_sdk_cpl=0;libos32gfx_unicode_init();
    CHECK(unicode_queries==q);host_sdk_cpl=3;
    utf8_set_jis_table(0);utf8_set_jis_table_ready(1);
    CHECK(!utf8_host_ready() && !unicode_to_jis(0x4E9C));
    gfx_api=0;libos32gfx_shutdown();CHECK(!gfx_ready);
    caller_access_leave(&prev);
    char number[12]; u32 n=checks, len=0;
    do {number[len++]=(char)('0'+n%10);n/=10;} while(n);
    report("PASS checks=",12);
    while(len) report(&number[--len],1);
    SAY("; unicode_surface (two instances, RO, failures, TVRAM, teardown)");
    die(0);
}
