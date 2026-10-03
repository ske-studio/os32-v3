/* Reuse only the hardware/MMU/AS footings; all policy is production code. */
unsigned int host_arch_if = 0x202U;
#define run original_run
#include "gfx_kernel_fb_host.c"
#undef run
#include "libgfx_attach_internal.h"
unsigned int host_sdk_cpl = 3;
static u32 queries, legacy_calls, draws, releases, asm_calls;
static int bad_view, deny_query, bad_screen;

/* Translate SDK values through actual B1 user storage, not a kernel model. */
static int port_query(struct gfx_attach_desc *out)
{
    struct surface_query_source src;
    struct surface_query_result *q = P2V(payload);
    int rc;
    queries++;
    if (deny_query) return OS32_ERR_INVAL;
    rc=gfx_surface_source(LEDGER_ROLE_CLIENT,&src);
    if (rc) return rc;
    rc=surface_query(&src,(void *)MEM_EXEC_LOAD_ADDR);
    if (rc) return rc;
    CHECK(q->count==1);
    struct surface_desc *d=&q->desc[0];
    out->ref.sid=d->ref.sid; out->ref.generation=d->ref.generation;
    out->format=d->format; out->width=d->width; out->height=d->height;
    out->pitch=d->pitch; out->planes=d->planes; out->bytes=d->bytes;
    for(u32 i=0;i<4;i++) out->plane_offset[i]=d->plane_offset[i];
    return 0;
}
static int port_lease(const struct gfx_attach_ref *ref, struct gfx_attach_view *out)
{
    struct surface_query_source src;
    struct lease_view *view=P2V(payload+256);
    int rc=gfx_surface_source(LEDGER_ROLE_CLIENT,&src);
    if (rc) return rc;
    *(struct surface_ref *)P2V(payload)=(struct surface_ref){ref->sid,ref->generation};
    rc=surface_lease(&src,(void *)MEM_EXEC_LOAD_ADDR,LEDGER_PERM_RW,
                     (void *)(MEM_EXEC_LOAD_ADDR+256));
    if (rc) return rc;
    out->token=view->token;out->base=view->base;out->bytes=view->bytes;
    for(u32 i=0;i<4;i++) out->planes[i]=view->planes[i];
    if(bad_view) out->planes[0]++;
    return 0;
}
static int port_unlease(u32 token) { releases++;return lease_release(&space,token); }
static const struct gfx_attach_port port={port_query,port_lease,port_unlease};
static void __cdecl legacy_fb(void *out) {legacy_calls++;gfx_get_framebuffer(out);}
static void __cdecl screen(void *out) {
    gfx_screen_info(out);
    if(bad_screen) ((GFX_ScreenInfo *)out)->height=0;
}
static void __cdecl dirty(int x,int y,int w,int h) {(void)x;(void)y;(void)w;(void)h;draws++;}
static void __cdecl raster(void *p) {(void)p;draws++;}
static KernelAPI api;
static u8 sdk_pool[4*1024*1024];
static u32 sdk_used, sdk_allocations;
static void *__cdecl sdk_alloc(u32 n) {
    n=(n+15)&~15U;CHECK(n<=sizeof(sdk_pool)-sdk_used);
    void *p=sdk_pool+sdk_used;sdk_used+=n;sdk_allocations++;return p;
}
/* Assembly entry counters make the C wrappers' failure gates observable. */
void __cdecl asm_gfx_clear(u8 c,u8 **p) {(void)c;(void)p;asm_calls++;}
void __cdecl asm_gfx_hline(u8 **p,int b,int x,int xx,u8 c) {(void)p;(void)b;(void)x;(void)xx;(void)c;asm_calls++;}
void __cdecl asm_gfx_line(u8 **p,int b,int x,int y,int xx,int yy,u8 c) {(void)p;(void)b;(void)x;(void)y;(void)xx;(void)yy;(void)c;asm_calls++;}
void __cdecl asm_fill_plane_rect(u8 *p,int b,int r,int w,u8 c) {(void)p;(void)b;(void)r;(void)w;(void)c;asm_calls++;}
void __cdecl asm_copy_plane_rect(u8 *d,int dp,const u8 *s,int sp,int h,int w) {(void)d;(void)dp;(void)s;(void)sp;(void)h;(void)w;asm_calls++;}
void __cdecl asm_blit_transparent_core(u8 **p,int off,int pitch,const u8 **s,int m,int sp,int w,int h) {(void)p;(void)off;(void)pitch;(void)s;(void)m;(void)sp;(void)w;(void)h;asm_calls++;}
void __cdecl asm_gfx_draw_sprite_core(int x,int y,int w,int h,int pitch,const u8 **p,const u8 *m,u8 **b) {(void)x;(void)y;(void)w;(void)h;(void)pitch;(void)p;(void)m;(void)b;asm_calls++;}
void __cdecl asm_kcg_draw_font(int x,int y,const u8 *p,int w,int h,u8 c,u8 **b) {(void)x;(void)y;(void)p;(void)w;(void)h;(void)c;(void)b;asm_calls++;}
static u32 live(void) {u32 n=0;for(u32 i=0;i<MEM_LEASE_MAX;i++) n+=space.leases[i].token!=0;return n;}
static void empty(void)
{
    CHECK(!gfx_ready && !gfx_fb.width && !gfx_fb.height && !gfx_fb.pitch);
    for(u32 i=0;i<4;i++) CHECK(!gfx_fb.planes[i]);
}
static void no_drawing(void)
{
    u32 old=draws, oldasm=asm_calls;
    sdk_gfx_clear(3);sdk_gfx_fill_rect(0,0,8,8,3);
    sdk_gfx_blit(0,0,0,0);sdk_gfx_blit_colorkey(0,0,0,0,0);sdk_gfx_blit_transparent(0,0,0,0);
    sdk_gfx_draw_sprite(0,0,0);sdk_gfx_sprite_save_bg(0,0,0);sdk_gfx_sprite_restore_bg(0,0,0);
    sdk_gfx_save_rect(0,0,8,8,0);sdk_gfx_restore_rect(0,0,8,8,0);
    sdk_gfx_draw_font(0,0,0,1,1,3);
    sdk_gfx_present();sdk_gfx_present_with_raster(0);sdk_gfx_present_raster_only(0);
    CHECK(draws==old && asm_calls==oldasm);
}
static void run(void) __attribute__((used));
static void run(void)
{
    u32 owner;
    CallerAccessFrame prev;
    map_host(0x10000,0xFF0000);
    map_host(WAB_XE10_LINEARWIN_BASE,WAB_XE10_LINEARWIN_SIZE);
    map_host(MEM_LEASE_BASE,0x100000); /* host VA backing, PTEs still real */
    boot();gfx_boot_reserve();
    CHECK(ledger_owner_new(LEDGER_KIND_AS,2,"e6",&owner));
    CHECK(!paging_addrspace_create_lease(&space,owner));
    payload=pgalloc_alloc_phys(owner,1);
    CHECK(payload && !paging_addrspace_map_user(&space,MEM_EXEC_LOAD_ADDR,payload,PAGE_RW|PTE_USER));
    slot.state=APP_STATE_RUNNING;slot.cpl3=1;slot.as=&space;slot.hdr_flags=OS32X_FLAG_GFX;
    host_cr3=space.pd_phys;CHECK(caller_access_enter(&prev,CALLER_USER));
    api.mem_alloc=sdk_alloc;api.version=69;api.gfx_get_framebuffer=legacy_fb;api.gfx_screen_info=screen;
    api.gfx_add_dirty_rect=dirty;api.gfx_present_raster=raster;
    gfx_api=&api;gfx_attach_port=&port;
    /* The very first attach fails: present must recover usable pools. */
    gfx_set_backend_pref(GFX_PREF_PC98);gfx_prepare_backend();gfx_init();
    struct gfx_attach_desc first_desc;struct gfx_attach_view first_full[MEM_LEASE_MAX];
    CHECK(!port_query(&first_desc));
    for(u32 i=0;i<MEM_LEASE_MAX;i++) CHECK(!port_lease(&first_desc.ref,&first_full[i]));
    CHECK(libos32gfx_attach_checked()==OS32_ERR_FULL);empty();
    for(u32 i=0;i<MEM_LEASE_MAX;i++) CHECK(!port_unlease(first_full[i].token));
    sdk_gfx_present();CHECK(gfx_ready && live()==1);
    CHECK(sdk_allocations==1);
    GFX_Surface *kept_surface=sdk_gfx_create_surface(8,8);CHECK(kept_surface);
    sdk_gfx_surface_pixel(kept_surface,0,0,3);
    GFX_Sprite *kept_sprite=sdk_gfx_create_sprite(kept_surface,0);CHECK(kept_sprite);
    sdk_gfx_blit(0,0,kept_surface,0);sdk_gfx_draw_sprite(0,0,kept_sprite);
    CHECK(!libos32gfx_attach_checked());
    GFX_Surface *next_surface=sdk_gfx_create_surface(8,8);
    GFX_Sprite *next_sprite=sdk_gfx_create_sprite(kept_surface,0);
    CHECK(next_surface && next_surface!=kept_surface);
    CHECK(next_sprite && next_sprite!=kept_sprite);
    CHECK((kept_surface->planes[0][0]&0x80) && (kept_surface->planes[1][0]&0x80));
    sdk_gfx_free_sprite(next_sprite);sdk_gfx_free_sprite(kept_sprite);
    sdk_gfx_free_surface(next_surface);sdk_gfx_free_surface(kept_surface);
    libos32gfx_detach();
    for(int pref=GFX_PREF_PC98;pref<=GFX_PREF_CIRRUS;pref++) {
        gfx_set_backend_pref(pref);gfx_prepare_backend();gfx_init();
        CHECK(!libos32gfx_attach_checked() && gfx_ready && live()==1);
        CHECK((u32)gfx_fb.planes[0]>=MEM_LEASE_BASE && !legacy_calls);
        u32 token=space.leases[0].token;
        CHECK(!libos32gfx_attach_checked() && live()==1 && space.leases[0].token==token);
        u32 q=queries;
        CHECK(!libos32gfx_check() && queries==q+1 && live()==1);
        q=queries;u32 before=draws;sdk_gfx_present();
        CHECK(queries==q+1 && draws==before+1);
        sdk_gfx_pixel(0,0,3);CHECK(sdk_gfx_get_pixel(0,0)==3);sdk_gfx_clear(3);
        GFX_Framebuffer *bridge=P2V(payload+512);
        gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
        CHECK(live()==2 && !slot.abort_req);
        CHECK((u32)bridge->planes[0]==space.leases[1].base);
        CHECK(bridge->width==gfx_fb.width && bridge->height==gfx_fb.height);
        for(u32 i=0;i<4;i++) {
            CHECK((bridge->planes[i]!=0)==(gfx_fb.planes[i]!=0));
            if(bridge->planes[i]) CHECK(bridge->planes[i]-bridge->planes[0]==gfx_fb.planes[i]-gfx_fb.planes[0]);
        }
        CHECK(!lease_check(&space));
        CHECK(!lease_release(&space,space.leases[1].token));
        CHECK(draws && (gfx_packed || asm_calls));
        gfx_init_200();
        CHECK(!live());
        CHECK(!libos32gfx_check() && live()==1 && space.leases[0].token!=token);
        CHECK((u32)gfx_fb.height==(pref==GFX_PREF_PC98 ? GFX_HEIGHT_200 : MEM_GFX_BB8_HEIGHT));
        if(pref==GFX_PREF_PC98) CHECK(gfx_fb.planes[1]-gfx_fb.planes[0]==GFX_PLANE_SZ);
        deny_query=1;
        CHECK(libos32gfx_check()==OS32_ERR_INVAL && !live());empty();no_drawing();
        deny_query=0;bad_view=1;
        CHECK(libos32gfx_attach_checked()==OS32_ERR_INVAL && !live());empty();
        bad_view=0;bad_screen=1;
        CHECK(libos32gfx_attach_checked()==OS32_ERR_INVAL && !live());empty();
        bad_screen=0;
        CHECK(!libos32gfx_attach_checked());
        gfx_shutdown();
        CHECK(libos32gfx_check()==OS32_ERR_INVAL && !live());empty();no_drawing();
    }
    gfx_set_backend_pref(GFX_PREF_PC98);gfx_prepare_backend();gfx_init();
    /* Force a new VA after revoke; retaining the old BB must be observable. */
    CHECK(!libos32gfx_attach_checked());u8 *old=gfx_fb.planes[0];
    gfx_init();struct gfx_attach_desc d;struct gfx_attach_view occupied;
    CHECK(!port_query(&d) && !port_lease(&d.ref,&occupied));
    CHECK(!libos32gfx_check() && gfx_fb.planes[0]!=old && live()==2);
    libos32gfx_detach();CHECK(!port_unlease(occupied.token));
    /* Eight occupied slots: FULL never falls back to an alias. */
    struct gfx_attach_view full[MEM_LEASE_MAX];
    for(u32 i=0;i<MEM_LEASE_MAX;i++) CHECK(!port_lease(&d.ref,&full[i]));
    CHECK(libos32gfx_attach_checked()==OS32_ERR_FULL && live()==MEM_LEASE_MAX);empty();
    for(u32 i=0;i<MEM_LEASE_MAX;i++) CHECK(!port_unlease(full[i].token));
    /* TRUSTED SDK skips query; production NULL binding stays legacy at CPL3. */
    u32 q=queries;host_sdk_cpl=0;
    CHECK(!libos32gfx_attach_checked() && queries==q && legacy_calls==1 && !live());
    libos32gfx_detach();host_sdk_cpl=3;gfx_attach_port=0;
    CHECK(!libos32gfx_attach_checked() && queries==q && legacy_calls==2);
    libos32gfx_detach();gfx_attach_port=&port;
    /* Dormant USER bridge: flag metadata must not leak into PTE expectations. */
    GFX_Framebuffer *fb=P2V(payload+512);
    gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
    CHECK(live()==1 && (u32)fb->planes[0]>=MEM_LEASE_BASE && !slot.abort_req);
    u32 token=space.leases[0].token;
    CHECK(space.leases[0].flags & AS_LEASE_GFX_COMPAT);
    CHECK(!lease_check(&space));
    gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
    CHECK(live()==1 && space.leases[0].token==token);
    gfx_init_200();gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
    CHECK(live()==1 && space.leases[0].token!=token && fb->height==GFX_HEIGHT_200);
    gfx_shutdown();u32 fails=gfx_bridge_fail_count;
    gfx_framebuffer_bridge((void *)(MEM_EXEC_LOAD_ADDR+512));
    CHECK(slot.abort_req && gfx_bridge_fail_count==fails+1);
    CHECK(!fb->width && !fb->height && !fb->planes[0] && !fb->planes[1] && !fb->planes[2] && !fb->planes[3]);
    slot.abort_req=0;caller_access_leave(&prev);
    CHECK(caller_access_enter(&prev,CALLER_TRUSTED));
    gfx_init();u32 failed=gfx_bridge_fail_count;
    gfx_framebuffer_bridge(fb);
    CHECK(fb->planes[0]==bb_b && fb->planes[1]==bb_r && !live());
    CHECK(gfx_bridge_fail_count==failed && !slot.abort_req);
    failed=gfx_bridge_fail_count;
    gfx_framebuffer_bridge(0);
    CHECK(gfx_bridge_fail_count==failed+1 && !slot.abort_req);
    caller_access_leave(&prev);
    char digits[12];u32 n=checks,pos=sizeof(digits);
    do {digits[--pos]='0'+n%10;n/=10;} while(n);
    report(digits+pos,sizeof(digits)-pos);SAY(" checks");
    SAY("PASS gfx_attach: 3 backends, reuse/revoke/FULL/rollback/drawing/TRUSTED/bridge");
    die(0);
}
