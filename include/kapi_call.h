/* Internal logical KAPI values; native argument layout belongs to arch. */
#ifndef OS32_KAPI_CALL_H
#define OS32_KAPI_CALL_H
#include "exec_arch.h"
#include "redir_access.h"
#include "kapi_call_generated.h"
struct kapi_arg_source {
    const struct arch_syscall *call;
    struct caller_access caller;
    unsigned next;
    int caller_valid;
};
struct kapi_values { u32 value[KAPI_FIXED_MAX]; };
STATIC_ASSERT(KAPI_FIXED_MAX >= 3, private_ls_fixed_values);
void arch_kapi_source(struct kapi_arg_source *, const struct arch_syscall *);
/* Failure preserves next. One checked copy for the entire fixed block. */
int arch_kapi_read_block(struct kapi_arg_source *, unsigned first,
                         unsigned count, struct kapi_values *);
u32 arch_kapi_invoke(void *, const struct kapi_values *, unsigned count);
/* D3 connects the variadic cursor. */
int arch_kapi_va_next(struct kapi_arg_source *, enum kapi_value_kind, u32 *);
/* Transitional raw adapters: removed by D2/D3/E3, in the existing gate order. */
u32 arch_kapi_legacy_ls(const struct arch_syscall *);
int arch_kapi_legacy_print(const struct arch_syscall *, u32 slot, u32 *);
#endif
