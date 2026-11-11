/* PowerPC integer USER and parent context storage (PPC_SHELL_EXEC_DESIGN §5). */
#ifndef OS32_PPC_ARCH_EXEC_STORAGE_H
#define OS32_PPC_ARCH_EXEC_STORAGE_H
/* GPR0-31, LR, CR, CTR, XER, SRR0/1, DAR, DSISR and vector. */
#define ARCH_USER_BYTES 176
#define ARCH_USER_ALIGN 4
/* Parent address-space root and the kernel SP used on USER entry. */
#define PPC_PRIV_ROOT_OFFSET 0
#define PPC_PRIV_KERNEL_SP_OFFSET 4
#define ARCH_PRIV_BYTES 8
#define ARCH_PRIV_ALIGN 4
#define ARCH_ENTRY_BYTES 24
#define ARCH_ENTRY_ALIGN 4
/* USER has no floating-point or vector state. */
#define ARCH_FP_BYTES 1
#define ARCH_FP_ALIGN 1
#endif
