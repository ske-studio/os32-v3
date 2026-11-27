/* Each allocator page has its own offset in a shared host memory file.
 * Inspect its physical alias independently of the VA installed by C7. */
#include "gfx2_sw.h"
#include "paging.h"
#include "redir_access.h"

#define PHYS 0xa00000u
#define PAGES 12u
static int fd, inject_unmap;
static u8 owners[PAGES], pins[PAGES], readback[3 * PAGE_SIZE];
static struct addrspace app = {.owner = 7, .generation = 11, .pd_phys = PAGE_SIZE};
static u32 cr3 = PAGE_SIZE;
static GfxBackend v1, trident;
u32 gfx2_pal[256];

static int host_call(u32 nr, u32 a, u32 b)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(nr), "b"(a), "c"(b) : "memory");
    return result;
}
static int map_pages(u32 va, u32 bytes, u32 offset)
{
    u32 args[] = {va, bytes, 3, 0x11, (u32)fd, offset};
    return (u32)host_call(90, (uptr)args, 0) == va ? 0 : -1;
}

int paging_gfx2_map(struct addrspace *as, u32 va, u32 pa, u32 n, u32 flags)
{
    if (as != &app || pa < PHYS || (pa-PHYS)/PAGE_SIZE + n > PAGES) return -1;
    return map_pages(va, n * PAGE_SIZE, pa - PHYS);
}
int paging_lease_unmap(struct addrspace *as, u32 va, u32 n)
{ return inject_unmap ? -1 : host_call(91, va, n * PAGE_SIZE); }
u32 paging_current_cr3(void) { return cr3; }
u32 paging_pte_flags(u32 va) { return 0; }
u32 pgalloc_arena_end(void) { return PHYS / PAGE_SIZE + PAGES; }
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 flags, u32 *pfn)
{
    for (u32 i = 0; n > 0 && i + (u32)n <= PAGES; i += 2) {
        u32 j = 0;
        while (j < (u32)n && !owners[i+j]) j++;
        if (j != (u32)n) continue;
        for (j = 0; j < (u32)n; j++) owners[i+j] = owner;
        *pfn = PHYS/PAGE_SIZE + i;
        return 1;
    }
    return 0;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    u32 i = pfn - PHYS/PAGE_SIZE;
    if (i >= PAGES || n <= 0 || (u32)n > PAGES-i) return 0;
    for (int j = 0; j < n; j++) if (owners[i+j] != owner || pins[i+j]) return 0;
    for (int j = 0; j < n; j++) owners[i+j] = 0;
    return 1;
}
int pgalloc_pin(u32 pfn, u32 n, u32 owner)
{ u32 i = pfn-PHYS/PAGE_SIZE; if (i >= PAGES || owners[i] != owner || n != 1) return 0; pins[i]++; return 1; }
int pgalloc_unpin(u32 pfn, u32 n, u32 owner)
{ u32 i = pfn-PHYS/PAGE_SIZE; if (i >= PAGES || !pins[i] || owners[i] != owner || n != 1) return 0; pins[i]--; return 1; }
int caller_access_get_user(struct caller_access *out)
{ out->owner = app.owner; out->as = &app; out->app_id = 1; return 1; }
i32 gfx_screen_owner(void) { return 1; }
int ring3_call_from_user(void) { return 0; }
int ring3_user_range_ok(u32 va, u32 size) { return 0; }
int ring3_user_range_writable(u32 va, u32 size) { return 0; }
void *kmemset(void *p, int v, u32 n) { u8 *d=p; while(n--) *d++=v; return p; }
void *kmemcpy(void *p, const void *q, u32 n) { u8 *d=p; const u8 *s=q; while(n--) *d++=*s++; return p; }
void gfx_policy_reeval(void) {}
#ifndef GFX2_TEARDOWN_BODY
void gfx2_quarantine_drain(struct gfx2_adapter *a) {}
#endif
struct GfxSyncCtx { int mode; };
static u32 pushed;
int gfxsync_mode(const GfxSyncCtx *ctx) { return ctx->mode; }
int gfxsync_read_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, u8 *p, u32 pitch) { return OS32_ERR_INVAL; }
int gfxsync_write_rows(GfxSyncCtx *c, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch) { pushed++; return 0; }
int gfxsync_flush(GfxSyncCtx *ctx) { return 0; }
void kprintf(u8 attr, const char *fmt, ...) {}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner) { *owner=8; return 1; }
int v1_bridge_rebind(void) { return 0; }
void gfx_policy_scan_changed(void) {}
int appslot_gui_present_ok(void) { return 1; }
int gfx2_caps_from_v1(struct gfx2_adapter *a, GFX2_Caps *out)
{
    *out=(GFX2_Caps){.max_w=640,.max_h=480,.fmt_surface=1u<<GFX2_FMT_INDEX8,.max_pitch=1024,.pitch_align=1};
    return 0;
}
void gfx2_caps_refresh(struct gfx2_adapter *a) { a->be->caps(a->ctx,&a->caps); }
int gfx2_v1_enum_head(const GfxBackend *v,u32 h,GFX2_HeadInfo *o) { return OS32_ERR_NOSYS; }
int gfx2_v1_enum_conn(const GfxBackend *v,u32 c,GFX2_ConnectorInfo *o) { return OS32_ERR_NOSYS; }
int gfx2_v1_enum_mode(const GfxBackend *v,u32 h,u32 f,u32 i,GFX2_ModeInfo *o) { return OS32_ERR_NOSYS; }
static int surf_ok(void *ctx, struct gfx2_surf *s) { return 0; }
static int stop(void *ctx) { return 0; }
static int bind(void *ctx, const u32 *pfns, u32 n, u32 *off) { *off=0; return 0; }
static int unbind(void *ctx, u32 off, u32 n) { return 0; }
static int exercise(struct gfx2_adapter *a, u32 pool)
{
    u32 done, fence, sid, gen=a->gen;
    if (gfx2_surface_create(a->index,64,192,GFX2_FMT_INDEX8,pool,GFX2_USE_RENDER2D,&sid)) return 1;
    GFX2_Op ops[2] = {{.op=GFX2_OP_FILL, .dst=sid, .dw=64, .dh=192, .fg.v=0x5a, .rop=GFX2_ROP_PATCOPY},
                      {.op=GFX2_OP_READBACK, .src=sid, .sw=64, .sh=192, .pitch=64, .data=(uptr)readback}};
    if (pool == GFX2_POOL_GART) {
        int lookup;
        struct gfx2_exec ex = {.op=&ops[0], .dst=gfx2_surf_get(sid, app.owner, &lookup)};
        u32 how;
        if (!ex.dst || gfx2_check_exec(a,&ex,&how) || how!=GFX2_HOW_SW) return 2;
        cr3++;
        if (gfx2_check_exec(a,&ex,&how) || how!=GFX2_HOW_NOSUPPORT) return 2;
        int rc=gfx2_submit(a->index,ops,2,gen,&done,&fence);
        cr3--;
        if (rc!=OS32_ERR_NOSYS || done) return 2;
    }
    kmemset(readback,0,sizeof(readback));
    if (gfx2_submit(a->index,ops,2,gen,&done,&fence) || done!=2) return 3;
    for (u32 i=0;i<sizeof(readback);i++) {
        u32 page=i/PAGE_SIZE, offset=i%PAGE_SIZE;
        u32 phys=PHYS+(pool==GFX2_POOL_GART ? 2*page : page)*PAGE_SIZE;
        if (readback[i]!=0x5a || *(u8 *)(uptr)(phys+offset)!=0x5a) return 4;
    }
    u32 completed, failed;
    if (gfx2_fence_query(a->index,&completed,&failed) || failed || completed!=a->fence.issued) return 8;
    if (gfx2_surface_free(sid)) return 5;
    for (u32 i=0;i<PAGES;i++) if (owners[i] || pins[i]) return 5;
    {
        gfx2_adapter_unregister(a->v1);
        if (a->be!=&gfx2_lost_be) return 6;
        if (gfx2_submit(a->index,NULL,0,gen,&done,&fence)!=OS32_ERR_STALE) return 6;
    }
    return 0;
}
static int hw_check(void *ctx, const struct gfx2_exec *ex) { return 0; }
static int published(struct gfx2_adapter *a)
{
    GfxSyncCtx direct={GFXSYNC_DIRECT}, shadow={GFXSYNC_SHADOW}, *got;
    static u8 pixels[64];
    struct gfx2_alias_desc d={.adapter=a->index, .role=GFX2_ROLE_DISPLAY, .w=8, .h=8,
        .fmt=GFX2_FMT_INDEX8, .pool=GFX2_POOL_VRAM, .bytes=64, .pitch=8, .kva=pixels};
    struct gfx2_surf *s;
    GFX2_Op fill={.op=GFX2_OP_FILL,.rop=GFX2_ROP_PATCOPY,.dw=8,.dh=8,.fg.v=0x71};
    struct gfx2_exec ex={.op=&fill};
    GfxBackendV2 device=*a->be;
    device.check=hw_check;
    const GfxBackendV2 *saved=a->be;
    a->be=&device; a->heads=1;
    if (gfx2_surf_alias(a,&d,&s)) return 10;
    ex.dst=s; fill.dst=s->sid;
    void *va; u32 pitch, how, done, fence;
    if (gfx2_sync_publish(a->kind,0,&direct) ||
        gfx2_lock(s->sid,GFX2_LOCK_DISCARD|GFX2_LOCK_READONLY,&va,&pitch) ||
        !s->staging || (uptr)va!=s->map_va || gfx2_unlock(s->sid) ||
        gfx2_check_exec(a,&ex,&how) || how!=GFX2_HOW_HW ||
        gfx2_present(a->index,0,0,0,8,8) || pushed) return 10;
    if (gfx2_sync_publish(a->kind,0,&shadow) ||
        gfx2_lock(s->sid,0,&va,&pitch)!=OS32_ERR_INVAL ||
        gfx2_check_exec(a,&ex,&how) || how!=GFX2_HOW_SW ||
        gfx2_submit(a->index,&fill,1,a->gen,&done,&fence) || done!=1 ||
        pixels[0]!=0x71 || pixels[63]!=0x71 || pushed ||
        gfx2_present(a->index,0,0,0,8,8) || pushed!=1) return 11;
    if (gfx2_sync_publish(a->kind,0,NULL) ||
        gfx2_sync_get(a,0,&got,&how)!=OS32_ERR_NOTFOUND ||
        gfx2_check_exec(a,&ex,&how) || how!=GFX2_HOW_HW ||
        gfx2_present(a->index,0,0,0,8,8) || pushed!=1) return 12;
    a->be=saved;
    return 0;
}

