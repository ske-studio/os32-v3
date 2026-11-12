/* Real HAL init -> video_start -> F4 start/restore -> present, libc-free.
 * Reuse the C1 PCI/IRQ/DMA device model. Only route ports, palette owner and
 * CPU BAR mappings, kernel time/allocator and fixed-board devinit are
 * host seams. No pre-filled route, PLL limits, selected_front or CRTC.
 * Independent fields: MIT nouveau c03c1a1 dispnv04/crtc.c:869-903,
 * hw.h:353-372, dac.c:385-415. VGA 640x480 INDEX8, front offset zero.
 */
#include "gf_startlog_host.h"
#define GF4_SERVICE_SOURCE "gf4_scanlog_service.h"
#define GF4_C1_ENTRY scanlog_unused_c1_start
#include "gf4_c1_host.c"
#undef GF4_C1_ENTRY
#include "../../hwdev/gfinit_mode.h"
/* gf4_c2svc_host.c already includes the real F5 service for this fixture. */

/* Scanlog supplies synthetic PCI and host mmap, rather than kernel PTEs. */
int gf_production_identity(struct gf_production *p)
{ assert(p==&cint_prod); return p->revoked ? -1 : 0; }
int gf_production_mapped(struct gf_production *p)
{ assert(p==&cint_prod); return p->foundation.bar0 ? 0 : -1; }

/* Synthetic ROM: DCB 3.0 analog A (bios/dcb.c:28-95,121-193),
 * PLL 0x21 (bios/pll.c), BIT P v2 thermal conversion (bios/therm.c).
 * No parsed route, PLL limit or F5 policy is injected into product state. */
enum { S_ROM_BYTES=GF_INIT_PLL_TABLE+0x100, S_DCB=0x100, S_GPIO=0x140,
       S_BIT=0x180, S_P=0x200, S_THERM=0x280, S_PERF=0x300 };
