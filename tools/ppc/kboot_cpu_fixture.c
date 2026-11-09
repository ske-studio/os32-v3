/* mac99 CPU takeover fixture, product C1/C2/D1 used unchanged. */
#include "ofw.h"
#include "ppc_frame.h"
#include "ppc_hpt.h"
#include "ppc_pd.h"
#define GUARD_LOW 0x00300000U
#define GUARD_HIGH 0x70000000U
#define TEST_VA 0x50000000U
#define IO_VA 0xfe300000U
static struct pm_escc serial;
static u32 root[1024] __attribute__((aligned(4096)));
static u32 low[1024] __attribute__((aligned(4096)));
static u32 test_pt[1024] __attribute__((aligned(4096)));
static u32 io_pt[1024] __attribute__((aligned(4096)));
static u32 hpt[32768] __attribute__((aligned(131072)));
static u32 page[1024] __attribute__((aligned(4096)));
static u32 pvr, faults, expected, isyncs;
/* Observe the privileged boundary, still execute the real product instruction. */
void __real_ppc_isync(void);
void __wrap_ppc_isync(void) { ++isyncs; __real_ppc_isync(); }
extern unsigned char _start[], _end[];
extern void fixture_real(void), fixture_translated(void), fixture_finish(void);
extern void fixture_guard(u32), fixture_guard_load(void), fixture_guard_resume(void);
static void say(const char *s)
{
    unsigned n=0; while (s[n]) ++n;
    pm_escc_write(&serial,s,n); pm_escc_drain(&serial);
}
static void bad(void) { say("CPU-FAIL\r\n"); ppc_stop(); }
int fixture_hpt_spill(struct ppc_frame *f);
int ppc_hpt_spill(struct ppc_frame *f) { return fixture_hpt_spill(f); }
u32 ppc_root_current(void) { return (u32)root; }
void ppc_trap_dispatch(struct ppc_frame *f)
{
    if (f->vector!=PPC_VECTOR_DSI || f->dar!=expected ||
        f->dsisr!=PPC_DSISR_MISS || f->srr0!=(u32)fixture_guard_load) bad();
    ++faults;
    f->srr0=(u32)fixture_guard_resume;
}
void fixture_main(void)
{
    if (*(volatile u32 *)TEST_VA!=0x76543210) bad();
    say("HPT-PASS\r\n");
    expected=GUARD_LOW;
    fixture_guard(expected);
    if (faults!=1) bad();
    say("LOW-BAT-GUARD-PASS\r\n");
    if (pvr>>16==PPC_PVR_7455) {
        expected=GUARD_HIGH;
        fixture_guard(expected);
        if (faults!=2) bad();
        say("HIGH-BAT-GUARD-PASS\r\n");
    }
    say("CPU-PASS\r\n");
    fixture_finish();
}
void fixture_setup_real(void)
{
    if (!ppc_cpu_identify(&pvr)) bad();
    /* Start with live RAM BAT0; 7455 also has a live extended BAT4. */
    ppc_bat_set(0,0x12,0x1ffe);
    if (pvr>>16==PPC_PVR_7455) {
        ppc_write_hid0(ppc_read_hid0()|PPC_HID_HIGHBAT|PPC_HID_XBSEN|PPC_HID_STEN);
        ppc_sync(); ppc_isync();
        u32 upper=GUARD_HIGH|0x1ffe, lower=0x12;
        __asm__ volatile("mtspr 569,%0; mtspr 568,%1; mtspr 561,%0; mtspr 560,%1"
                         :: "r"(lower), "r"(upper) : "memory");
    }
    ppc_cpu_disable_bats(pvr);
    u32 hid=ppc_read_hid0();
    if ((hid&PPC_HID_STEN) || !(hid&PPC_HID_TBEN)) bad();
    root[0]=(u32)low|3;
    root[TEST_VA>>22]=(u32)test_pt|3;
    root[IO_VA>>22]=(u32)io_pt|3;
    for (u32 i=0;i<1024;++i) low[i]=(i<<12)|3;
    low[GUARD_LOW>>12]=0;
    test_pt[0]=(u32)page|3;
    u32 pa=serial.csr & ~4095U;
    io_pt[(IO_VA>>12)&1023]=pa|0x13;
    io_pt[((IO_VA>>12)&1023)+1]=(pa+4096)|0x13;
    serial.data=IO_VA+(serial.data-pa);
    serial.csr=IO_VA+(serial.csr-pa);
    page[0]=0x76543210;
    if (!ppc_hpt_init((u32)hpt,sizeof(hpt))) ppc_stop();
    ppc_cpu_load_segments();
    u32 before=isyncs;
    ppc_cpu_load_sdr1(ppc_hpt_sdr1());
    if (isyncs!=before+1) ppc_stop();
    /* Check all actual SRs, including both key bits, before translation. */
    for (u32 i=0;i<16;++i) {
        u32 sr;
        __asm__ volatile("mfsrin %0,%1" : "=r"(sr) : "r"(i<<28));
        if (sr!=(PPC_SR_KP|ppc_hpt_vsid(i))) ppc_stop();
    }
    u32 sdr;
    __asm__ volatile("mfsdr1 %0" : "=r"(sdr));
    if (sdr!=ppc_hpt_sdr1()) ppc_stop();
    for (u32 va=(u32)_start & ~4095U; va<(u32)_end; va+=4096) {
        struct ppc_frame f={.vector=PPC_VECTOR_DSI,.dar=va,.dsisr=PPC_DSISR_MISS};
        if (!ppc_hpt_spill(&f)) ppc_stop();
    }
    ppc_cache_copy((void *)0,ppc_kernel_vectors.src,ppc_kernel_vectors.size);
    fixture_translated();
}
void powermac_boot(ppc_ci ci)
{
    struct ofw_env e;
    ofw_init(ci);
    if (ofw_setup(&e) || pm_escc_prepare(&e,&serial)) ppc_stop();
    if (!ppc_cpu_identify(&pvr)) { say("UNKNOWN-PVR\r\n"); ppc_stop(); }
    ppc_disable();
    fixture_real();
}