#ifdef GFX2_TEARDOWN_BODY
#include "gfx2_exec_teardown.inc"
#endif

static int run(void)
{
    struct gfx2_adapter *a=&gfx2_adapters[0];
    if (gfx2_adapter_register(&trident,GFX2_KIND_TRIDENT,GFX2_AF_ACTIVE) ||
        a->be!=&gfx2_v1_shim) return 9;
    fd=host_call(356,(uptr)"gfx2-pages",0);
    if (fd<0 || host_call(93,fd,PAGES*PAGE_SIZE) || map_pages(PHYS,PAGES*PAGE_SIZE,0)) return 7;
#ifdef GFX2_TEARDOWN_BODY
    return teardown(a);
#endif
    int rc=published(a);
    if (!rc) rc=exercise(a,GFX2_POOL_SYSMEM);
    if (rc) return rc;
    if (gfx2_adapter_register(&v1,GFX2_KIND_PC98,GFX2_AF_ACTIVE)) return 9;
    a=gfx2_active();
    if (!a || a->be!=&gfx2_v1_shim) return 9;
    rc=exercise(a,GFX2_POOL_SYSMEM);
    if (rc) return rc;
    /* The v1 shim has no GART. Supply just the device hooks for the GART case. */
    if (gfx2_adapter_register(&v1,GFX2_KIND_PC98,GFX2_AF_ACTIVE)) return 9;
    static GfxBackendV2 gart_device;
    gart_device=gfx2_v1_shim;
    gart_device.alloc=surf_ok; gart_device.release=surf_ok;
    gart_device.gart_bind=bind; gart_device.gart_unbind=unbind; gart_device.stop=stop;
    a->be=&gart_device;
    kmemset((void *)(uptr)PHYS,0,PAGES*PAGE_SIZE);
    return exercise(a,GFX2_POOL_GART);
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
