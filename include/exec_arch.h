/* Owned saved contexts; syscall views borrow only the live entry stack. */
#ifndef OS32_EXEC_ARCH_H
#define OS32_EXEC_ARCH_H
#include "types.h"
#include "arch_exec_storage.h"
struct arch_syscall { void *native; };
struct arch_user_context {
    unsigned char opaque[ARCH_USER_BYTES];
} __attribute__((aligned(ARCH_USER_ALIGN)));
struct arch_priv_context {
    unsigned char opaque[ARCH_PRIV_BYTES];
} __attribute__((aligned(ARCH_PRIV_ALIGN)));
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
#endif
