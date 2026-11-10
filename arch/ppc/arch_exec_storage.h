/* PowerPC saved-context storage sizes: compile-only, provisional.
 * ② (kernel boot) does not run USER code: arch_entry_prepare reports an error and
 * enter/resume/syscall are unsupported (PPC_KBOOT_DESIGN §5). These sizes only let
 * the common exec/ structures be laid out; the USER ABI (slot/argument registers,
 * what is saved, FP/AltiVec ownership) is decided in the ③ design, not here. */
#ifndef OS32_PPC_ARCH_EXEC_STORAGE_H
#define OS32_PPC_ARCH_EXEC_STORAGE_H
/* The existing exception frame: GPR0-31, LR, CR, CTR, XER, SRR0/1, DAR, DSISR,
 * vector (arch/ppc/ppc_frame.h PPC_FRAME_SIZE). */
#define ARCH_USER_BYTES 176
#define ARCH_USER_ALIGN 4
#define ARCH_PRIV_BYTES 4
#define ARCH_PRIV_ALIGN 4
#define ARCH_ENTRY_BYTES 16
#define ARCH_ENTRY_ALIGN 4
/* FPR0-31 + FPSCR. The kernel is integer-only (-msoft-float -mno-altivec);
 * whether USER may use the FPU or AltiVec at all is a ③ decision. */
#define ARCH_FP_BYTES 264
#define ARCH_FP_ALIGN 8
#endif
