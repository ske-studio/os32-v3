#ifndef OS32_PPC_ARCH_CPU_H
#define OS32_PPC_ARCH_CPU_H
#include "ppc_oea.h"
u32 ppc_root_current(void);
void ppc_root_load(u32 root);
void ppc_boot_load_root(u32 root);
void ppc_root_flush(void);
void ppc_user_not_available(u32 root,u32 sp,u32 entry);
void ppc_stack_not_available(u32 *saved,u32 sp,u32 entry);
static inline u32 arch_mmu_current_root(void) { return ppc_root_current(); }
static inline void arch_mmu_load_root(u32 root) { ppc_boot_load_root(root); }
static inline void arch_mmu_flush_tlb(void) { ppc_root_flush(); }
static inline void arch_mmu_enable(void) { ppc_enable_translation(); }
#define arch_enter_user(root,sp,entry) ppc_user_not_available(root,sp,entry)
#define arch_call_on_stack(saved,sp,entry) ppc_stack_not_available(&(saved),sp,entry)
static inline void arch_cpu_serialize(void)
{
    __asm__ volatile("sync" : : : "memory");
}
static inline int arch_cpu_has_mmx(void) { return 0; }

#endif
