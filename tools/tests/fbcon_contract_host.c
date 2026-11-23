/* 期待の出所: 契約 = HAL_V2_DESIGN の surface の失効・lock・dirty・present (docs/decisions/gui/HAL_V2_DESIGN.md)。
 * 現状記録 = PC-98 標準 (PLANAR4) が INDEX8 を open で拒否する振る舞い (2026-11-14 の基準、設計で変えるなら直してよい)。 */
/* Real fbcon -> console target -> surface -> OF/PC98 providers; no libc. */
#define _start surface_fixture_start
#include "surface_fixture.inc"
#undef _start
#include "platform/powermac/pm_console.h"
#include "drivers/video/of_fb/backend_of_fb.h"
#include "platform/powermac/rh_observe.h"
#include "gfx/palette.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(e) do { if (!(e)) { const char msg[] = "FAIL " #e "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); finish(1); } } while (0)
static u8 heap[4096], screen[4096] __attribute__((aligned(4))), shadow[4096];
static u8 frame[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
int vram_scroll_y;

u32 pgalloc_arena_end(void) { return V2P(frame) / PAGE_SIZE + 1; }
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 flags, u32 *out)
{
    u32 pfn = V2P(frame) / PAGE_SIZE;
    REQUIRE(owner == LEDGER_OWNER_KERNEL && n == 1 && first <= pfn && pfn < end);
    REQUIRE(flags == LEDGER_TOP_DOWN);
    *out = pfn;
    return 1;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    REQUIRE(owner == LEDGER_OWNER_KERNEL && n == 1 && pfn == V2P(frame) / PAGE_SIZE);
    return 1;
}

static u8 snapshot[16384];
static struct gfx_console_target target;
static struct gfx2_rect expected;
static int cpu_locked, deny_lock, presents, reads;
static GfxBackendV2 observed;

void *kmalloc(u32 n) { REQUIRE(n <= sizeof(heap)); return heap; }
void kfree(void *p) { REQUIRE(!p || p == heap); }
void *kmemcpy(void *d, const void *s, u32 n)
{ for (u32 i=0; i<n; i++) ((u8 *)d)[i]=((const u8 *)s)[i]; return d; }
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *out)
{ (void)id; (void)name; REQUIRE(kind == LEDGER_KIND_MODULE); *out=21; return 1; }
void gfx2_head_detach(struct gfx2_adapter *a, u32 h) { gfx2_scan_ref_swap(a,h,0); }
static void unchanged(void)
{ for (u32 i=0; i<sizeof(ram); i++) REQUIRE(ram[i] == snapshot[i]); }
int gfx2_cpu_begin(struct gfx2_surf *s, u32 access, u32 dl)
{
    (void)dl; REQUIRE(access == (GFX2_CPU_R | GFX2_CPU_W));
    REQUIRE(!cpu_locked && !target.locked && s->state == GFX2_ST_IDLE);
    unchanged();
    if (deny_lock) return OS32_ERR_BUSY;
    cpu_locked=1; return 0;
}
void gfx2_cpu_end(struct gfx2_surf *s, u32 access, const struct gfx2_rect *r)
{
    (void)access; REQUIRE(cpu_locked && target.locked && s->state == GFX2_ST_LOCKED_RW);
    REQUIRE(r && r->x==expected.x && r->y==expected.y && r->w==expected.w && r->h==expected.h);
    cpu_locked=0; kmemcpy(snapshot,ram,sizeof(ram));
}
int gfx2_surf_quiesce(struct gfx2_surf *s, u32 dl)
{ (void)dl; REQUIRE(!cpu_locked && !target.locked && s->state==GFX2_ST_IDLE); unchanged(); return 0; }
static int present(void *ctx, u32 h, const struct gfx2_surf *s, u32 flags, u32 dl)
{
    REQUIRE(!cpu_locked && !target.locked && s->state==GFX2_ST_IDLE); unchanged();
    REQUIRE(s->dirty.x==expected.x && s->dirty.y==expected.y && s->dirty.w==expected.w && s->dirty.h==expected.h);
    presents++; return of_fb_v2.flip(ctx,h,s,flags,dl);
}
static void ank(u8 ch, u8 *out)
{
    REQUIRE(cpu_locked && target.locked && gfx2_surfs[target.sid & 1023].state==GFX2_ST_LOCKED_RW);
    reads++; for (int i=0; i<16; i++) out[i]=ch==' ' ? 0 : 0x80;
}
static void kanji(u16 ch, u8 *out) { (void)ch; ank('A',out); ank('A',out+16); }
static const KcgModuleApi font = {.read_ank=ank, .read_kanji=kanji};
/* PC98 device operations are unreachable: INDEX8 must be rejected first. */
int gfx_current_height=400, gfx_flip_enabled, gfx_display_page;
GfxCounters gfx_counters;
void gfx_add_dirty_rect(int x,int y,int w,int h)
{ (void)x; (void)y; (void)w; (void)h; REQUIRE(0); }
void gfx_present_dirty(void) { REQUIRE(0); }
void palette_set(int i,u8 r,u8 g,u8 b) { (void)i; (void)r; (void)g; (void)b; REQUIRE(0); }
void palette_shadow_set(int i,u8 r,u8 g,u8 b) { (void)i; (void)r; (void)g; (void)b; REQUIRE(0); }

