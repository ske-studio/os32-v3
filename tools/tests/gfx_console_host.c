/* Real surface lifetime plus recording synchronous backend; libc-free ILP32. */
#define _start surface_fixture_start
#include "surface_fixture.inc"
#undef _start
#include "gfx_console.h"

static unsigned owners, modes, flips, locks, ends;
static int mode_rc, flip_rc;
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *out)
{ (void)id; (void)name; if (!out || (kind != LEDGER_KIND_AS && kind != LEDGER_KIND_MODULE && kind != LEDGER_KIND_DEVICE)) return 0; *out = 21; owners++; return 1; }
int ledger_owner_retire(u32 owner) { if (owner != 21 || sys_used) return 0; owners--; return 1; }
int gfx2_cpu_begin(struct gfx2_surf *s, u32 access, u32 dl)
{ (void)access; (void)dl; locks++; return s->state == GFX2_ST_LOST ? OS32_ERR_STALE : 0; }
void gfx2_cpu_end(struct gfx2_surf *s, u32 access, const struct gfx2_rect *r)
{ (void)s; (void)access; (void)r; ends++; }
int gfx2_surf_quiesce(struct gfx2_surf *s, u32 dl) { (void)s; (void)dl; return 0; }
void gfx2_head_detach(struct gfx2_adapter *a, u32 h) { gfx2_scan_ref_swap(a, h, NULL); }
static int enum_mode(void *ctx, u32 h, u32 fmt, u32 i, GFX2_ModeInfo *m)
{
    (void)ctx;
    if (h || fmt != GFX2_FMT_INDEX8 || i) return OS32_ERR_INVAL;
    *m = (GFX2_ModeInfo){.id=1, .w=7, .h=4, .fmt=GFX2_FMT_INDEX8}; return 0;
}
static int mode_set(void *ctx, u32 h, u32 mask, const GFX2_ModeInfo *m, const struct gfx2_surf *s)
{
    (void)ctx;
    if (h || mask != 1 || m->w != 7 || m->h != 4 || s->w != 7 || s->h != 4 ||
        s->pool != GFX2_POOL_SYSMEM || (s->flags & GFX2_SF_ALIAS)) return OS32_ERR_INVAL;
    modes++; return mode_rc;
}
static int flip(void *ctx, u32 h, const struct gfx2_surf *s, u32 flags, u32 dl)
{
    (void)ctx; (void)dl;
    if (h || flags || s->state != GFX2_ST_IDLE || s->pitch != 64 ||
        s->dirty.x != 1 || s->dirty.y != 1 || s->dirty.w != 5 || s->dirty.h != 2) return OS32_ERR_INVAL;
    /* CPU rendering must leave every pitch padding byte unchanged. */
    for (u32 y=0; y<4; y++) for (u32 x=7; x<64; x++)
        if (s->kva[y*s->pitch+x] != 0x55) return OS32_ERR_IO;
    flips++; return flip_rc;
}
static const GfxBackendV2 console_be = {.enum_mode=enum_mode, .mode_set=mode_set, .flip=flip};
static int test_console(void)
{
    struct gfx_console_target t = {0}, other = {0};
    struct gfx2_adapter *a = gfx2_adapters;
    u8 *pixels = NULL;
    reset(); a->be = &console_be; a->gen=9; a->heads=1;
    T(gfx_console_open(0, 1, &t) == OS32_ERR_INVAL);
    mode_rc = OS32_ERR_NOSYS;
    T(gfx_console_open(0, 0, &t) == OS32_ERR_NOSYS && owners==1 && !sys_used);
    mode_rc = 0;
    T(!gfx_console_open(0, 0, &t) && owners==1 && modes==2);
    T(t.width==7 && t.height==4 && t.pitch==64 && t.sid);
    T(a->head[0].scan_sid==t.sid && gfx2_surfs[t.sid&1023].scan_ref==1);
    T(gfx_console_open(0, 0, &other)==OS32_ERR_BUSY);
    kmemset(ram, 0x55, sizeof(ram));
    T(!gfx_console_lock(&t, &pixels) && pixels==ram && locks==1);
    T(gfx_console_lock(&t, &pixels)==OS32_ERR_BUSY);
    T(gfx_console_present(&t)==OS32_ERR_BUSY);
    T(gfx_console_unlock(&t, 1, 1, 7, 2)==OS32_ERR_INVAL && t.locked);
    for (u32 y=1; y<3; y++) for (u32 x=1; x<6; x++) pixels[y*t.pitch+x]=255;
    T(!gfx_console_unlock(&t, 1, 1, 5, 2) && !t.locked && ends==1);
    T(!gfx_console_present(&t) && flips==1);
    flip_rc=OS32_ERR_IO; T(gfx_console_present(&t)==OS32_ERR_IO); flip_rc=0;
    a->state=GFX2_AD_LOST;
    T(gfx_console_lock(&t, &pixels)==OS32_ERR_STALE && gfx_console_present(&t)==OS32_ERR_STALE);
    a->state=GFX2_AD_OK; a->gen++;
    T(gfx_console_present(&t)==OS32_ERR_STALE);
    T(!gfx_console_close(&t) && owners==1 && !sys_used && !a->head[0].scan_sid);
    T(!gfx_console_open(0, 0, &t));
    T(!gfx_console_lock(&t, &pixels));
    T(!gfx_console_close(&t) && owners==1 && !sys_used);
    T(!gfx_console_open(0, 0, &t));
    gfx2_surfs[t.sid&1023].state=GFX2_ST_LOST;
    T(gfx_console_lock(&t, &pixels)==OS32_ERR_STALE);
    T(!gfx_console_close(&t));
    /* An existing boot bridge alias may be replaced, an app scanout may not. */
    struct gfx2_surf *alias;
    struct gfx2_alias_desc d = {.adapter=0, .head=0, .w=7, .h=4,
        .fmt=GFX2_FMT_INDEX8, .pool=GFX2_POOL_SYSMEM, .role=GFX2_ROLE_DISPLAY,
        .pitch=64, .bytes=256, .kva=ram};
    T(!gfx2_surf_alias(a, &d, &alias));
    gfx2_scan_ref_swap(a, 0, alias);
    T(!gfx_console_open(0, 0, &t) && !alias->scan_ref);
    T(!gfx_console_close(&t));
    T(owners==1);
    return 0;
}
void _start(void)
{
    int rc=test_console();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
