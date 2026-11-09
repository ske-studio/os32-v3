/* Architecture-owned USER syscall code page construction. */
#ifndef SYSCALL_CODE_H
#define SYSCALL_CODE_H
#include "types.h"

struct syscall_layout {
    u32 stub_off, stub_stride, stub_count;
    u32 scratch_off, scratch_bytes, ls_off, ls_bytes, total_bytes;
};
/* Return 0 on invalid capacity/layout or insufficient output space.
 * Offsets are relative to dst/user_base. No table pointers are published here. */
int arch_syscall_layout(unsigned capacity, struct syscall_layout *layout);
int arch_syscall_emit(void *dst, u32 user_base, u32 bytes, unsigned capacity);
/* After every instruction write and before exposing the USER table.
 * Backends requiring D-cache writeback/I-cache invalidation order them here. */
void arch_syscall_publish(void *dst, u32 bytes);
#endif
