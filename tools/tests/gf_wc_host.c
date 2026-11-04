/* T1 compiles the real GF WC owner, module stop and HAL lifecycle together. */
#include "geforce_module.h"
#include "hwdev/gf/production.h"
#include "mm/pgalloc.h"
#include "mm/paging.h"
#include "arch/x86/kernel/mtrr_wc.h"
#include "palette.h"
#define geforce_backend_ops gfx_backend_geforce
const unsigned int __os32_abi_1_mem_4_fmt_4 = 0;
#define DISPLAY 0x30000000U
#define REGS 0x24000000U
static void die(int line)
{
    char msg[]="FAIL WC line 0000\n";
    for(int i=16;i>=13;--i) { msg[i]='0'+line%10;line/=10; }
    __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(msg),"d"(18) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(1) : "memory");
    __builtin_unreachable();
}
#define CHECK(c) do { if(!(c)) die(__LINE__); } while(0)
struct ledger_region ledger_regions[LEDGER_MAX_REGIONS];
struct ledger_resource ledger_resources[LEDGER_MAX_RESOURCES];
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
u32 ledger_region_count;
static struct geforce_platform plat;
static struct gf_production_resources resources;
static u32 pd[PDE_COUNT] __attribute__((aligned(4096)));
static u32 display_pt[PTE_COUNT] __attribute__((aligned(4096)));
static u32 reg_pt[4][PTE_COUNT] __attribute__((aligned(4096)));
static u32 alias_pt[PTE_COUNT] __attribute__((aligned(4096)));
static u32 saved_pd[PDE_COUNT], saved_display[PTE_COUNT], saved_regs[4][PTE_COUNT];
static struct ledger_region saved_region[LEDGER_MAX_REGIONS];
static struct ledger_resource saved_resource[LEDGER_MAX_RESOURCES];
static struct ledger_surface saved_surface[LEDGER_MAX_SURFACES];
static int ram, identity=1, acquire_calls, release_calls, quiesces, started, presented;
static int fault, result=1, borrowed, restore_fail, effective, baseline, reported;
static u32 flags, cpu_features=0x1020, max_ext=0x80000007U, msr_reads;
static int has_id=1, wc_regs, default_wc;
static unsigned long long gf_wc_msr(u32 reg)
{
    msr_reads++;
    CHECK(has_id && (cpu_features&0x1020)==0x1020);
    if(reg==0x2ff) return default_wc ? 0xc01 : 0xc06;
    if(reg==0xfe) return 0x501;
    if(reg==0x200) return REGS|1;
    CHECK(reg==0x201);
    return wc_regs ? 0xfff000800ULL : 0;
}
static u32 gf_wc_flags(void) { return flags; }
static void gf_wc_set_flags(u32 v) { flags=has_id ? v : v&~(1U<<21); }
static void gf_wc_cpuid(u32 leaf,u32 *a,u32 *b,u32 *c,u32 *d)
{
    *a=*b=*c=*d=0;
    if(!leaf) { *a=1;*b=0x756e6547;*d=0x49656e69;*c=0x6c65746e; }
    else if(leaf==1) *d=cpu_features;
    else if(leaf==0x80000000U) *a=max_ext;
    else { CHECK(leaf==0x80000008U && max_ext>=leaf);*a=36; }
}
struct geforce_platform *geforce_platform_get(void) { return &plat; }
int pgalloc_range_has_ram(u32 a,u32 b) { CHECK(a==DISPLAY/PAGE_SIZE && b==a+128);return ram; }
u32 paging_kernel_pd_phys(void) { return V2P(pd); }
u32 paging_current_cr3(void) { return V2P(pd); }
u32 paging_pte_raw(u32 va)
{
    u32 entry=pd[PDE_INDEX(va)];
    if(!(entry&PTE_PRESENT)) return 0;
    u32 *pt=P2V(entry&PTE_FRAME_MASK);
    return pt[PTE_INDEX(va)];
}
struct ledger_surface *ledger_surface_find(u32 backend,u32 role)
{ CHECK(backend==LEDGER_SF_GEFORCE && role==LEDGER_ROLE_DISPLAY);return &ledger_surfaces[0]; }
int x86_wc_acquire(u32 base,u32 bytes,struct x86_wc_lease *l)
{
    CHECK(started && base==DISPLAY && bytes==0x80000);
    acquire_calls++;
    /* The architecture API requires inactive input, and loses old ownership
     * on reacquire. Catch the caller error before it can hide behind BIOS WC. */
    CHECK(!l->active);
    if(result) { l->active=1;l->owned=!borrowed;l->old_base=baseline;effective=1; }
    return result;
}
int x86_wc_release(struct x86_wc_lease *l)
{
    if(!l->active) return 0;
    release_calls++;
    if(restore_fail) return -1;
    if(l->owned) effective=l->old_base;
    l->active=0;
    return 0;
}
void serial_puts(const char *s) { CHECK(s[0]=='R');reported++; }
#include "gf_wc_source.inc"

