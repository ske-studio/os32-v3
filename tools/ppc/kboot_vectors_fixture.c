/* Minimal D1 mac99 image: firmware is used only to discover ESCC.
 * The spill/dispatch wrappers observe the D1 boundary; the real C1/C2
 * providers still perform all page walking and HPT insertion. */
#include "ofw.h"
#include "ppc_frame.h"
#include "ppc_hpt.h"
#include "ppc_pd.h"
#include "ppc_trap.h"
#define TEST_VA 0x50000000U
#define NEST_VA 0x60000000U
#define IO_VA 0xfe300000U
static struct pm_escc serial;
static u32 root[1024] __attribute__((aligned(4096)));
static u32 low[1024] __attribute__((aligned(4096)));
static u32 test_pt[1024] __attribute__((aligned(4096)));
static u32 nest_pt[1024] __attribute__((aligned(4096)));
static u32 io_pt[1024] __attribute__((aligned(4096)));
static u32 hpt[32768] __attribute__((aligned(131072)));
static u32 page[1024] __attribute__((aligned(4096)));
extern unsigned char _start[], _end[];
extern void fixture_real(void), fixture_translated(void), fixture_finish(void);
extern void fixture_canary(u32 kind), fixture_resume(void), fixture_load(void);
extern u32 fixture_regs[37];
void fixture_guard_check(void);
volatile u32 d1_reached;
static volatile u32 kind, dispatches, spills, nested, in_dispatch;
static u32 msr(void) { u32 v; __asm__ volatile("mfmsr %0":"=r"(v)); return v; }
static void say(const char *s)
{
    unsigned n=0; while (s[n]) ++n;
    pm_escc_write(&serial,s,n); pm_escc_drain(&serial);
}
static void bad(void) { say("FAIL\r\n"); ppc_stop(); }
u32 ppc_root_current(void) { return (u32)root; }
int fixture_hpt_spill(struct ppc_frame *f);
int ppc_hpt_spill(struct ppc_frame *f)
{
    if (msr() & (PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_EE)) ppc_stop();
    u32 fp=(u32)f;
    if (fp<(u32)ppc_kernel_spill_stack ||
        fp+PPC_FRAME_SIZE>(u32)ppc_kernel_spill_stack+PPC_KERNEL_STACK_SIZE) ppc_stop();
    if (f->dar==TEST_VA || f->dar==TEST_VA+4096) {
        ++spills;
        if (f->srr0!=(u32)fixture_load || f->gpr[31]!=287 ||
            f->gpr[1]!=0x00301000 || f->cr!=0x12345678 ||
            f->lr!=0x1234 || f->ctr!=0x2345 || f->xer!=0xa0000000)
            ppc_stop();
    }
    if (in_dispatch && f->dar==NEST_VA) ++nested;
    return fixture_hpt_spill(f);
}
void product_trap_dispatch(struct ppc_frame *f);
static u32 table_read(u32 pa, void *ctx)
{
    (void)ctx;
    if (in_dispatch && !nested) {
        if (*(volatile u32 *)NEST_VA!=0x76543210) bad();
    }
    return *(volatile u32 *)pa;
}
void ppc_trap_fatal_sink(const struct ppc_frame *f, u32 reason)
{ (void)f; (void)reason; bad(); }
void ppc_trap_dispatch(struct ppc_frame *f)
{
    if ((msr() & (PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_EE)) !=
        (PPC_MSR_IR|PPC_MSR_DR)) bad();
    if (kind!=1 || f->vector!=PPC_VECTOR_DSI || f->dar!=TEST_VA+4096 ||
        f->srr0!=(u32)fixture_load || f->gpr[1]!=0x00301000 ||
        f->cr!=0x12345678 || f->gpr[31]!=287) bad();
    if ((u32)f<(u32)ppc_kernel_trap_stack ||
        (u32)f+PPC_FRAME_SIZE>(u32)ppc_kernel_trap_stack+PPC_KERNEL_STACK_SIZE) bad();
    ++dispatches;
    in_dispatch=1;
    product_trap_dispatch(f);
    in_dispatch=0;
    /* A nested spill must not change the outer frame. */
    if (f->dar!=TEST_VA+4096 || f->gpr[31]!=287 || f->cr!=0x12345678) bad();
    if (f->srr0!=(u32)fixture_resume) bad();
}
void fixture_main(void)
{
    say("D1-BEGIN\r\n");
    ppc_trap_set_reader(table_read,0);
    for (kind=0; kind<2; ++kind) {
        if (kind && !ppc_trap_onfault((u32)fixture_load,TEST_VA+4096,
                                     PPC_TRAP_DSI_MISS,(u32)fixture_resume)) bad();
        fixture_canary(kind);
        for (u32 r=0;r<32;++r) {
            u32 expected=r==1 ? 0x00301000 : r==3 ? (kind ? TEST_VA+4096 : 0x76543210) : 256+r;
            if (fixture_regs[r]!=expected) bad();
        }
        if (fixture_regs[32]!=0x1234 || fixture_regs[33]!=0x12345678 ||
            fixture_regs[34]!=0x2345 || fixture_regs[35]!=0xa0000000 ||
            (fixture_regs[36] & (PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_EE))!=0x30) bad();
        say(kind ? "DISPATCH-CANARY-PASS\r\n" : "RETRY-CANARY-PASS\r\n");
    }
    if (dispatches!=1 || spills!=2 || nested!=1) bad();
    fixture_guard_check();
    say("GUARD-PASS\r\n");
    say("NESTED-SPILL-PASS\r\nVECTORS-PASS\r\n");
    fixture_finish();
}
void fixture_setup_real(void)
{
    /* All kernel resident addresses are below 4MiB in this fixture. */
    root[0]=(u32)low|3;
    root[TEST_VA>>22]=(u32)test_pt|3;
    root[NEST_VA>>22]=(u32)nest_pt|3;
    root[IO_VA>>22]=(u32)io_pt|3;
    for (u32 i=0;i<1024;++i) low[i]=(i<<12)|3;
    low[0x300]=low[0x301]=0;
    volatile u32 *guard=(volatile u32 *)0x300000;
    for (u32 i=0;i<8192/4;++i) guard[i]=0xa55aa55a;
    test_pt[0]=(u32)page|3; /* second page deliberately NP */
    nest_pt[0]=(u32)page|3;
    u32 pa=serial.csr & ~4095U;
    io_pt[(IO_VA>>12)&1023]=pa|0x13;
    io_pt[((IO_VA>>12)&1023)+1]=(pa+4096)|0x13;
    serial.data=IO_VA+(serial.data-pa);
    serial.csr=IO_VA+(serial.csr-pa);
    page[0]=0x76543210;
    if (!ppc_hpt_init((u32)hpt,sizeof(hpt))) ppc_stop();
    u32 sr[16]; for (u32 i=0;i<16;++i) sr[i]=PPC_SR_KP|ppc_hpt_vsid(i);
    ppc_sr_sdr1_load(sr,ppc_hpt_sdr1());
    ppc_tlb_flush_all();
    /* Pin the small image in HPT except the real-only spill stack and trap
     * stack: early DR causes a recursive miss, not an unnoticed BAT hit. */
    for (u32 va=(u32)_start & ~4095U; va<(u32)_end; va+=4096) {
        if (va>=(u32)ppc_kernel_trap_stack &&
            va<(u32)ppc_kernel_spill_stack+PPC_KERNEL_STACK_SIZE) continue;
        struct ppc_frame f={.vector=PPC_VECTOR_DSI,.dar=va,.dsisr=PPC_DSISR_MISS};
        if (!fixture_hpt_spill(&f)) ppc_stop();
    }
    ppc_cache_copy((void *)0,ppc_kernel_vectors.src,ppc_kernel_vectors.size);
    d1_reached=1;
    fixture_translated();
}
/* Called in real mode with the test's ordinary, reserved boot stack. */
void fixture_check_real_guard(void)
{
    volatile u32 *guard=(volatile u32 *)0x300000;
    for (u32 i=0;i<8192/4;++i) if (guard[i]!=0xa55aa55a) ppc_stop();
}
void powermac_boot(ppc_ci ci)
{
    struct ofw_env e;
    ofw_init(ci);
    if (ofw_setup(&e) || pm_escc_prepare(&e,&serial)) ppc_stop();
    ppc_disable();
    fixture_real();
}
