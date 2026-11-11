/* No libc; compile the real C4 TU against deterministic neighbouring stages. */
#include "gfx2_int.h"
#include "kstring.h"

struct gfx2_adapter gfx2_adapters[GFX2_MAX_ADAPTERS];
static struct gfx2_caller caller = {7, (struct addrspace *)1, 11};
static int caller_rc, screen_owner = 7, alloc_rc, release_rc, kva_rc, unmap_rc, map_rc, backing_rc;
static u32 heap_used, sys_used, gart_used, completed;
static char trace[32];
static u32 nt;
static u8 ram[128];
static void note(char c) { trace[nt++] = c; }
void *kmemset(void *d, int v, u32 n) { u8 *p = d; while (n--) *p++ = v; return d; }
struct gfx2_adapter *gfx2_adapter_get(u32 n) { return n < 1 ? gfx2_adapters : 0; }
int gfx2_caller_get(struct gfx2_caller *c) { if (caller_rc) return caller_rc; *c = caller; return 0; }
i32 gfx_screen_owner(void) { return screen_owner; }
int gfx2_fence_done(struct gfx2_adapter *a, u32 f) { (void)a; return !f || f <= completed; }
int gfx2_heap_alloc(struct gfx2_heap *h, u32 n, u32 align, u32 *off)
{ (void)h; (void)align; note('H'); *off = 64; heap_used += n; return 0; }
int gfx2_heap_free(struct gfx2_heap *h, u32 off)
{ (void)h; (void)off; note('h'); if (backing_rc) return backing_rc; heap_used = 0; return 0; }
int gfx2_sysmem_alloc(struct gfx2_surf *s, u32 owner)
{ (void)owner; note('S'); s->kva = ram; sys_used += s->bytes; return 0; }
int gfx2_sysmem_free(struct gfx2_surf *s)
{ (void)s; note('s'); if (backing_rc) return backing_rc; sys_used = 0; return 0; }
int gfx2_gart_alloc(struct gfx2_adapter *a, struct gfx2_surf *s, u32 owner)
{ (void)a; (void)owner; note('G'); s->gart_n = 3; gart_used += s->bytes; return 0; }
int gfx2_gart_free(struct gfx2_adapter *a, struct gfx2_surf *s)
{ (void)a; (void)s; note('g'); if (backing_rc) return backing_rc; gart_used = 0; return 0; }
int gfx2_map(struct gfx2_surf *s, struct addrspace *as, u32 *va)
{ note('M'); if (map_rc) return map_rc; s->map_as = as; s->map_va = 0xf8000000UL; *va = s->map_va; return 0; }
int gfx2_unmap(struct gfx2_surf *s)
{ note('U'); if (unmap_rc) return unmap_rc; s->map_as = 0; s->map_va = 0; return 0; }
void gfx2_quarantine_drain(struct gfx2_adapter *a) { (void)a; }
static int alloc(void *ctx, struct gfx2_surf *s) { (void)ctx; (void)s; note('A'); return alloc_rc; }
static int release(void *ctx, struct gfx2_surf *s) { (void)ctx; (void)s; note('R'); return release_rc; }
static int kva(void *ctx, const struct gfx2_surf *s, u8 **va)
{ (void)ctx; (void)s; note('K'); if (!kva_rc) *va = ram; return kva_rc; }
static const GfxBackendV2 be = {.alloc = alloc, .release = release, .kva = kva};
#define T(e) do { if (!(e)) return __LINE__; } while (0)
static void reset(void)
{
    kmemset(gfx2_surfs, 0, sizeof(gfx2_surfs));
    kmemset(gfx2_adapters, 0, sizeof(gfx2_adapters));
    gfx2_adapters[0].be = &be; gfx2_adapters[0].heads = 2;
    gfx2_adapters[0].flags = GFX2_AF_ACTIVE;
    gfx2_adapters[0].caps = (GFX2_Caps){.max_w=65535, .max_h=65535, .max_pitch=~0UL,
        .fmt_surface=0x7fe, .pitch_align=64, .offset_align=64, .scanout_align=256};
    caller = (struct gfx2_caller){7, (struct addrspace *)1, 11};
    caller_rc = alloc_rc = release_rc = kva_rc = unmap_rc = map_rc = backing_rc = 0;
    heap_used = sys_used = gart_used = completed = nt = 0; screen_owner = 7;
}
static int created(u8 pool, struct gfx2_surf **s)
{ return gfx2_surf_new(gfx2_adapters, 8, 8, GFX2_FMT_XRGB8888, pool, GFX2_USE_RENDER2D, 11, s); }
static int test_create(void)
{
    struct gfx2_surf *s = (void *)2;
    u32 sid;
    reset(); alloc_rc = OS32_ERR_NOSPC;
    T(created(GFX2_POOL_VRAM, &s) == OS32_ERR_NOSPC);
    T(s == (void *)2 && heap_used == 0 && gfx2_surfs[0].state == GFX2_ST_FREE);
    T(nt == 3 && trace[0] == 'H' && trace[1] == 'A' && trace[2] == 'h');
    reset(); kva_rc = OS32_ERR_IO;
    T(created(GFX2_POOL_VRAM, &s) == OS32_ERR_NOSPC);
    T(heap_used == 0 && nt == 5 && trace[3] == 'R' && trace[4] == 'h');
    reset(); map_rc = OS32_ERR_NOSPC;
    T(created(GFX2_POOL_GART, &s) == OS32_ERR_NOSPC);
    T(gart_used == 0 && trace[2] == 'M' && trace[3] == 'R' && trace[4] == 'g');
    reset(); T(created(GFX2_POOL_GART, &s) == 0);
    T(!s->kva && s->map_as == caller.as && s->map_va == 0xf8000000UL);
    reset(); alloc_rc = OS32_ERR_NOSPC; release_rc = OS32_ERR_NOSYS;
    T(created(GFX2_POOL_SYSMEM, &s) == 0);
    T(nt == 1 && trace[0] == 'S');
    T(s->kva == ram && s->pitch == 64 && s->bytes == 512 && s->sid);
    T(gfx2_surface_desc(s->sid, &(GFX2_SurfaceDesc){0}) == 0);
    sid = s->sid; T(gfx2_surface_free(sid) == 0 && sys_used == 0);
    T(nt == 3 && trace[1] == 'U' && trace[2] == 's');
    T(created(GFX2_POOL_SYSMEM, &s) == 0 && s->sid != sid);
    int rc; T(!gfx2_surf_get(sid, 11, &rc) && rc == OS32_ERR_STALE);
    T(!gfx2_surf_get(s->sid, 12, &rc) && rc == OS32_ERR_INVAL);
    reset(); T(gfx2_surf_new(gfx2_adapters, 65535, 65535, GFX2_FMT_XRGB8888,
        GFX2_POOL_VRAM, 1, 11, &s) == OS32_ERR_INVAL);
    T(nt == 0);
    T(gfx2_surf_new(gfx2_adapters, 0, 8, 1, 1, 1, 11, &s) == OS32_ERR_INVAL);
    T(gfx2_surf_new(gfx2_adapters, 8, 8, 0, 1, 1, 11, &s) == OS32_ERR_INVAL);
    T(gfx2_surf_new(gfx2_adapters, 8, 8, 11, 1, 1, 11, &s) == OS32_ERR_INVAL);
    T(gfx2_surf_new(gfx2_adapters, 8, 8, 1, 4, 1, 11, &s) == OS32_ERR_INVAL);
    T(gfx2_surf_new(gfx2_adapters, 8, 8, 1, 1, 0x80, 11, &s) == OS32_ERR_INVAL);
    gfx2_adapters[0].flags = 0; sid = 123;
    T(gfx2_surface_create(0, 8, 8, 1, 1, 1, &sid) == OS32_ERR_NOSYS && sid == 123);
    reset(); for (u32 i=0; i<GFX2_MAX_SURFACES; i++) gfx2_surfs[i].state = GFX2_ST_IDLE;
    T(created(GFX2_POOL_VRAM, &s) == OS32_ERR_NOSPC && !nt);
    return 0;
}
static int test_destroy(void)
{
    struct gfx2_surf *s;
    reset(); T(!created(GFX2_POOL_GART, &s)); nt = 0;
    T(!gfx2_surface_free(s->sid));
    T(nt == 3 && trace[0]=='U' && trace[1]=='R' && trace[2]=='g' && !gart_used);
    for (int failure=0; failure<3; failure++) {
        reset(); T(!created(GFX2_POOL_GART, &s)); nt=0;
        if (failure==0) unmap_rc=OS32_ERR_IO;
        if (failure==1) release_rc=OS32_ERR_BUSY;
        if (failure==2) backing_rc=OS32_ERR_IO;
        T(gfx2_surface_free(s->sid) == OS32_ERR_BUSY);
        T((s->flags & GFX2_SF_KEEP) && gart_used && nt == (u32)failure+1);
        if (failure == 0)
            T(s->map_as == caller.as && s->map_va == 0xf8000000UL);
        nt=0; T(gfx2_surf_destroy(s) == OS32_ERR_BUSY && !nt);
    }
    reset(); T(!created(GFX2_POOL_VRAM, &s)); nt=0;
    gfx2_adapters[0].state=GFX2_AD_LOST_HELD;
    T(gfx2_surface_free(s->sid)==OS32_ERR_BUSY && heap_used && !nt);
    reset(); T(!created(GFX2_POOL_VRAM, &s));
    gfx2_scan_ref_get(s); T(!gfx2_surface_free(s->sid));
    T(s->state==GFX2_ST_CLOSING && heap_used);
    gfx2_closing_sweep(gfx2_adapters); T(heap_used);
    gfx2_scan_ref_put(s); gfx2_closing_sweep(gfx2_adapters);
    T(s->state==GFX2_ST_FREE && !heap_used);
    reset(); T(!created(GFX2_POOL_VRAM, &s)); s->last_read=5; s->last_write=3;
    T(!gfx2_surface_free(s->sid) && s->state==GFX2_ST_CLOSING);
    completed=3; gfx2_closing_sweep(gfx2_adapters); T(heap_used);
    completed=5; gfx2_closing_sweep(gfx2_adapters); T(!heap_used);
    return 0;
}
static int test_alias(void)
{
    struct gfx2_alias_desc d = {.adapter=0,.head=0,.role=GFX2_ROLE_DISPLAY,
        .w=64,.h=32,.fmt=GFX2_FMT_INDEX8,.pool=GFX2_POOL_SYSMEM,.pitch=64,.bytes=2048,.kva=ram};
    struct gfx2_surf *s;
    int rc; u32 sid, screen_sid=0;
    reset(); T(!gfx2_surf_alias(gfx2_adapters, &d, &s)); sid=s->sid;
    T(!nt && s->owner==GFX2_OWNER_SCREEN && s->usage==(GFX2_USE_SCANOUT|GFX2_USE_V1));
    T(gfx2_surf_get(sid, caller.owner, &rc)==s);
    T(!gfx2_screen_surface(0,0,GFX2_ROLE_DISPLAY,&screen_sid) && screen_sid==sid);
    gfx2_adapters[0].flags = 0; screen_sid = 123;
    T(gfx2_screen_surface(0,0,GFX2_ROLE_DISPLAY,&screen_sid)==OS32_ERR_NOSYS && screen_sid==123);
    gfx2_adapters[0].flags = GFX2_AF_ACTIVE;
    caller.app_id=8; T(!gfx2_surf_get(sid,caller.owner,&rc) && rc==OS32_ERR_INVAL);
    T(gfx2_screen_surface(0,0,GFX2_ROLE_DISPLAY,&screen_sid)==OS32_ERR_BUSY);
    caller.app_id=7; T(gfx2_surface_free(sid)==OS32_ERR_INVAL && !nt);
    T(gfx2_surf_destroy(s)==OS32_ERR_INVAL && !nt);
    T(!gfx2_surf_state_set(s,GFX2_ST_LOCKED_RO));
    T(gfx2_surf_alias_update(s,&d)==OS32_ERR_BUSY && s->sid==sid);
    T(!gfx2_surf_state_set(s,GFX2_ST_IDLE)); s->last_write=1;
    T(gfx2_surf_alias_update(s,&d)==OS32_ERR_BUSY && s->sid==sid);
    completed=1; d.kva=ram+1; T(!gfx2_surf_alias_update(s,&d));
    T(s->sid!=sid && s->gen==2 && s->kva==ram+1 && !s->last_write && !nt);
    T(!gfx2_surf_get(sid,caller.owner,&rc) && rc==OS32_ERR_STALE);
    T(!gfx2_surf_state_set(s,GFX2_ST_LOST)); T(!gfx2_surf_alias_update(s,0));
    T(s->state==GFX2_ST_IDLE && !s->w && !s->h && !s->kva);
    return 0;
}
static int test_states(void)
{
    struct gfx2_surf *s;
    reset();
    s = gfx2_surfs;
    for (u32 old=GFX2_ST_FREE; old<=GFX2_ST_QUAR; old++) {
        for (u32 st=GFX2_ST_FREE; st<=GFX2_ST_QUAR+1; st++) {
            int expected = (old==GFX2_ST_IDLE && (st==GFX2_ST_LOCKED_RO || st==GFX2_ST_LOCKED_RW || st==GFX2_ST_CLOSING)) ||
                ((old==GFX2_ST_LOCKED_RO || old==GFX2_ST_LOCKED_RW) && st==GFX2_ST_IDLE) ||
                (old>=GFX2_ST_IDLE && old<=GFX2_ST_CLOSING && st==GFX2_ST_LOST) ||
                (old>=GFX2_ST_IDLE && old<=GFX2_ST_LOST && st==GFX2_ST_QUAR) ||
                (old==GFX2_ST_LOST && st==GFX2_ST_CLOSING);
            s->state=old;
            T(gfx2_surf_state_set(s,st)==(expected ? 0 : OS32_ERR_INVAL));
            T(s->state==(expected ? st : old));
        }
    }
    reset(); T(!created(GFX2_POOL_SYSMEM,&s));
    for (u32 st=GFX2_ST_LOCKED_RO; st<=GFX2_ST_LOCKED_RW; st++) {
        T(!gfx2_surf_state_set(s,st));
        T(gfx2_surface_free(s->sid)==OS32_ERR_BUSY);
        T(gfx2_surf_state_set(s,GFX2_ST_LOCKED_RW)==OS32_ERR_INVAL && s->state==st);
        T(!gfx2_surf_state_set(s,GFX2_ST_IDLE));
    }
    T(gfx2_surf_state_set(s,GFX2_ST_FREE)==OS32_ERR_INVAL);
    T(!gfx2_surf_state_set(s,GFX2_ST_LOST));
    T(!gfx2_surface_desc(s->sid,&(GFX2_SurfaceDesc){0}));
    T(!gfx2_surface_free(s->sid));
    reset(); T(!created(GFX2_POOL_SYSMEM,&s));
    gfx2_dirty_add(s,&(struct gfx2_rect){-2,-2,5,5});
    T(s->dirty.x==0 && s->dirty.y==0 && s->dirty.w==3 && s->dirty.h==3);
    gfx2_dirty_add(s,&(struct gfx2_rect){6,6,100,100});
    T(s->dirty.w==8 && s->dirty.h==8);
    gfx2_scan_ref_swap(gfx2_adapters,0,s); T(s->scan_ref==1);
    gfx2_scan_ref_swap(gfx2_adapters,0,s); T(s->scan_ref==1);
    gfx2_scan_ref_swap(gfx2_adapters,0,0); T(s->scan_ref==0);
    return 0;
}
static int test(void)
{
    int rc=test_create(); if (rc) return rc;
    rc=test_destroy(); if (rc) return rc;
    rc=test_alias(); if (rc) return rc;
    return test_states();
}
void _start(void)
{
    int rc=test();
    /* Exit status 1..255; zero must never hide a failed assertion at line 256. */
    if (rc) rc=1+rc%254;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
