/* e7 PM §9: the app stops both renderers, but cannot detach a successful
 * shlib via the current protocol. Same-generation checks reuse that token;
 * a generation change or exit reclaims it. */
extern KernelAPI *shl_gfx_api;
extern GFX_Framebuffer shl_gfx_fb;
extern int shl_gfx_ready;
extern const struct gfx_attach_port *shl_gfx_attach_port;
extern int shl_libos32gfx_attach_checked(void);
extern int shl_libos32gfx_check(void);
extern void shl_libos32gfx_detach(void);
extern void shl_gfx_pixel(int x, int y, u8 color);
extern u8 shl_gfx_get_pixel(int x, int y);
static int deny_shlib;
static int shl_query(struct gfx_attach_desc *out)
{
    if (deny_shlib) return OS32_ERR_INVAL;
    int deny=deny_query;
    deny_query=0;
    int rc=port_query(out);
    deny_query=deny;
    return rc;
}
static const struct gfx_attach_port shl_port = {shl_query, port_lease, port_unlease};
static int return_both(void)
{
    int b = shl_libos32gfx_check();
    int a = libos32gfx_check();
    if (a || b) {
        libos32gfx_detach(); /* application rollback */
        return 0;
    }
    return 1;
}
static void run(void) __attribute__((used));
static void run(void)
{
    u32 owner;
    CallerAccessFrame prev;
    map_host(0x10000,0xFF0000);
    map_host(WAB_XE10_LINEARWIN_BASE,WAB_XE10_LINEARWIN_SIZE);
    map_host(MEM_LEASE_BASE,0x100000);
    boot();gfx_boot_reserve();
    CHECK(ledger_owner_new(LEDGER_KIND_AS,2,"e7",&owner));
    CHECK(!paging_addrspace_create_lease(&space,owner));
    payload=pgalloc_alloc_phys(owner,1);
    CHECK(payload && !paging_addrspace_map_user(&space,MEM_EXEC_LOAD_ADDR,payload,PAGE_RW|PTE_USER));
    slot.state=APP_STATE_RUNNING;slot.cpl3=1;slot.as=&space;slot.hdr_flags=OS32X_FLAG_GFX;
    host_cr3=space.pd_phys;CHECK(caller_access_enter(&prev,CALLER_USER));
    api.mem_alloc=sdk_alloc;api.version=69;api.gfx_get_framebuffer=legacy_fb;api.gfx_screen_info=screen;
    api.gfx_add_dirty_rect=dirty;
    gfx_api=&api;gfx_attach_port=&port;
    shl_gfx_api=&api;shl_gfx_attach_port=&shl_port;
    for(int pref=GFX_PREF_PC98;pref<=GFX_PREF_CIRRUS;pref++) {
        gfx_set_backend_pref(pref);gfx_prepare_backend();gfx_init();
        CHECK(!libos32gfx_attach_checked() && !shl_libos32gfx_attach_checked());
        CHECK(live()==2 && live()<=MEM_LEASE_MAX);
        int lease_pointer = (u32)gfx_fb.planes[0]>=MEM_LEASE_BASE &&
                            (u32)shl_gfx_fb.planes[0]>=MEM_LEASE_BASE;
        CHECK(lease_pointer);
        u32 gen=space.leases[0].generation;
        CHECK(return_both() && live()==2);
        gfx_init_200();
        CHECK(!live());
        CHECK(return_both());
        int both_new_generation = live()==2 && space.leases[0].generation!=gen &&
                                  space.leases[1].generation!=gen;
        CHECK(both_new_generation);
        int old_bb_not_reused = (u32)gfx_fb.planes[0]==space.leases[1].base &&
                               (u32)shl_gfx_fb.planes[0]==space.leases[0].base;
        CHECK(old_bb_not_reused);
        CHECK(gfx_fb.height==shl_gfx_fb.height);
        CHECK((u32)gfx_fb.height==(pref==GFX_PREF_PC98 ? GFX_HEIGHT_200 : MEM_GFX_BB8_HEIGHT));
        sdk_gfx_pixel(0,0,3);shl_gfx_pixel(0,0,5);
        CHECK(sdk_gfx_get_pixel(0,0)==3 && shl_gfx_get_pixel(0,0)==5);
        /* Seven other CLIENT tokens leave only one of the eight slots.
         * The shlib succeeds, static FULL rolls back and both stop drawing. */
        libos32gfx_detach();shl_libos32gfx_detach();
        struct gfx_attach_desc desc;
        struct gfx_attach_view held[MEM_LEASE_MAX-1];
        CHECK(!port_query(&desc));
        for(u32 i=0;i<MEM_LEASE_MAX-1;i++) CHECK(!port_lease(&desc.ref,&held[i]));
        CHECK(!return_both() && !gfx_ready && shl_gfx_ready && live()==MEM_LEASE_MAX);
        for(u32 i=0;i<MEM_LEASE_MAX-1;i++) CHECK(!port_unlease(held[i].token));
        shl_libos32gfx_detach();
        CHECK(!live());
        CHECK(return_both() && live()==2);
        /* A shlib-only failure must also return the successful static token. */
        deny_shlib=1;
        u32 before=asm_calls;
        if(return_both()) {sdk_gfx_pixel(0,0,7);shl_gfx_pixel(0,0,7);}
        int static_token_returned = !live() && !gfx_ready && !shl_gfx_ready;
        CHECK(static_token_returned);
        CHECK(asm_calls==before);
        deny_shlib=0;
        CHECK(return_both() && live()==2);
        /* Fail only static query after the shlib check; retained shlib token
         * is intentional under §9. Neither renderer may be entered. */
        deny_query=1;
        before=asm_calls;
        if(return_both()) {sdk_gfx_pixel(0,0,7);shl_gfx_pixel(0,0,7);}
        CHECK(!gfx_ready && shl_gfx_ready && live()==1 && asm_calls==before);
        deny_query=0;
        libos32gfx_detach();shl_libos32gfx_detach();
        CHECK(!live() && !lease_check(&space));
    }
    char number[12]; u32 n=checks, len=0;
    do {number[len++]=(char)('0'+n%10);n/=10;} while(n);
    report("PASS checks=",12);
    while(len) report(&number[--len],1);
    SAY("; gfx_reattach");
    die(0);
}
