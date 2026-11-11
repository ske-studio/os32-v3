/* PowerPC kernel jump context (PPC_SHELL_EXEC_DESIGN §5). */
#ifndef OS32_PPC_ARCH_JMP_STORAGE_H
#define OS32_PPC_ARCH_JMP_STORAGE_H
/* r13-r31 occupy 19 consecutive words beginning at PPC_JMP_R13_OFFSET. */
#define PPC_JMP_R1_OFFSET 0
#define PPC_JMP_R2_OFFSET 4
#define PPC_JMP_R13_OFFSET 8
#define PPC_JMP_LR_OFFSET 84
#define PPC_JMP_CR_OFFSET 88
#define PPC_JMP_IRQ_DEPTH_OFFSET 92
#define PPC_JMP_EXCEPTION_DEPTH_OFFSET 96
#define ARCH_JMP_BYTES 100
#define ARCH_JMP_ALIGN 4
#endif
