/* Real HAL init -> video_start -> F4 start/restore -> present, libc-free.
 * Reuse the C1 PCI/IRQ/DMA device model. Only route ports, palette owner and
 * CPU BAR mappings are host seams. No pre-filled selected_front or CRTC.
 * Independent fields: MIT nouveau c03c1a1 dispnv04/crtc.c:869-903,
 * hw.h:353-372, dac.c:385-415. VGA 640x480 INDEX8, front offset zero.
 */
#include "gf_startlog_host.h"
#define GF4_C1_ENTRY scanlog_unused_c1_start
#include "gf4_c1_host.c"
#undef GF4_C1_ENTRY
#include "../../hwdev/gfinit_mode.h"

static PaletteEntry scanlog_colors[16];
const PaletteEntry *palette_get_all(void) { return scanlog_colors; }
void palette_shadow_set(int i,u8 r,u8 g,u8 b) { (void)i;(void)r;(void)g;(void)b; }
static u32 scanlog_route;
void pc98_console_set_safe(int safe) { (void)safe; }
int pc98_route_cell_check(void) { assert(!scanlog_route); return 0; }
int pc98_route_to_gf(void) { scanlog_route=1; return 0; }
int pc98_route_to_98(void) { scanlog_route=0; return 0; }
static u32 scanlog_failures;
void geforce_display_failed(void) { scanlog_failures++; }
static int scanlog_identify(void) { return 1; }
static bool scanlog_point_fail;
static int scanlog_point(void *owner) { assert(owner==&cint_host); return scanlog_point_fail ? -901 : 0; }
#define SCANLOG_BAR0 0x50000000U
static void scanlog_map(u32 addr,u32 bytes)
{
    /* Linux i386 mmap: anonymous, private, fixed; external host memory only. */
    u32 args[6]={addr,bytes,3,0x32,U32_MAX,0},result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90),"b"(args) : "memory");
    assert(result==addr);
}
static i32 scanlog_clock_fail(void *owner,u64 *now)
{ (void)owner;(void)now; return -901; }
static struct video_device scanlog_device;
static void scanlog_init(void)
{
    svc_fixture();
    svc_links.admission=c1_admission;
    svc_links.scan_changed=gf4_scan_changed;
    cint_host.observe=c1_observe;
    cint_prod.display.bars[GF_MODE_VRAM_BAR]=(struct recover_bar_extent){
        .base=CINT_FB_PHYS,.length=sizeof(c1_ram),.valid=true};
    selected_front=(struct gf3_surface){0};
    failed=active=routed=0;
    scanlog_point_fail=false;
    scanlog_device=(struct video_device){.ops=&gf_video_ops,.context=&cint_prod.display};
    geforce_backend_bind(&scanlog_device,scanlog_identify,SCANLOG_BAR0,scanlog_point,&cint_host);
    startlog_clear();
    geforce_init();
    assert(active && routed && !failed && scanlog_route && !scanout_logged);
    assert(c1_normal==&c1_ops && selected_front.bytes && !selected_front.off);
    assert(startlog_equal("")); /* Boot's initial clearing transfer is not the app present. */
}
static bool scanlog_contains(const char *s)
{
    for (u32 i=0;startlog[i];++i) {
        u32 j=0;
        while (s[j] && startlog[i+j]==s[j]) ++j;
        if (!s[j]) return true;
    }
    return false;
}
static void scanlog_present(void)
{
    for (u32 y=0;y<400;++y) for (u32 x=0;x<640;++x)
        c1_ram[y*640+x]=(u8)(8+y/60);
    gfx_counters=(GfxCounters){0};
    c1_clear_trace();
    geforce_present(0,0,640,400);
    assert(gfx_counters.present_bytes==640U*400U && gfx_counters.commits==1);
    assert(!gf2_host_memcmp(c1_ram,(const void *)(uptr)CINT_FB_PHYS,640U*400U));
    /* Independently enumerate the diagnostic's VGA byte / MMIO word reads.
     * Wrong-width indexed accesses can look correct in a permissive fake. */
    const u32 read_regs[]={0x6013d5U,0x600800U,0x600804U,0x6013d5U,
        0x6013d5U,0x6013d5U,0x6013d5U,0x0c03c5U,0x6013d5U,
        0x6013d5U,0x68052cU,0x680608U};
    u32 reads=0,writes=0;
    for (u32 i=0;i<svc_host.nevents;++i) {
        const struct gf4_host_event *e=&svc_host.events[i];
        assert(e->a[0]==0);
        if (e->op==D_READ) {
            assert(reads<sizeof(read_regs)/sizeof(read_regs[0]));
            u32 reg=read_regs[reads++];
            assert(e->a[1]==reg);
            assert(e->a[2]==((reg==0x6013d5U || reg==0x0c03c5U) ? 1U : 4U));
        } else {
            assert(e->op==D_WRITE && e->a[2]==1);
            assert(e->a[1]==0x6013d4U || e->a[1]==0x6013d5U || e->a[1]==0x0c03c4U);
            ++writes;
        }
    }
    assert(reads==12 && writes==11);
    assert(scanlog_contains("[gf4] scanout head=0 start=00000000 front=00000000 pitch=640 depth=8 output=0 dpms=0 present=cpu bytes=256000 fb=00000000 expected_pitch=640 config=00000002 connector=0 or=1 dac=00000001 test=00100000"));
    assert(svc_host.cr[0][0x13]==80 && !(svc_host.cr[0][0x19]&0xe0));
    assert(!(svc_host.cr[0][0x42]&0x40) && (svc_host.cr[0][0x28]&3)==1);
    assert(!(svc_host.sr[0][1]&0x20) && (svc_host.cr[0][0x17]&0x80));
    assert(!(svc_host.cr[0][0x1a]&0xc0));
    assert(!gf2_host_memcmp(svc_host.lut[0],initial_palette,sizeof(initial_palette)));
    u32 len=startlog_len;
    geforce_present(0,0,640,400);
    assert(startlog_len==len && gfx_counters.present_bytes==2U*640U*400U);
}
void _start(void)
{
    scanlog_map(CINT_FB_PHYS,GF_MODE_VRAM_BYTES);
    scanlog_map(SCANLOG_BAR0,0x101000U);
    scanlog_colors[8]=(PaletteEntry){15,0,0};
    scanlog_init();
    scanlog_present();
    /* Readback comes from HW, not the cached descriptor: expose mismatch. */
    scanout_logged=false;
    gf3_host_set(&svc_host.base,0,0x600800U,0x1000U);
    svc_host.cr[0][0x13]=88;
    svc_host.cr[0][0x28]=2;
    startlog_clear();
    geforce_present(0,0,1,1);
    assert(scanlog_contains("start=00001000 front=00000000 pitch=704 depth=16"));
    svc_host.cr[0][0x13]=80; svc_host.cr[0][0x28]=1;
    gf3_host_set(&svc_host.base,0,0x600800U,0);
    /* Nonzero front uses the real RAM upload and counts successfully moved bytes. */
    assert(!c1_hal.set_mode(c1_mode(GF3_INDEX8)));
    assert(selected_front.off);
    scanout_logged=false; startlog_clear(); gfx_counters=(GfxCounters){0};
    geforce_present(1,2,3,4);
    assert(scanlog_contains("present=upload bytes=12") && gfx_counters.present_bytes==12);
    assert(gfx_counters.commits==1);
    /* Diagnostic I/O failures retain the underlying rc and restore owner. */
    scanout_logged=false; startlog_clear(); c1_clear_trace();
    svc_host.cr[0][0x44]=3;
    gf4_host_fail_at(&svc_host,D_READ,1,-901);
    geforce_present(0,0,1,1);
    assert(scanlog_contains("[gf4] scanout fail stage=read rc=-901\n"));
    assert(svc_host.cr[0][0x44]==3 && active);
    gf2_host_memset(svc_host.fail_rc,0,sizeof(svc_host.fail_rc));
    /* A failed transfer must not perform scanout MMIO after identity loss. */
    assert(!geforce_stop());
    scanlog_init(); scanlog_point_fail=true;
    c1_clear_trace(); startlog_clear();
    geforce_present(0,0,640,400);
    assert(scanlog_contains("[gf4] present fail stage=transfer rc=-1\n"));
    assert(!scanlog_contains("[gf4] scanout ") && !active && scanlog_failures==1);
    svc_finish();
    scanlog_init(); startlog_clear();
    struct gf5 polling={.phase=GF5_RUN,.links={.clock64=scanlog_clock_fail}};
    geforce_backend_feature5_bind(&polling);
    geforce_present(0,0,1,1);
    assert(scanlog_contains("[gf4] present fail stage=poll rc="));
    assert(!active && scanlog_failures==2 && polling.source_rc==-901);
    geforce_backend_feature5_bind(0);
    svc_finish();
    /* Initial active state missing: diagnose once instead of reporting success. */
    scanout_logged=false; startlog_clear();
    geforce_present(0,0,1,1);
    assert(scanlog_contains("stage=inactive"));
    u32 len=startlog_len; geforce_present(0,0,1,1); assert(startlog_len==len);
    gf2_host_exit(0);
}
