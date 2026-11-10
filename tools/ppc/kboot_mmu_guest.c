/* C3 acceptance image only: RAM BAT retains code/stack identity; the probe
 * alias uses the product plan-D walker/HPT and hardware page translation.
 * Existing standalone vectors are a harness, not the concurrent D1 provider. */
#include "ppc_pmap.h"
#include "ppc_hpt.h"
#include "ppc_frame.h"
#include "arch_cpu.h"
#include "ofw.h"
static u32 roots[2][1024] __attribute__((aligned(4096)));
static u32 tables[2][1024] __attribute__((aligned(4096)));
static u32 pages[2][1024] __attribute__((aligned(4096)));
static struct pm_escc console;
static u32 misses, guard_expected, guard_seen, translated_msr;
extern u32 c3_read_alias(u32 va);
__asm__(".text\n.globl c3_read_alias\nc3_read_alias:\n lwz 3,0(3)\n blr\n");
static u32 msr(void) { u32 v; __asm__ volatile("mfmsr %0":"=r"(v)); return v; }
/* C3 in isolation: D3's first-load hook is not part of this image. */
void ppc_boot_load_root(u32 root) { ppc_root_load(root); }
static void say(const char *s, u32 n)
{
    if (pm_escc_write(&console,s,n) || pm_escc_drain(&console)) ppc_stop();
}
static void check(int ok)
{
    if (!ok) { say("C3-FAIL\r\n",9); ppc_stop(); }
}
void ppc_exception(struct ppc_frame *f)
{
    /* Old standalone vector entry enables translation via the identity RAM
     * BAT. Replace only that privileged transition before calling C2. */
    u32 saved=msr(), real=saved & ~(PPC_MSR_EE|PPC_MSR_IR|PPC_MSR_DR);
    __asm__ volatile("sync; mtmsr %0; isync"::"r"(real):"memory");
    int ok=ppc_hpt_spill(f);
    __asm__ volatile("sync; mtmsr %0; isync"::"r"(saved):"memory");
    if (!ok && guard_expected && f->vector==PPC_VECTOR_DSI &&
        f->dar==PM_TEST_ALIAS && f->dsisr==PPC_DSISR_MISS) {
        ++guard_seen; f->gpr[3]=0xfeedface; f->srr0+=4; return;
    }
    if (!ok) { say("C3-FAULT\r\n",10); ppc_stop(); }
    ++misses;
}
static u32 read_alias(void) { return c3_read_alias(PM_TEST_ALIAS); }
static void expect(u32 value)
{
    /* Observe before touching the virtual alias or diagnostic MMIO. On
     * failure only, restore the harness MSR so the verdict can be printed. */
    if (msr()!=translated_msr) {
        __asm__ volatile("sync; mtmsr %0; isync"::"r"(translated_msr):"memory");
        check(0);
    }
    check(read_alias()==value);
    check(msr()==translated_msr);
}
void pm_mmu_start(const struct pm_boot_info *info)
{
    /* Test fixture owns setup (C4 is deliberately outside C3). */
    check(ppc_hpt_init(info->hpt_pa,128*1024));
    u32 sr[16];
    for (u32 i=0;i<16;++i) sr[i]=PPC_SR_KP|ppc_hpt_vsid(i);
    ppc_sr_sdr1_load(sr,ppc_hpt_sdr1());
    for (u32 i=0;i<2;++i) {
        roots[i][PM_TEST_ALIAS>>22]=(u32)tables[i]|7;
        tables[i][0]=(u32)pages[i]|7;
    }
    pages[0][0]=0x12345678; pages[1][0]=0x87654321;
    console.csr=info->escc_pa; console.data=console.csr+PM_ESCC_STRIDE;
    /* Use the pre-existing standalone MMIO PTEs in the new HPT through
     * explicit spill-like insertion below; no product aperture setup. */
    static u32 iopt[1024] __attribute__((aligned(4096)));
    u32 ioindex=PM_IO_ALIAS>>22;
    iopt[0]=(info->escc_pa & PPC_PAGE_MASK)|0x17;
    iopt[1]=((info->escc_pa & PPC_PAGE_MASK)+PPC_PAGE_SIZE)|0x17;
    roots[0][ioindex]=roots[1][ioindex]=(u32)iopt|7;
    console.csr=PM_IO_ALIAS+(info->escc_pa & ~PPC_PAGE_MASK);
    console.data=console.csr+PM_ESCC_STRIDE;
    arch_mmu_load_root((u32)roots[0]);
    check(arch_mmu_current_root()==(u32)roots[0]);
    check(!(msr()&(PPC_MSR_IR|PPC_MSR_DR)));
    arch_mmu_enable();
    translated_msr=msr();
    expect(0x12345678);
    u32 before=misses;
    arch_mmu_load_root((u32)roots[1]);
    check(arch_mmu_current_root()==(u32)roots[1]);
    expect(0x87654321); check(misses>before);
    tables[1][0]=(u32)pages[0]|7;
    arch_mmu_load_root((u32)roots[1]);
    expect(0x12345678);
    tables[1][0]=(u32)pages[1]|7;
    arch_mmu_flush_tlb();
    expect(0x87654321); check(arch_mmu_current_root()==(u32)roots[1]);
    /* Unmap and require ordinary fault rather than a stale cached mapping. */
    tables[1][0]=0;
    guard_expected=1;
    arch_mmu_flush_tlb();
    check(read_alias()==0xfeedface && guard_seen==1);
    guard_expected=0;
    tables[1][0]=(u32)pages[0]|7;
    arch_mmu_flush_tlb();
    expect(0x12345678);
    say("C3-PASS\r\n",9);
    ppc_stop();
}
