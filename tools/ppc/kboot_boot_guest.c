/* D3 link-only H2/fatal providers; all boot/MMU/mm implementations are product. */
#include "kernel_boot.h"
#include "kboot_info.h"
#include "ppc_trap.h"
#include "ppc_oea.h"
#include "ofw.h"
#include "paging.h"
#include "kstring.h"
void fixture_done(void) __attribute__((noreturn));
void fixture_fail(void) __attribute__((noreturn));
__asm__(".globl fixture_done\nfixture_done: b fixture_done\n"
        ".globl fixture_fail\nfixture_fail: b fixture_fail\n");
/* Make the incoming BAT state adversarial even when firmware used only HPT.
 * Runs at the real-mode boundary; C4 must retire this mapping. */
void __real_ppc_kernel_real_start(const struct pm_kboot_info *) __attribute__((noreturn));
void __wrap_ppc_kernel_real_start(const struct pm_kboot_info *info)
{
    ppc_bat_set(0, PM_BAT_RAM_LOWER, PM_BAT_RAM_UPPER);
    __real_ppc_kernel_real_start(info);
}
static void say(const char *text)
{
    struct pm_escc s = {PPC_BOOT_ESCC_VA + 0x20, PPC_BOOT_ESCC_VA + 0x30, 0};
    if (pm_escc_write(&s, text, kstrlen(text)) || pm_escc_drain(&s)) fixture_fail();
}
void ppc_trap_fatal_sink(const struct ppc_frame *f, u32 reason)
{
    if (f->vector != 0x300 || reason != PPC_TRAP_DSI_MISS) fixture_fail();
    if (f->dar != (BOOT_NEGATIVE == 1 ? 0x80000U : 0x1000000U)) fixture_fail();
    say("FATAL:DSI\r\n"); fixture_done();
}
void ppc_kernel_main(void)
{
    u32 msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    if ((msr & (PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_EE)) !=
        (PPC_MSR_IR | PPC_MSR_DR)) fixture_fail();
    say("PAGING:MAIN\r\n");
#if BOOT_NEGATIVE == 1
    say("PROBE:LOW\r\n");
    (void)*(volatile u32 *)0x80000;
#elif BOOT_NEGATIVE == 2
    say("PROBE:HPT\r\n");
    (void)*(volatile u32 *)0x1000000;
#else
    say("BOOT:PASS\r\n"); fixture_done();
#endif
    say("FAIL:SURVIVED\r\n"); fixture_fail();
}