static void setup(void)
{
    reset(); cpu_locked=deny_lock=presents=reads=0;
    target=(struct gfx_console_target){0};
    kmemset(ram,0xa5,sizeof(ram)); kmemcpy(snapshot,ram,sizeof(ram));
    kmemset(screen,0xa5,sizeof(screen)); kmemset(shadow,0x77,sizeof(shadow));
    struct of_fb_desc d={.display=screen,.shadow=shadow,.width=28,.height=99,
        .pitch=32,.bytes=sizeof(screen),.shadow_pitch=32,.shadow_bytes=sizeof(shadow),
        .protected_rows=PM_RH_FB_ROWS};
    REQUIRE(!of_fb_bind(&d));
    struct gfx2_adapter *a=gfx2_adapters;
    a->v1=&gfx_backend_of_fb; a->ctx=a->v1; a->kind=GFX2_KIND_OF_FB; a->gen=9;
    observed=of_fb_v2; observed.flip=present; a->be=&observed;
    expected=(struct gfx2_rect){0,PM_RH_FB_ROWS,24,32};
    /* Early v1 alias is borrowed; normal fbcon owns a different RAM surface. */
    struct gfx2_alias_desc desc={.adapter=0,.head=0,.w=28,.h=99,.pitch=32,
        .fmt=GFX2_FMT_INDEX8,.pool=GFX2_POOL_SYSMEM,.role=GFX2_ROLE_DISPLAY,
        .kva=shadow,.bytes=sizeof(shadow)};
    struct gfx2_surf *early;
    REQUIRE(!gfx2_surf_alias(a,&desc,&early)); gfx2_scan_ref_swap(a,0,early);
    REQUIRE(!con_backend_safe());
    REQUIRE(!gfx_console_open(0,0,&target) && target.pitch==64);
    REQUIRE(!early->scan_ref && early->kva==shadow && a->head[0].scan_sid==target.sid);
    REQUIRE(!pm_console_attach(&target,&font,PM_RH_FB_ROWS) && con_backend_safe());
    REQUIRE(con_backend_geometry()->cols==3 && con_backend_geometry()->rows==2);
    REQUIRE(presents==2 && !target.locked && !cpu_locked);
}
static void rectangle(int x,int y,int w,int h)
{
    gfx2_surfs[target.sid & 1023].dirty=(struct gfx2_rect){0};
    expected=(struct gfx2_rect){x,y,w,h};
}
static void cleanup(void)
{
    pm_console_detach(); REQUIRE(!con_backend_safe());
    REQUIRE(!gfx_console_close(&target) && !sys_used);
    of_fb_unbind();
}
static void rendering(void)
{
    setup();
    rectangle(0,80,24,16); con_backend_putchar(1,1,'A',FBCON_DEFAULT_ATTR);
    REQUIRE(con_backend_safe() && presents==3 && ram[80*64+8]==0 && ram[80*64+9]==255);
    for (u32 y=0; y<99; y++) for (u32 x=0; x<64; x++) {
        if (y<64 || y>=96 || x>=24) REQUIRE(ram[y*64+x]==0xa5);
        if (x<32) REQUIRE(screen[y*32+x]==(y<64 || x>=28 ? 0xa5 : ram[y*64+x]));
    }
    for (u32 i=0; i<sizeof(shadow); i++) REQUIRE(shadow[i]==0x77);
    int n=presents;
    con_backend_putchar(-1,0,'X',1); con_backend_putchar(3,0,'X',1);
    con_backend_putchar(0,2,'X',1); con_backend_putkanji(2,0,0x2422,1);
    REQUIRE(presents==n); unchanged();
    rectangle(8,80,8,16); REQUIRE(con_backend_reverse(1,1)==1);
    rectangle(0,64,24,32); con_backend_scroll(2);
    REQUIRE(con_backend_safe()); cleanup();
}
static void invalidation(void)
{
    for (int reason=0; reason<5; reason++) {
        setup();
        rectangle(0,64,16,16);
        struct gfx2_adapter *a=gfx2_adapters;
        struct gfx2_surf *s=&gfx2_surfs[target.sid & 1023];
        if (reason==0) { /* Backend replacement publishes a new generation. */
            a->v1=&gfx_backend_pc98; a->be=&gfx2_v1_shim; a->ctx=a; a->gen++;
        }
        if (reason==1) gfx2_scan_ref_swap(a,0,0); /* Mode selected another scanout. */
        if (reason==2) s->state=GFX2_ST_LOST;
        if (reason==3) a->flags=0;
        if (reason==4) deny_lock=1;
        /* safe is readiness, not a proactive generation query: next draw latches failure. */
        REQUIRE(con_backend_safe());
        int n=presents, glyphs=reads;
        con_backend_putchar(0,0,'X',1);
        REQUIRE(!con_backend_safe() && presents==n && reads==glyphs && !target.locked);
        con_backend_clear(); con_backend_scroll(2); con_backend_cursor_shape(1);
        REQUIRE(presents==n && reads==glyphs); unchanged();
        cleanup();
    }
}
static void pc98(void)
{
    reset(); struct gfx2_adapter *a=gfx2_adapters;
    a->v1=&gfx_backend_pc98; a->ctx=a; a->be=&gfx2_v1_shim;
    target=(struct gfx_console_target){0};
    REQUIRE(gfx_console_open(0,0,&target)==OS32_ERR_INVAL && !target.sid && !sys_used);
    u8 *pixels=0;
    REQUIRE(gfx_console_lock(&target,&pixels)==OS32_ERR_STALE && !pixels);
    REQUIRE(gfx_console_present(&target)==OS32_ERR_STALE && !con_backend_safe());
}
void _start(void) { rendering(); invalidation(); pc98(); finish(0); }
