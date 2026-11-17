/* PowerPC integer USER and parent context storage (PPC_SHELL_EXEC_DESIGN §5). */
#ifndef OS32_PPC_ARCH_EXEC_STORAGE_H
#define OS32_PPC_ARCH_EXEC_STORAGE_H
/* GPR0-31, LR, CR, CTR, XER, SRR0/1, DAR, DSISR and vector. */
#define ARCH_USER_BYTES 176
#define ARCH_USER_ALIGN 4
/* Kernel SP used on USER entry. */
#define PPC_PRIV_KERNEL_SP_OFFSET 0
#define ARCH_PRIV_BYTES 4
#define ARCH_PRIV_ALIGN 4
#define ARCH_ENTRY_BYTES 24
#define ARCH_ENTRY_ALIGN 4
/* 32 FPR bit images followed by the 8-byte mffs image; no vector state. */
#define PPC_FP_FPSCR_OFFSET 256
#define ARCH_FP_BYTES 264
#define ARCH_FP_ALIGN 8
#endif
