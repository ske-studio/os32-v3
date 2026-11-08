/* Host observation provider: native fixtures may use LP64 u32 arrays, while
 * the owned saved x86 context always stays 52 bytes. No privileged execution. */
#ifndef OS32_HOST_EXEC_CONTEXT_H
#define OS32_HOST_EXEC_CONTEXT_H
#include "exec_arch.h"
#include "tss.h"
#define APP_FRAME_WORDS 13
#define APP_FRAME_EAX 7
#define APP_FRAME_EIP 8
#define APP_FRAME_ESP 11
#define HOST_FRAME(saved) ((unsigned int *)(saved).opaque)
#define HOST_CONTEXT_INLINE extern inline __attribute__((gnu_inline, always_inline))
extern struct tss_entry kernel_tss;
extern void ring3_resume(const u32 *, u32, void *);
HOST_CONTEXT_INLINE void arch_priv_save(volatile struct arch_priv_context *out)
{
    unsigned int sp = tss_get_esp0();
    __builtin_memcpy((void *)out->opaque, &sp, sizeof(sp));
}
HOST_CONTEXT_INLINE void arch_priv_restore(const volatile struct arch_priv_context *saved)
{
    unsigned int sp;
    __builtin_memcpy(&sp, (const void *)saved->opaque, sizeof(sp));
    tss_set_esp0(sp);
}
HOST_CONTEXT_INLINE void arch_user_save(struct arch_user_context *out, const struct arch_syscall *call)
{
    for (unsigned k = 0; k < APP_FRAME_WORDS; k++)
        HOST_FRAME(*out)[k] = ((const u32 *)call->native)[k];
}
HOST_CONTEXT_INLINE u32 arch_syscall_slot(const struct arch_syscall *call)
{
    return ((const u32 *)call->native)[APP_FRAME_EAX];
}
HOST_CONTEXT_INLINE void arch_syscall_result(struct arch_syscall *call, u32 result)
{
    ((u32 *)call->native)[APP_FRAME_EAX] = result;
}
HOST_CONTEXT_INLINE void arch_user_result(struct arch_user_context *saved, u32 result)
{
    HOST_FRAME(*saved)[APP_FRAME_EAX] = result;
}
HOST_CONTEXT_INLINE void arch_user_restart(struct arch_user_context *saved)
{
    HOST_FRAME(*saved)[APP_FRAME_EIP] -= 2;
}
HOST_CONTEXT_INLINE void arch_user_resume(const struct arch_user_context *saved, u32 root)
{
    ring3_resume((const u32 *)saved->opaque, root, &kernel_tss);
}
#undef HOST_CONTEXT_INLINE
#endif