static u8 scanlog_rom[S_ROM_BYTES];
static struct gf_init_context scanlog_init_ctx;
static struct gf_init_program scanlog_program;
static struct gf_display_owner scanlog_owner;
/* Share the publication slot initialized by svc_fixture_state, too. */
#define scanlog_f5_ops guard_f5_slot
static bool scanlog_calibrated=true,scanlog_rom_fail;
static u32 scanlog_devinit_calls;
static bool scanlog_clock_error;
static u32 scanlog_no_fan;
int scanlog_devinit(struct gf_init_context *ctx,struct gf_init_program *work)
{
    assert(ctx==&scanlog_init_ctx && work==&scanlog_program);
    assert(!cint_prod.display.initialized && !cint_prod.display.baseline_valid);
    ++scanlog_devinit_calls;
    return 0;
}
/* Kernel page allocator/time are host boundaries; production F5 adapters stay real. */
static u8 scanlog_font_pages[0x40000] __attribute__((aligned(4096)));
static u8 scanlog_pages[0x20000] __attribute__((aligned(4096)));
u32 pgalloc_alloc_phys(u32 owner,int pages)
{
    (void)owner;
    if (pages==64) return (u32)(uptr)scanlog_font_pages;
    if (scanlog_rom_fail) return 0;
    assert(pages>0 && (u32)pages*4096<=sizeof(scanlog_pages));
    return (u32)(uptr)scanlog_pages;
}
int pgalloc_free_n_owner(u32 owner,u32 pfn,int pages)
{ (void)owner;(void)pfn;(void)pages; return 1; }
int sys_time_now(u32 *lo,u32 *hi)
{ if (scanlog_clock_error) return -901; *lo=cint_host.now; *hi=0; return 0; }
static void scanlog_put(u32 at,u32 v,u32 bytes)
{ for (u32 i=0;i<bytes;++i) scanlog_rom[at+i]=(u8)(v>>(8*i)); }
static int scanlog_acquire(struct gf_display_context *l)
{
    l->init=&scanlog_init_ctx; l->program=&scanlog_program;
    return 0;
}
static int scanlog_bootpoint(void *owner) { assert(owner==&cint_host); return 0; }
static void scanlog_cold(void)
{
    service_ctx=0; service_slot=0; gf5_links_table()->owner=0;
    svc_fixture_state(true);
    cint_prod.display.initialized=cint_prod.display.baseline_valid=false;
    gf2_host_memset(svc_ctx.limits,0,sizeof(svc_ctx.limits));
    gf2_host_memset(svc_ctx.limits_valid,0,sizeof(svc_ctx.limits_valid));
    svc_ctx.dcb=(struct gf4_dcb_values){0}; svc_ctx.routes=0; svc_ctx.nroutes=0;
    gf2_host_memset(scanlog_rom,0,sizeof(scanlog_rom));
    scanlog_put(0x36,S_DCB,2);
    scanlog_put(S_DCB,0x08010c30,4); scanlog_put(S_DCB+6,0x4edcbdcb,4);
    scanlog_put(S_DCB+10,S_GPIO,2);
    scanlog_put(S_DCB+12,0x010001f0,4); /* analog, no DDC, head A, OR A */
    scanlog_put(S_GPIO,0x010210,3);
    scanlog_put(S_GPIO+3,2|(9U<<5)|(1U<<13),2); /* fan: GPIO2, low0/high1 */
    if (scanlog_no_fan==1) scanlog_put(S_DCB+10,0,2);
    if (scanlog_no_fan==2) scanlog_put(S_GPIO+2,0,1);
    u32 at=GF_INIT_PLL_TABLE;
    scanlog_put(at,GF_PLL_TBL_VERSION,1);
    scanlog_put(at+GF_PLL_HDR_HEADER_LEN,GF_PLL_TBL_HDR_MIN,1);
    scanlog_put(at+GF_PLL_HDR_ENTRY_LEN,GF_PLL_TBL_ENTRY_MIN,1);
    scanlog_put(at+GF_PLL_HDR_COUNT,GF_PLL_TBL_COUNT,1);
    at+=GF_PLL_TBL_HDR_MIN;
    scanlog_put(at+GF_PLL_ENT_VCO1_MIN,100,2); scanlog_put(at+GF_PLL_ENT_VCO1_MAX,500,2);
    scanlog_put(at+GF_PLL_ENT_VCO1_INPUT_MIN,1,2); scanlog_put(at+GF_PLL_ENT_VCO1_INPUT_MAX,27,2);
    scanlog_put(at+GF_PLL_ENT_VCO1_N_MIN,1,1); scanlog_put(at+GF_PLL_ENT_VCO1_N_MAX,255,1);
    scanlog_put(at+GF_PLL_ENT_VCO1_M_MIN,1,1); scanlog_put(at+GF_PLL_ENT_VCO1_M_MAX,27,1);
    scanlog_put(at+GF_PLL_ENT_MAX_P,7,1); scanlog_put(at+GF_PLL_ENT_BIAS_P,1,1);
    const u8 bit[]={0xff,0xb8,'B','I','T',0,0,0,12,6,1,0,'P',2,0x5c,0,0,2};
    gf2_host_memcpy(scanlog_rom+S_BIT,bit,sizeof(bit));
    /* bios/perf.c:30-49: v0x20 header8, PWM divisor100 at+6. */
    scanlog_put(S_P,S_PERF,4); scanlog_put(S_PERF,0x0820,2);
    scanlog_put(S_PERF+6,100,2);
    scanlog_put(S_P+16,S_THERM,4); scanlog_put(S_THERM,0x06030410,4);
    const u8 tags[]={0,1,0x10,0x11,0x12,0x13};
    const u16 values[]={0,0,8,1,1,1};
    for (u32 i=0;i<6;++i) {
        scanlog_put(S_THERM+4+3*i,tags[i],1);
        scanlog_put(S_THERM+5+3*i,scanlog_calibrated ? values[i] : 0,2);
    }
    cint_prod.display.io.point=scanlog_bootpoint;
    scanlog_init_ctx=(struct gf_init_context){.rom=scanlog_rom,.rom_size=sizeof(scanlog_rom),
        .io=cint_prod.display.io,.started_us=cint_host.now};
    scanlog_owner=svc_display_owner; scanlog_owner.acquire=scanlog_acquire;
    cint_prod.display.owner=&scanlog_owner;
    /* Service singleton borrows this same immovable production owner. */
    assert(!gf5_service_init(&cint_prod,&svc_ctx,&scanlog_f5_ops));
    geforce_backend_feature5_bind(&cint_prod.feature5);
    gf3_host_set(&cint_host,0,0x10f0,0x80460064); /* PWM2: divisor100, duty30 */
    scanlog_devinit_calls=0;
    assert(!svc_ctx.ch->f && !svc_ctx.nroutes && !svc_ctx.limits_valid[0]);
}
/* Thermal tests need a readable clock and a supported performance table. */
static void __attribute__((unused)) scanlog_heat_fixture(void)
{
    /* bios/perf.c v0x21: supported empty table (header8, entry22),
     * no lower pstate. The scanlog's v0x20 only supplies its fan divisor. */
    scanlog_rom[S_PERF]=0x21;
    scanlog_rom[S_PERF+3]=22;
    gf5_rom_release(&cint_prod.feature5);
    assert(!gf5_rom_acquire(&cint_prod.feature5,scanlog_rom,sizeof(scanlog_rom),0));
    /* nv40.c:39-114: enabled PLL, 27MHz * 20 / 2, divide by 2;
     * shared core/shader mux, memory's second stage multiplies by two. */
    gf3_host_set(&svc_host.base,0,GF_PLL_GATE,0x23);
    gf3_host_set(&cint_host,0,GF_PLL_GATE,0x23);
    gf3_host_set(&cint_host,0,0x4000,0xc0010000);
    gf3_host_set(&cint_host,0,0x4004,0x02011402);
    gf3_host_set(&cint_host,0,0x4008,0x80011402);
    gf3_host_set(&cint_host,0,0x4020,0xc0010000);
    gf3_host_set(&cint_host,0,0x4024,0x02011402);
}
static PaletteEntry scanlog_colors[16];
const PaletteEntry *palette_get_all(void) { return scanlog_colors; }
void palette_shadow_set(int i,u8 r,u8 g,u8 b) { (void)i;(void)r;(void)g;(void)b; }
static u32 scanlog_route,scanlog_restore98_calls;
void pc98_console_set_safe(int safe) { (void)safe; }
int pc98_route_cell_check(void) { assert(!scanlog_route); return 0; }
int pc98_route_to_gf(void) { scanlog_route=1; return 0; }
int pc98_route_to_98(void)
{
    /* The diagnostic must finish before the real route restores/redraws 98. */
    assert(gf2_host_strstr(startlog,"GF4 STATE stage=leave98 stop_rc="));
    ++scanlog_restore98_calls; /* Route seam includes display_restore_98. */
    scanlog_route=0; return 0;
}
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
static struct video_device scanlog_device;
static void scanlog_init(void)
{
    scanlog_cold();
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
    /* Host mapping seam corresponding to devinit.c's BAR0 loan. */
    cint_prod.foundation.bar0=(volatile u8 *)(uptr)SCANLOG_BAR0;
    cint_prod.display.bar0_map=(void *)(uptr)SCANLOG_BAR0;
    startlog_clear();
    geforce_init();
    startlog_remove_mode_diagnostics();
    assert(active && routed && !failed && scanlog_route && !scanout_logged);
    assert(scanlog_devinit_calls==1 && cint_prod.display.initialized && cint_prod.display.baseline_valid);
    assert((!!cint_prod.feature5.rom.bytes)==!scanlog_rom_fail && scanlog_f5_ops);
    assert(c1_normal==&c1_ops && selected_front.bytes && !selected_front.off);
    /* This synthetic ROM has no usable DDC port. The new diagnostic does
     * not interfere with boot's initial clearing transfer. */
    assert(startlog_equal("[gf4] probe-after-mode conn=0 rc=-412\n"));
}
static void scanlog_finish(void)
{
    geforce_backend_feature5_bind(0);
    gf2_host_memset(cint_host.fail_rc,0,sizeof(cint_host.fail_rc));
    gf2_host_memset(svc_host.fail_rc,0,sizeof(svc_host.fail_rc));
    startlog_clear();
    assert(!geforce_stop());
    assert(gf2_host_strstr(startlog,"GF4 STATE stage=leave98 stop_rc=0 read_rc=0 valid=00000fff"));
    svc_finish();
    gf5_rom_release(&cint_prod.feature5);
}
static u32 scanlog_count(const char *s)
{
    u32 count=0;
    for (u32 i=0;startlog[i];++i) {
        u32 j=0;
        while (s[j] && startlog[i+j]==s[j]) ++j;
        if (!s[j]) ++count;
    }
    return count;
}
static bool scanlog_contains(const char *s) { return scanlog_count(s)!=0; }
static void scanlog_therm_recovery(bool auto_error)
{
    scanlog_init();
    struct gf5 *g=&cint_prod.feature5;
    gf3_host_set(&cint_host,0,0x15b4,0);
    assert(!gf5_service_poll(g));
    assert(g->fan.mode==GF5_FAN_AUTO && g->fan.target==0 && g->fan.duty==27);
    if (auto_error) g->next_auto=0;
    else cint_host.now=(u32)g->next_sensor;
    c1_clear_trace(); startlog_clear();
    gf3_host_fail_at(&cint_host,H_READ,auto_error ? 0 : 1,-903);
    geforce_present(0,0,1,1);
    assert(active && !g->fan.sensor_poll && !g->next_sensor);
    assert(scanlog_contains(auto_error ? "[gf5] therm off rc=-903 reason=auto\n" :
                                        "[gf5] therm off rc=-903 reason=sensor\n"));
    assert(g->fan.mode==GF5_FAN_AUTO && g->next_auto>cint_host.now);
    u32 low=g->fan.duty,auto_len=startlog_len;
    gf2_host_memset(cint_host.fail_rc,0,sizeof(cint_host.fail_rc));
    /* Synthetic calibration gives raw degrees: 90 => boost threshold90,
     * hence max100%, with the reference's first ramp step of3 points. */
    gf3_host_set(&cint_host,0,0x15b4,90);
    cint_host.now=(u32)g->next_auto-1;
    c1_clear_trace();
    geforce_present(0,0,1,1);
    assert(active && !cint_host.calls[H_READ] && g->fan.duty==low);
    ++cint_host.now;
    geforce_present(0,0,1,1);
    assert(active && cint_host.calls[H_READ] && startlog_len==auto_len);
    assert(g->fan.target==100);
    assert(g->fan.duty==low+3 && !g->fan.sensor_poll);
    /* Explicit AUTO must also update immediately despite the stopped monitor. */
    assert(!gf5_fan_mode_set(g,GF5_FAN_NONE));
    gf3_host_set(&cint_host,0,0x15b4,0);
    assert(!gf5_fan_mode_set(g,GF5_FAN_AUTO));
    assert(g->fan.target==0 && g->fan.duty==low && g->next_auto>cint_host.now);
    scanlog_finish();
}
static void scanlog_present(void)
{
    for (u32 y=0;y<400;++y) for (u32 x=0;x<640;++x)
        c1_ram[y*640+x]=(u8)(8+y/60);
    gfx_counters=(GfxCounters){0};
    assert(!gf5_service_poll(&cint_prod.feature5));
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
#ifndef GF4_SCANLOG_ENTRY
#define GF4_SCANLOG_ENTRY _start
#endif
void GF4_SCANLOG_ENTRY(void)
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
    c1_clear_trace(); startlog_clear();
    gf3_host_fail_at(&cint_host,H_CLOCK,0,-902);
    geforce_present(0,0,1,1);
    assert(scanlog_contains("[gf4] present fail stage=transfer rc=-902\n") && !active);
    scanlog_finish(); scanlog_init();
    assert(!c1_hal.set_mode(c1_mode(GF3_INDEX8)));
    startlog_clear();
    selected_front.pitch=1; /* Impossible surface pitch reaches real upload validation. */
    geforce_present(0,0,1,1);
    assert(scanlog_contains("[gf4] present fail stage=transfer rc=-303\n") && !active);
    scanlog_finish(); scanlog_init();
    assert(!c1_hal.set_mode(c1_mode(GF3_INDEX8)));
    /* Diagnostic I/O failures retain the underlying rc and restore owner. */
    scanout_logged=false; startlog_clear(); c1_clear_trace();
    svc_host.cr[0][0x44]=3;
    gf4_host_fail_at(&svc_host,D_READ,1,-901);
    geforce_present(0,0,1,1);
    assert(scanlog_contains("[gf4] scanout fail stage=read rc=-901\n"));
    assert(svc_host.cr[0][0x44]==3 && active);
    gf2_host_memset(svc_host.fail_rc,0,sizeof(svc_host.fail_rc));
    /* A failed transfer must not perform scanout MMIO after identity loss. */
    scanlog_finish();
    scanlog_init(); scanlog_point_fail=true;
    c1_clear_trace(); startlog_clear();
    geforce_present(0,0,640,400);
    assert(scanlog_contains("[gf4] present fail stage=transfer rc=-901\n"));
    assert(!scanlog_contains("[gf4] scanout ") && !active && scanlog_failures==3);
    scanlog_finish();
    scanlog_init(); startlog_clear();
    /* Fail the kernel clock boundary through the production adapter. */
    scanlog_clock_error=true;
    geforce_present(0,0,1,1);
    assert(active && scanlog_failures==3 && cint_prod.feature5.source_rc==-901);
    assert(scanlog_count("[gf5] clock off rc=-901 reason=clock64\n")==1);
    assert(scanlog_contains("[gf4] scanout "));
    u32 clock_len=startlog_len;
    geforce_present(0,0,1,1);
    assert(active && startlog_len>clock_len);
    scanlog_clock_error=false;
    scanlog_finish();
    /* No GPIO table, or a GPIO table without FAN: both are passive boards. */
    for (u32 absent=0;absent<2;++absent) {
        scanlog_no_fan=absent+1;
        scanlog_init();
        startlog_clear(); gfx_counters=(GfxCounters){0};
        geforce_present(0,0,1,1);
        assert(active && cint_prod.feature5.fan_off);
        assert(scanlog_count("[gf5] fan off rc=-510 reason=no-fan\n")==1);
        assert(scanlog_contains("[gf4] scanout ") && gfx_counters.present_bytes==1);
        u32 fan_len=startlog_len;
        cint_host.now+=1000000;
        geforce_present(0,0,1,1);
        assert(active && startlog_len==fan_len && gfx_counters.present_bytes==2);
        scanlog_finish();
    }
    scanlog_no_fan=0;
    /* A scheduled PWM ramp write fails; sensor monitoring remains live. */
    scanlog_init();
    assert(!gf5_service_poll(&cint_prod.feature5));
    cint_host.now=(u32)cint_prod.feature5.next_step;
    c1_clear_trace(); startlog_clear();
    gf3_host_fail_at(&cint_host,H_WRITE,3,-904);
    geforce_present(0,0,1,1);
    assert(active && cint_prod.feature5.fan_off && cint_prod.feature5.fan.sensor_poll);
    assert(scanlog_count("[gf5] fan off rc=-904 reason=fan-pwm\n")==1);
    gf2_host_memset(cint_host.fail_rc,0,sizeof(cint_host.fail_rc));
    u32 pwm_len=startlog_len;
    geforce_present(0,0,1,1);
    assert(active && startlog_len==pwm_len);
    /* A failed step does not cancel AUTO; the production poll retries. */
    struct gf5 *fan=&cint_prod.feature5;
    assert(fan->fan.mode==GF5_FAN_AUTO && fan->next_auto>cint_host.now);
    u32 failed_duty=fan->fan.duty;
    cint_host.now=(u32)fan->next_auto;
    geforce_present(0,0,1,1);
    /* The service retains its previous diagnostic after successful work. */
    assert(active && fan->last_rc==GF5_EIO && fan->source_rc==-904);
    assert(fan->fan.duty!=failed_duty);
    assert(fan->next_auto>cint_host.now && startlog_len==pwm_len);
    scanlog_finish();
    /* AUTO has its own temperature read even when the threshold alarm is not due. */
    scanlog_therm_recovery(true);
    scanlog_therm_recovery(false);
    scanlog_calibrated=false;
    scanlog_init(); startlog_clear();
    geforce_present(0,0,1,1);
    assert(active && !cint_prod.feature5.fan.sensor_poll);
    assert(scanlog_contains("[gf5] therm off rc=-502 reason=sensor auto=unavailable (uncalibrated)\n"));
    assert(cint_prod.feature5.fan.mode==GF5_FAN_NONE && !cint_prod.feature5.next_auto);
    u32 duty;
    assert(!gf5_fan_get(&cint_prod.feature5,&duty) && duty==30);
    assert(gf5_fan_mode_set(&cint_prod.feature5,GF5_FAN_AUTO)==GF5_EINVAL);
    assert(!gf5_fan_get(&cint_prod.feature5,&duty) && duty==30);
    u32 off_len=startlog_len;
    geforce_present(0,0,1,1); assert(startlog_len==off_len && active);
    scanlog_finish();
    /* F5 ROM allocation failure is optional: F4 remains usable. */
    scanlog_rom_fail=true; scanlog_init();
    startlog_clear(); geforce_present(0,0,1,1);
    assert(active && scanlog_contains("[gf5] therm off rc=-502 reason=init auto=unavailable (uncalibrated)\n"));
    off_len=startlog_len;
    geforce_present(0,0,1,1); assert(startlog_len==off_len && active);
    scanlog_finish();
    /* Initial active state missing: diagnose once instead of reporting success. */
    scanout_logged=false; startlog_clear();
    geforce_present(0,0,1,1);
    assert(scanlog_contains("stage=inactive"));
    u32 len=startlog_len; geforce_present(0,0,1,1); assert(startlog_len==len);
    gf2_host_exit(0);
}
