/* Parent root and USER exception entry stack, on the surviving kernel stack. */
#include "exec_arch.h"
#include "arch_cpu.h"

/* Set by the USER entry path; USER exceptions use this stack. */
u32 ppc_exec_kernel_sp;

STATIC_ASSERT(PPC_PRIV_ROOT_OFFSET == 0, priv_root_offset);
STATIC_ASSERT(PPC_PRIV_KERNEL_SP_OFFSET == sizeof(u32), priv_sp_offset);
STATIC_ASSERT(ARCH_PRIV_BYTES == 2 * sizeof(u32), priv_context_size);
STATIC_ASSERT(__alignof__(struct arch_priv_context) == 4, priv_context_align);

void arch_priv_save(volatile struct arch_priv_context *out)
{
    u32 root = arch_mmu_current_root();
    u32 sp = ppc_exec_kernel_sp;

    __builtin_memcpy((void *)(out->opaque + PPC_PRIV_ROOT_OFFSET), &root, sizeof(root));
    __builtin_memcpy((void *)(out->opaque + PPC_PRIV_KERNEL_SP_OFFSET), &sp, sizeof(sp));
}

void arch_priv_restore(const volatile struct arch_priv_context *saved)
{
    u32 root, sp;

    __builtin_memcpy(&root, (const void *)(saved->opaque + PPC_PRIV_ROOT_OFFSET), sizeof(root));
    __builtin_memcpy(&sp, (const void *)(saved->opaque + PPC_PRIV_KERNEL_SP_OFFSET), sizeof(sp));
    arch_mmu_load_root(root);
    ppc_exec_kernel_sp = sp;
}
