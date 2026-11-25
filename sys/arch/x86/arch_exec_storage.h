/* x86 saved USER frame and privileged parent snapshot, not public KAPI ABI. */
#ifndef OS32_X86_ARCH_EXEC_STORAGE_H
#define OS32_X86_ARCH_EXEC_STORAGE_H
#define ARCH_USER_BYTES 52
#define ARCH_USER_ALIGN 4
#define ARCH_PRIV_BYTES 4
#define ARCH_PRIV_ALIGN 4
#define ARCH_ENTRY_BYTES 16
#define ARCH_ENTRY_ALIGN 4
/* x87 FNSAVE 域。AppSlot の中の位置を動かさないため整列は 1。 */
#define ARCH_FP_BYTES 108
#define ARCH_FP_ALIGN 1
#endif
