/* PowerMac HW_OP §5 6a. Test-only RAM backing and injection. The second
 * translation is removed after mapping, immediately before the real stores. */
static volatile u32 sample[2048] __attribute__((aligned(4096)));
volatile u32 hwb_inject, hwb_observed[12];
extern char ppc_hwb_write32_insn[], ppc_hwb_write32_resume[];

static void hwb_window_report(void)
{
    u32 pa = (u32)&sample[1023];
    u32 value = 17;
    for (u32 i = 1023; i <= 1025; i++) sample[i] = 91;
    __asm__ volatile("dcbf 0,%0; dcbf 0,%1; sync" ::
                     "r"(pa), "r"(pa+4) : "memory");
    u32 saved = ppc_irq_save();
    ppc_irq_restore(saved);
    hwb_inject = 1;
    hwb_observed[0] = pm_hwb_window_transfer(pa,4,0,1,3,1,&value);
    u32 after = ppc_irq_save();
    ppc_irq_restore(after);
    hwb_observed[1] = (after ^ saved) & PPC_MSR_EE;
    hwb_observed[2] = paging_pte_raw(PPC_HWB_WINDOW_VA);
    hwb_observed[3] = paging_pte_raw(PPC_HWB_WINDOW_VA + PAGE_SIZE);
    hwb_observed[4] = sample[1023];
    hwb_observed[5] = sample[1024];
    hwb_observed[6] = sample[1025];
    hwb_observed[7] = pm_hwb_window_transfer(pa,4,0,0,1,0,&value);
    hwb_observed[8] = value;
}
