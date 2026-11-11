/* Parent root and USER exception entry stack, on the surviving kernel stack. */
#include "exec_arch.h"
#include "arch_cpu.h"
#include "kstring.h"

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

/* The saved image has the same layout as the USER exception frame. */
#include "ppc_frame.h"
STATIC_ASSERT(ARCH_USER_BYTES == PPC_FRAME_SIZE, user_frame_size);
STATIC_ASSERT(__builtin_offsetof(struct ppc_frame, srr0) == PPC_FRAME_SRR0, user_srr0_offset);

void arch_user_save(struct arch_user_context *out, const struct arch_syscall *call)
{
    kmemcpy(out->opaque, call->native, ARCH_USER_BYTES);
}

void arch_user_result(struct arch_user_context *saved, u32 result)
{
    __builtin_memcpy(saved->opaque + 3 * sizeof(u32), &result, sizeof(result));
}

void arch_user_restart(struct arch_user_context *saved)
{
    u32 pc;
    __builtin_memcpy(&pc, saved->opaque + PPC_FRAME_SRR0, sizeof(pc));
    pc -= 4;
    __builtin_memcpy(saved->opaque + PPC_FRAME_SRR0, &pc, sizeof(pc));
}

void arch_fp_save(struct arch_fp_context *saved) { (void)saved; }
void arch_fp_restore(const struct arch_fp_context *saved) { (void)saved; }
void arch_fp_init(void) { }
