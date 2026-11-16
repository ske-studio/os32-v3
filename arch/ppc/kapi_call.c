/* PowerPC syscall registers carry logical 32-bit values. */
#include "kapi_call.h"
#include "exec_arch.h"
#include "ppc_frame.h"
#include "os32_kapi_shared.h"

#include "ppc_kapi_adapter_generated.h"

/* The dispatcher is single-tasked and invokes immediately after extraction. */
static u32 current_slot;

u32 arch_syscall_slot(const struct arch_syscall *call)
{
    return ((const struct ppc_frame *)call->native)->gpr[0];
}

void arch_syscall_result(struct arch_syscall *call, u32 result)
{
    ((struct ppc_frame *)call->native)->gpr[3] = result;
}

void arch_kapi_source(struct kapi_arg_source *src, const struct arch_syscall *call)
{
    src->call = call;
    src->next = 0;
    src->caller_valid = 0;
    current_slot = arch_syscall_slot(call);
}

int arch_kapi_read_block(struct kapi_arg_source *src, unsigned first,
                         unsigned count, struct kapi_values *out)
{
    if (!src->caller_valid) {
        if (!caller_access_get_user(&src->caller)) return 0;
        src->caller_valid = 1;
    }
    const struct ppc_frame *frame = src->call->native;
    if (first > 8 || count > 8 - first || count > KAPI_FIXED_MAX) return 0;
    for (unsigned i = 0; i < count; i++)
        out->value[i] = frame->gpr[3 + first + i];
    src->next = first + count;
    return 1;
}

int arch_kapi_va_next(struct kapi_arg_source *src, enum kapi_value_kind kind,
                      u32 *value)
{
    const struct ppc_frame *frame = src->call->native;
    u32 word;
    /* Two fixed plus fourteen variable words, as in the public print window.
     * Integers and pointers both occupy one SysV word. */
    (void)kind;
    if (src->next >= 16u || !src->caller_valid) return 0;
    if (src->next < 8u) {
        word = frame->gpr[3 + src->next];
    } else {
        /* SysV overflow arguments follow the 8-byte linkage area. The
         * syscall_code.c stub leaves r1 unchanged, so use the saved user SP. */
        u32 offset = 8u + (src->next - 8u) * sizeof(u32);
        if (frame->gpr[1] > U32_MAX - offset - sizeof(u32)) return 0;
        u32 address = frame->gpr[1] + offset;
        if (!copy_caller_bytes(&src->caller, (const void *)(uptr)address,
                               &word, sizeof(word))) return 0;
    }
    *value = word;
    src->next++;
    return 1;
}

u32 arch_kapi_invoke(void *wrapper, const struct kapi_values *values, unsigned count)
{
    KernelAPI api;
    (void)count;
    /* Only the selected typed field is read by the generated adapter. */
    __builtin_memcpy((u8 *)&api + 2 * sizeof(u32) + current_slot * sizeof(u32),
                     &wrapper, sizeof(wrapper));
    return ppc_kapi_invoke(&api, current_slot, values);
}
