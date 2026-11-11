/* Initial PPC32 SysV USER entry context and linkage area. */
#include "exec_arch.h"

struct ppc_entry {
    u32 pc, sp;
    struct exec_entry_args args;
};
STATIC_ASSERT(sizeof(struct ppc_entry) == ARCH_ENTRY_BYTES, ppc_entry_size);
STATIC_ASSERT(__alignof__(struct ppc_entry) == ARCH_ENTRY_ALIGN, ppc_entry_align);

int arch_entry_prepare(struct arch_entry_context *out, u32 top, u32 entry,
                       int user, const struct exec_entry_args *args,
                       int (*store)(void *, u32, const void *, u32), void *owner)
{
    struct ppc_entry ctx;
    const u32 zero_linkage[4] = {0, 0, 0, 0};
    (void)user;
    if (top < 16) return -1;
    ctx.pc = entry;
    ctx.sp = (top & ~(u32)15) - 16;
    ctx.args = *args;
    if (store(owner, ctx.sp, zero_linkage, sizeof(zero_linkage))) return -1;
    __builtin_memcpy(out->opaque, &ctx, sizeof(ctx));
    return 0;
}
