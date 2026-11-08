/* x86 pushad/int80 context provider. Keep the 52-byte saved frame layout. */
#include "exec_arch.h"
#include "tss.h"

extern struct tss_entry kernel_tss;
extern void ring3_resume(const u32 *, u32, void *);
enum { FRAME_EAX = 7, FRAME_EIP = 8, FRAME_WORDS = 13 };
STATIC_ASSERT(sizeof(struct arch_user_context) == FRAME_WORDS * sizeof(u32), user_context_size);
STATIC_ASSERT(__alignof__(struct arch_user_context) == 4, user_context_align);
STATIC_ASSERT(sizeof(struct arch_priv_context) == sizeof(u32), priv_context_size);
STATIC_ASSERT(__alignof__(struct arch_priv_context) == 4, priv_context_align);

void arch_priv_save(volatile struct arch_priv_context *out)
{
    u32 esp0 = tss_get_esp0();
    __builtin_memcpy((void *)out->opaque, &esp0, sizeof(esp0));
}

void arch_priv_restore(const volatile struct arch_priv_context *saved)
{
    u32 esp0;
    __builtin_memcpy(&esp0, (const void *)saved->opaque, sizeof(esp0));
    tss_set_esp0(esp0);
}

void arch_user_save(struct arch_user_context *out, const struct arch_syscall *call)
{
    __builtin_memcpy(out->opaque, call->native, sizeof(out->opaque));
}

u32 arch_syscall_slot(const struct arch_syscall *call)
{
    return ((const u32 *)call->native)[FRAME_EAX];
}

void arch_syscall_result(struct arch_syscall *call, u32 result)
{
    ((u32 *)call->native)[FRAME_EAX] = result;
}

void arch_user_result(struct arch_user_context *saved, u32 result)
{
    __builtin_memcpy(saved->opaque + FRAME_EAX * sizeof(u32), &result, sizeof(result));
}

void arch_user_restart(struct arch_user_context *saved)
{
    u32 eip;
    __builtin_memcpy(&eip, saved->opaque + FRAME_EIP * sizeof(u32), sizeof(eip));
    eip -= 2;
    __builtin_memcpy(saved->opaque + FRAME_EIP * sizeof(u32), &eip, sizeof(eip));
}

void arch_user_resume(const struct arch_user_context *saved, u32 address_space)
{
    ring3_resume((const u32 *)saved->opaque, address_space, &kernel_tss);
}
