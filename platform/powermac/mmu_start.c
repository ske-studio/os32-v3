#include "ppc_pmap.h"
#include "ppc_frame.h"
#include "ppc_memmap.h"
#include "arch_io.h"
#include "arch_cpu.h"
#include "platform_io.h"
extern u32 ppc_probe_canary(u32 kind);
extern void ppc_dec_observe(void);
extern void ppc_fatal_probe(void);
extern u32 ppc_probe_load(u32 va);
extern u32 ppc_probe_store_ro(u32 va,u32 value);
extern u32 ppc_probe_exec(u32 va);
extern const unsigned char ppc_probe_code[];
static void prepare(u32 vector,u32 pa,u32 action)
{
    ppc_probe=(struct ppc_probe_record){.active=1,.vector=vector,
        .fault_pc=PM_TEST_ALIAS,.va=PM_TEST_ALIAS,.pa=pa,
        .attr=PPC_ATTR_RAM_RW,.action=action};
}
static void probes(void)
{
    u32 a=pm_v2p_ram(ppc_probe_pages[0]), b=pm_v2p_ram(ppc_probe_pages[1]);
    *(volatile u32 *)pm_p2v_ram(a)=0x12345678;
    *(volatile u32 *)pm_p2v_ram(b)=0x87654321;
    if (ppc_pmap_enter(PM_TEST_ALIAS,a,PPC_ATTR_RAM_RW)) ppc_stop();
    if (ppc_probe_load(PM_TEST_ALIAS)!=0x12345678) ppc_stop();
    if (ppc_pmap_enter(PM_TEST_ALIAS,b,PPC_ATTR_RAM_RW)) ppc_stop();
    if (ppc_probe_load(PM_TEST_ALIAS)!=0x87654321) ppc_stop();
    ppc_select_context(ppc_current_context());
    if (ppc_probe_load(PM_TEST_ALIAS)!=0x87654321) ppc_stop();
    ppc_pmap_remove(PM_TEST_ALIAS);
    prepare(PPC_VECTOR_DSI,a,PPC_PROBE_MAP);
    if (ppc_probe_canary(0) || ppc_probe.active) ppc_stop();
    if (ppc_pmap_enter(PM_TEST_ALIAS,a,PPC_ATTR_RAM_RO)) ppc_stop();
    prepare(PPC_VECTOR_DSI,a,PPC_PROBE_ONFAULT);
    if (ppc_probe_canary(1) || ppc_probe.active) ppc_stop();
    if (*(volatile u32 *)pm_p2v_ram(a)!=0x12345678) ppc_stop();
    ppc_pmap_remove(PM_TEST_ALIAS);
    ppc_cache_copy(pm_p2v_ram(b),ppc_probe_code,PPC_CACHE_LINE);
    prepare(PPC_VECTOR_ISI,b,PPC_PROBE_MAP);
    if (ppc_probe_canary(2) || ppc_probe.active) ppc_stop();
    pm_mmu_dump("PROBES-PASS",0);
}
void pm_mmu_start(const struct pm_boot_info *info)
{
    u32 page=info->escc_pa & PPC_PAGE_MASK;
    if (ppc_pmap_enter(PM_IO_ALIAS,page,PPC_ATTR_IO_RW)) ppc_stop();
    if ((info->escc_pa & ~PPC_PAGE_MASK)+PM_ESCC_WINDOW>PPC_PAGE_SIZE)
        if (ppc_pmap_enter(PM_IO_ALIAS+PPC_PAGE_SIZE,page+PPC_PAGE_SIZE,PPC_ATTR_IO_RW)) ppc_stop();
    ppc_select_context(ppc_current_context());
    arch_mmu_enable();
    pm_mmu_console_bind(info->escc_pa);
    pm_mmu_dump("OWN-VECTORS",0);
    pm_mmu_dump("OWN-MMU",0);
    ppc_write_dec(PPC_DEC_FAR);
    if (_irq_enabled()) ppc_stop();
    _enable();
    if (!_irq_enabled()) ppc_stop();
    u32 saved=irq_save();
    if (_irq_enabled()) ppc_stop();
    irq_restore(saved);
    if (!_irq_enabled()) ppc_stop();
    _disable();
    saved=irq_save();
    irq_restore(saved);
    if (_irq_enabled()) ppc_stop();
    /* Exercise byte-I/O wrappers using the existing indexed RR1 protocol. */
    u32 csr=pm_p2v_io(info->escc_pa);
    outp(csr,PM_RR1_INDEX);
    if (!(inp(csr)&PM_RR1_ALL_SENT)) ppc_stop();
    pm_mmu_dump("EE-PASS",0);
#if PPC_MMU_STAGE >= 2
    probes();
#endif
#if PPC_MMU_STAGE >= 3
    ppc_dec_init(info->tb_hz);
    _enable();
    if (ppc_probe_canary(3)) ppc_stop();
    _disable();
    if (ppc_dec_count()<PM_DEC_DUMP_N+2) ppc_stop();
    pm_mmu_dump("CANARY-PASS",0);
    pm_mmu_dump("DEC-PASS",0);
#if PPC_MMU_MODE == 1
    ppc_fatal_probe();
#else
    _enable();
    ppc_dec_observe();
#endif
#endif
    ppc_stop();
}