static int prepare_identity(void) { return identity; }
int gf_production_prepare(struct gf_production *p,const struct gf_production_resources *r,
                         int (*route)(void *),void *o)
{ (void)r;(void)route;(void)o;p->prepared=true;return 0; }
int gf_production_identity(struct gf_production *p) { (void)p;return identity ? 0 : -1; }
int gf_production_decode_safe(struct gf_production *p) { (void)p;return 0; }
int gf_production_quiesce(struct gf_production *p)
{ (void)p;CHECK(!lease.active || restore_fail);quiesces++;started=0;return 0; }
int gf_production_point(void *p) { (void)p;return identity ? 0 : -1; }
int pc98_route_select_98(void) { return 0; }
#include "../../hwdev/gf/accel.h"
void gf_hook_scan_bind(bool active, const struct gf3_surface *scan)
{ CHECK(!active && !scan); }
#include "tools/tests/gf_accel_no_bind.h"
#define gf3_service_bind gf3_host_no_bind
#include "../../drivers/video/geforce/geforce_body.c"
#undef gf3_service_bind
struct gf1_test_ops gf1_module_ops;
void gf2_test_bind_production(struct gf_foundation *f,struct gf2_chan *ch)
{ (void)f;(void)ch; }
static PaletteEntry colors[16];
static u8 client[640*480];
static const PaletteEntry *fake_colors(void) { return colors; }
static void fake_shadow(int i,u8 r,u8 g,u8 b) { (void)i;(void)r;(void)g;(void)b; }
static int fake_mode(struct video_device *d,u32 i,struct video_mode *m)
{ (void)d;(void)i;m->id=0;return 0; }
static int fake_fb(struct video_device *d,struct video_fb *f)
{ (void)d;f->phys=DISPLAY;f->pitch=640;return 0; }
static int fake_start(struct video_device *d,u32 id,const u8 *rgb)
{ (void)d;(void)id;(void)rgb;started=1;return fault==1; }
static int fake_stop(struct video_device *d) { (void)d;return -1; }
static int fake_palette(struct video_device *d,u32 f,u32 n,const u8 *rgb)
{ (void)d;(void)f;(void)n;(void)rgb;return 0; }
static int fake_present(const u8 *s,u32 sp,volatile u8 *d,u32 dp,volatile u8 *r,
                        int x,int y,int w,int h,int (*p)(void *),void *o)
{
    (void)s;(void)sp;(void)d;(void)dp;(void)r;(void)x;(void)y;(void)w;(void)h;(void)p;(void)o;
    CHECK(started);presented++;
    CHECK(result<=0 || effective==1);
    return fault==3 || !identity;
}
void pc98_console_set_safe(int safe) { (void)safe; }
int pc98_route_cell_check(void) { return 0; }
int pc98_route_to_gf(void) { return fault==4; }
int pc98_route_to_98(void) { CHECK(!lease.active || restore_fail);return 0; }
#define palette_get_all fake_colors
#define palette_shadow_set fake_shadow
#define video_mode_info fake_mode
#define video_framebuffer fake_fb
#define video_start fake_start
#define video_stop fake_stop
#define video_palette fake_palette
#define gf_present_rect fake_present
#include "../../drivers/video/geforce/backend_geforce.c"
void geforce_display_failed(void) {}
int gfx_current_height,gfx_flip_enabled,gfx_display_page;
static GfxCounters counters;
static const struct gf3_test_ops *accel_slot;
static const struct gf1_test_ops *foundation_slot;
static GeforceModuleContext ctx={&gfx_backend_geforce,&counters,&gfx_current_height,
                                &gfx_flip_enabled,&gfx_display_page,&plat,&accel_slot,&foundation_slot};
