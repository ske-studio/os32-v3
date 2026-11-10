/* Owned saved contexts; syscall views borrow only the live entry stack. */
#ifndef OS32_EXEC_ARCH_H
#define OS32_EXEC_ARCH_H
#include "types.h"
#include "arch_exec_storage.h"
struct arch_syscall { void *native; };
void exec_syscall_dispatch(struct arch_syscall *);
struct arch_user_context {
    unsigned char opaque[ARCH_USER_BYTES];
} __attribute__((aligned(ARCH_USER_ALIGN)));
struct arch_priv_context {
    unsigned char opaque[ARCH_PRIV_BYTES];
} __attribute__((aligned(ARCH_PRIV_ALIGN)));
/* AppSlot owns this storage across nested launches and longjmp. */
struct arch_entry_context {
    unsigned char opaque[ARCH_ENTRY_BYTES];
} __attribute__((aligned(ARCH_ENTRY_ALIGN)));
/* Floating-point (and vector) state of one USER context; opaque outside arch.
 * The kernel itself is integer-only: this is saved/restored only around USER. */
struct arch_fp_context {
    unsigned char opaque[ARCH_FP_BYTES];
} __attribute__((aligned(ARCH_FP_ALIGN)));
struct exec_entry_args { u32 argc, argv, api, aux; };
/* Returns nonzero on arithmetic or store failure; complete before commit.
 * store returns zero on success. owner remains borrowed for this call only. */
int arch_entry_prepare(struct arch_entry_context *, u32 top, u32 entry,
                       int user, const struct exec_entry_args *,
                       int (*store)(void *, u32, const void *, u32), void *owner);
/* USER never returns; CPL0 may return normally or leave via exec_longjmp. */
void arch_entry_enter(struct arch_entry_context *, u32 address_space);
/* Save once on the surviving launch/resume stack, before setjmp. */
void arch_priv_save(volatile struct arch_priv_context *);
/* Caller holds IRQs disabled until the parent context is fully restored. */
void arch_priv_restore(const volatile struct arch_priv_context *);
void arch_user_save(struct arch_user_context *, const struct arch_syscall *);
u32 arch_syscall_slot(const struct arch_syscall *);
void arch_syscall_result(struct arch_syscall *, u32);
void arch_user_result(struct arch_user_context *, u32);
/* Restart once per resume; preserves slot and arguments. */
void arch_user_restart(struct arch_user_context *);
/* Atomic privileged transition; does not return. */
void arch_user_resume(const struct arch_user_context *, u32 address_space);
/* No-ops when the CPU has no such state. init = the state a new program starts with. */
void arch_fp_save(struct arch_fp_context *);
void arch_fp_restore(const struct arch_fp_context *);
void arch_fp_init(void);
#endif
