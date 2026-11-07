#ifndef OS32_PPC_ARCH_CPU_H
#define OS32_PPC_ARCH_CPU_H
#include "ppc_oea.h"
u32 ppc_legacy_root_not_available(u32 root);
void ppc_user_not_available(u32 root,u32 sp,u32 entry);
void ppc_stack_not_available(u32 *saved,u32 sp,u32 entry);
static inline u32 arch_mmu_current_root(void) { return ppc_legacy_root_not_available(0); }
static inline void arch_mmu_load_root(u32 root) { (void)ppc_legacy_root_not_available(root); }
static inline void arch_mmu_flush_tlb(void) { ppc_tlb_flush_all(); }
static inline void arch_mmu_enable(void) { ppc_enable_translation(); }
#define arch_enter_user(root,sp,entry) ppc_user_not_available(root,sp,entry)
#define arch_call_on_stack(saved,sp,entry) ppc_stack_not_available(&(saved),sp,entry)
#endif
