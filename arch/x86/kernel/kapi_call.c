/* x86 cdecl argument extraction and invocation. */
#include "kapi_call.h"
extern u32 kapi_invoke(void *, const void *, u32);

void arch_kapi_source(struct kapi_arg_source *src, const struct arch_syscall *call)
{
    src->call = call;
    src->next = 0;
    src->caller_valid = 0;
}

int arch_kapi_read_block(struct kapi_arg_source *src, unsigned first,
                         unsigned count, struct kapi_values *out)
{
    u32 sp = ((const u32 *)src->call->native)[11];
    if (count > KAPI_FIXED_MAX || first > U32_MAX - count ||
        first > U32_MAX / sizeof(u32) || count > U32_MAX / sizeof(u32)) return 0;
    u32 offset = first * sizeof(u32);
    u32 bytes = count * sizeof(u32);
    if (sp > U32_MAX - sizeof(u32)) return 0;
    sp += sizeof(u32);
    if (sp > U32_MAX - offset) return 0;
    sp += offset;
    if (sp > U32_MAX - bytes) return 0;
    if (!src->caller_valid) {
        if (!caller_access_get_user(&src->caller)) return 0;
        src->caller_valid = 1;
    }
    if (!copy_caller_bytes(&src->caller, (const void *)(uptr)sp,
                           out->value, bytes)) return 0;
    src->next = first + count;
    return 1;
}

int arch_kapi_va_next(struct kapi_arg_source *src, enum kapi_value_kind kind,
                      u32 *value)
{
    /* The public print window has two fixed and fourteen variable words.
     * Both kinds occupy one cdecl word; future ABIs may place them separately. */
    struct kapi_values word;
    (void)kind;
    if (src->next >= 16u) return 0;
    if (!arch_kapi_read_block(src, src->next, 1, &word)) return 0;
    *value = word.value[0];
    return 1;
}

u32 arch_kapi_invoke(void *wrapper, const struct kapi_values *values, unsigned count)
{
    return kapi_invoke(wrapper, values->value, count * sizeof(u32));
}
