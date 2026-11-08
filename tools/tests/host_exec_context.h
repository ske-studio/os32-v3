/* ILP32 executes the product provider with host TSS/ASM observation hooks.
 * LP64 caller_access uses explicitly named host substitutes: its native u32
 * words are 8 bytes and cannot satisfy the product x86 storage contract. */
#ifndef OS32_HOST_EXEC_CONTEXT_H
#define OS32_HOST_EXEC_CONTEXT_H
#include "exec_arch.h"
#include "tss.h"
#define APP_FRAME_WORDS 13
#define APP_FRAME_EAX 7
#define APP_FRAME_EIP 8
#define APP_FRAME_ESP 11
#define HOST_FRAME(saved) ((unsigned int *)(saved).opaque)
#if __SIZEOF_LONG__ == 4
#ifdef HOST_EXEC_CONTEXT_SOURCE
#include HOST_EXEC_CONTEXT_SOURCE
#else
#include "../../arch/x86/kernel/exec_context.c"
#endif
#else
/* Test substitutes for the product; never x86 provider coverage. */
#define HOST_CONTEXT_INLINE extern inline __attribute__((gnu_inline, always_inline))
extern struct tss_entry kernel_tss;
extern void ring3_resume(const u32 *, u32, void *);
HOST_CONTEXT_INLINE void host_arch_priv_save(volatile struct arch_priv_context *out)
{
    unsigned int sp = tss_get_esp0();
    __builtin_memcpy((void *)out->opaque, &sp, sizeof(sp));
}
HOST_CONTEXT_INLINE void host_arch_priv_restore(const volatile struct arch_priv_context *saved)
{
    unsigned int sp;
    __builtin_memcpy(&sp, (const void *)saved->opaque, sizeof(sp));
    tss_set_esp0(sp);
}
HOST_CONTEXT_INLINE void host_arch_user_save(struct arch_user_context *out, const struct arch_syscall *call)
{
    for (unsigned k = 0; k < APP_FRAME_WORDS; k++)
        HOST_FRAME(*out)[k] = ((const u32 *)call->native)[k];
}
HOST_CONTEXT_INLINE u32 host_arch_syscall_slot(const struct arch_syscall *call)
{
    return ((const u32 *)call->native)[APP_FRAME_EAX];
}
HOST_CONTEXT_INLINE void host_arch_syscall_result(struct arch_syscall *call, u32 result)
{
    ((u32 *)call->native)[APP_FRAME_EAX] = result;
}
HOST_CONTEXT_INLINE void host_arch_user_result(struct arch_user_context *saved, u32 result)
{
    HOST_FRAME(*saved)[APP_FRAME_EAX] = result;
}
HOST_CONTEXT_INLINE void host_arch_user_restart(struct arch_user_context *saved)
{
    HOST_FRAME(*saved)[APP_FRAME_EIP] -= 2;
}
HOST_CONTEXT_INLINE void host_arch_user_resume(const struct arch_user_context *saved, u32 root)
{
    ring3_resume((const u32 *)saved->opaque, root, &kernel_tss);
}
#undef HOST_CONTEXT_INLINE
#define arch_priv_save host_arch_priv_save
#define arch_priv_restore host_arch_priv_restore
#define arch_user_save host_arch_user_save
#define arch_syscall_slot host_arch_syscall_slot
#define arch_syscall_result host_arch_syscall_result
#define arch_user_result host_arch_user_result
#define arch_user_restart host_arch_user_restart
#define arch_user_resume host_arch_user_resume
#endif
#endif
