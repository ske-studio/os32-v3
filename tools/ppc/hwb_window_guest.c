/* PowerMac HW_OP §5 6a and common design §3.6. Remove a mapped RAM
 * page immediately before the transfer and record each fault separately. */
static volatile u32 sample[2048] __attribute__((aligned(4096)));
volatile u32 hwb_inject, hwb_case, hwb_observed[3][13];

static void hwb_window_report(void)
{
    u32 pa = (u32)&sample[1023];
    for (u32 test = 0; test < 3; test++) {
        int block = test == 0;
        int store = test != 1;
        u32 value = 17;
        volatile u32 *obs = hwb_observed[test];
        for (u32 i = 1023; i <= 1025; i++) sample[i] = 91;
        __asm__ volatile("dcbf 0,%0; dcbf 0,%1; sync" ::
                         "r"(pa), "r"(pa+4) : "memory");
        u32 saved = ppc_irq_save();
        ppc_irq_restore(saved);
        hwb_case = test;
        hwb_inject = block ? 2 : 1;
        obs[0] = pm_hwb_window_transfer(pa,4,0,store,block ? 3 : 1,1,&value);
        u32 after = ppc_irq_save();
        ppc_irq_restore(after);
        obs[1] = (after ^ saved) & PPC_MSR_EE;
        obs[2] = paging_pte_raw(PPC_HWB_WINDOW_VA);
        obs[3] = paging_pte_raw(PPC_HWB_WINDOW_VA + PAGE_SIZE);
        obs[4] = sample[1023];
        obs[5] = sample[1024];
        obs[6] = sample[1025];
        /* Keep the failed load output before reusing the window. */
        obs[12] = value;
        obs[7] = pm_hwb_window_transfer(pa,4,0,0,1,0,&value);
        obs[8] = value;
    }
}