static void snapshot(void)
{
    __builtin_memcpy(saved_pd,pd,sizeof(pd));
    __builtin_memcpy(saved_display,display_pt,sizeof(display_pt));
    __builtin_memcpy(saved_regs,reg_pt,sizeof(reg_pt));
    __builtin_memcpy(saved_region,ledger_regions,sizeof(ledger_regions));
    __builtin_memcpy(saved_resource,ledger_resources,sizeof(ledger_resources));
    __builtin_memcpy(saved_surface,ledger_surfaces,sizeof(ledger_surfaces));
}
static void unchanged(void)
{
    for(u32 i=0;i<PDE_COUNT;i++) CHECK(pd[i]==saved_pd[i]);
    for(u32 i=0;i<PTE_COUNT;i++) CHECK(display_pt[i]==saved_display[i]);
    for(u32 i=0;i<sizeof(reg_pt)/sizeof(u32);i++) CHECK(((u32 *)reg_pt)[i]==((u32 *)saved_regs)[i]);
    for(u32 i=0;i<sizeof(ledger_regions);i++) CHECK(((u8 *)ledger_regions)[i]==((u8 *)saved_region)[i]);
    for(u32 i=0;i<sizeof(ledger_resources);i++) CHECK(((u8 *)ledger_resources)[i]==((u8 *)saved_resource)[i]);
    for(u32 i=0;i<sizeof(ledger_surfaces);i++) CHECK(((u8 *)ledger_surfaces)[i]==((u8 *)saved_surface)[i]);
}
static void setup(int bios)
{
    CHECK(!lease.active);
    failed=active=routed=0;fault=ram=restore_fail=wc_regs=default_wc=0;identity=has_id=1;
    cpu_features=0x1020;max_ext=0x80000007U;flags=0x202;msr_reads=0;
    acquire_calls=release_calls=quiesces=started=presented=0;result=1;
    borrowed=bios;effective=baseline=bios;
    __builtin_memset(pd,0,sizeof(pd));__builtin_memset(display_pt,0,sizeof(display_pt));
    __builtin_memset(ledger_resources,0,sizeof(ledger_resources));
    resources=(struct gf_production_resources){0};
    resources.bars[0]=(struct recover_bar_extent){REGS,REGS,0x1000000,1};
    resources.bars[1]=(struct recover_bar_extent){DISPLAY|8,DISPLAY,0x10000000,1};
    pd[PDE_INDEX(DISPLAY)]=V2P(display_pt)|PAGE_RW;
    for(u32 i=0;i<75;i++) display_pt[i]=(DISPLAY+i*PAGE_SIZE)|PAGE_RW|PTE_PCD;
    for(u32 di=0;di<4;di++) {
        pd[PDE_INDEX(REGS)+di]=V2P(reg_pt[di])|PAGE_RW;
        for(u32 ti=0;ti<PTE_COUNT;ti++) reg_pt[di][ti]=(REGS+(di*PTE_COUNT+ti)*PAGE_SIZE)|PAGE_RW|PTE_PCD;
    }
    ledger_region_count=1;
    ledger_regions[0]=(struct ledger_region){.first=DISPLAY/PAGE_SIZE,.end=(DISPLAY+0x10000000)/PAGE_SIZE,
        .type=LEDGER_R_DEVICE,.owner=LEDGER_OWNER_GFX,.cache=LEDGER_CACHE_UC,.flags=LEDGER_RF_PERMANENT,.res_mask=1};
    ledger_resources[0]=(struct ledger_resource){.raw_bar=DISPLAY|8,.decode_first=DISPLAY/PAGE_SIZE,
        .decode_end=(DISPLAY+0x10000000)/PAGE_SIZE,.map_first=DISPLAY/PAGE_SIZE,.map_end=DISPLAY/PAGE_SIZE+75,
        .bus=LEDGER_BUS_PCI,.bdf=RECOVER_GF_BDF,.bar=1};
    ledger_surfaces[0]=(struct ledger_surface){.first=DISPLAY/PAGE_SIZE,.npages=75,.cache=LEDGER_CACHE_UC,.backing=LEDGER_SB_MMIO};
    plat=(struct geforce_platform){.resources=&resources,.bar0_phys=REGS,.display_phys=DISPLAY,
        .identify=prepare_identity,.wc_start=geforce_wc_start,.wc_stop=geforce_wc_stop,.report=geforce_wc_report};
    CHECK(!geforce_module_init(&gfx_backend_geforce,&ctx));
    gfx_backend_geforce.bb_base=client;snapshot();
}
static void restored(void) { CHECK(!lease.active && effective==baseline);unchanged(); }
void _start(void)
{
    for(int bios=0;bios<2;bios++) {
        setup(bios);gfx_backend_geforce.init();CHECK(active && acquire_calls==1 && presented==1);
        gfx_backend_geforce.init();CHECK(acquire_calls==1 && presented==1);
        CHECK(geforce_wc_start()==1 && acquire_calls==1); /* Opus A: direct same-mode init too. */
        gfx_backend_geforce.leave();restored();CHECK(release_calls==1);
        CHECK(!plat.stop());restored();CHECK(release_calls==1); /* double stop */
        gfx_backend_geforce.enter();CHECK(active && acquire_calls==2);
        gfx_backend_geforce.shutdown();restored();CHECK(release_calls==2);
        gfx_backend_geforce.init();CHECK(active && acquire_calls==3);
        CHECK(!plat.stop());restored(); /* abort/module recovery uses this same callback */
        for(int f=1;f<=4;f++) {
            setup(bios);fault=f;if(f==2) result=-1;
            gfx_backend_geforce.init();CHECK(failed && !active);restored();
            CHECK(acquire_calls==(f==1 ? 0 : 1));
            CHECK(release_calls==(f==1 ? 0 : 1));
        }
        setup(bios);gfx_backend_geforce.init();identity=0;
        gfx_backend_geforce.present_rect(0,0,1,1);CHECK(failed);restored();
    }
    setup(0);result=0;gfx_backend_geforce.init();CHECK(active && !lease.active);
    gfx_backend_geforce.shutdown();restored();
    for(int gate=0;gate<15;gate++) {
        setup(0);started=1;
        if(gate==0) ram=1;
        if(gate==1) resources.bars[1].raw&=~8U;
        if(gate==2) resources.bars[1].length=0x40000;
        if(gate==3) ledger_regions[0].res_mask|=2;
        if(gate==4) ledger_resources[1]=ledger_resources[0];
        if(gate==5) display_pt[0]|=PTE_PWT;
        if(gate==6) display_pt[0]|=PTE_PS;
        if(gate==7) { pd[0]=V2P(alias_pt)|PAGE_RW;alias_pt[0]=DISPLAY|PAGE_RW|PTE_PCD; }
        if(gate==8) display_pt[75]=(DISPLAY+75*PAGE_SIZE)|PAGE_RW|PTE_PCD;
        if(gate==9) reg_pt[0][0]|=PTE_PWT;
        if(gate==10) wc_regs=1;
        if(gate==11) default_wc=1;
        if(gate==12) has_id=0;
        if(gate==13) cpu_features=0;
        if(gate==14) ledger_surfaces[0].npages=128;
        CHECK(!geforce_wc_start() && !acquire_calls && !lease.active);
        if(gate==12 || gate==13) CHECK(!msr_reads);
        CHECK(flags==0x202);
    }
    setup(0);max_ext=0x80000008U;gfx_backend_geforce.init();CHECK(active);
    gfx_backend_geforce.shutdown();restored();
    setup(0);result=-1;restore_fail=1;gfx_backend_geforce.init();
    CHECK(failed && lease.active && quiesces==1 && !presented);
    CHECK(geforce_wc_start()<0 && acquire_calls==1);restore_fail=0;CHECK(!plat.stop());restored();
    plat.report("R record\n");CHECK(reported==1);
    __asm__ volatile("int $0x80" : : "a"(1),"b"(0) : "memory");
    __builtin_unreachable();
}
