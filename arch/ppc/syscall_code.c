/* PowerPC USER syscall stubs and shared system-call entry. */
#include "syscall_code.h"
#include "ring3_str.h"
#include "ring3_ls.h"
#include "paging.h"
#include "exec_cache.h"

STATIC_ASSERT(KAPI_STUB_SIZE == 8u, ppc_syscall_stub_size);
STATIC_ASSERT(RING3_LS_SHIM_CAP >= 16, ppc_syscall_shim_size);
STATIC_ASSERT(RING3_LS_SHIM_OFF + RING3_LS_SHIM_CAP <= PAGE_SIZE, ppc_syscall_page_size);
STATIC_ASSERT(KAPI_FUNC_CAPACITY <= 32767, ppc_syscall_slot_immediate);

int arch_syscall_layout(unsigned capacity, struct syscall_layout *out)
{
    struct syscall_layout l;
    if (!out || capacity > KAPI_FUNC_CAPACITY) return 0;
    l.stub_off = RING3_USTR_STUB_OFF;
    l.stub_stride = KAPI_STUB_SIZE;
    l.stub_count = capacity;
    l.scratch_off = RING3_USTR_OFF;
    l.scratch_bytes = RING3_USTR_CAP;
    l.ls_off = RING3_LS_SHIM_OFF;
    l.ls_bytes = RING3_LS_SHIM_CAP;
    if (l.stub_off < sizeof(KernelAPI) || l.stub_off > l.scratch_off ||
        l.stub_count > (l.scratch_off - l.stub_off) / l.stub_stride ||
        l.scratch_off > l.ls_off ||
        l.scratch_bytes > l.ls_off - l.scratch_off ||
        l.ls_off > PAGE_SIZE || l.ls_bytes > PAGE_SIZE - l.ls_off) return 0;
    l.total_bytes = l.ls_off + l.ls_bytes;
    *out = l;
    return 1;
}

static void put_be32(u8 *p, u32 word)
{
    p[0] = (u8)(word >> 24);
    p[1] = (u8)(word >> 16);
    p[2] = (u8)(word >> 8);
    p[3] = (u8)word;
}

int arch_syscall_emit(void *dst, u32 user_base, u32 bytes, unsigned capacity)
{
    struct syscall_layout l;
    u8 *page = dst;
    if (!dst || !arch_syscall_layout(capacity, &l) || bytes < l.total_bytes ||
        user_base > ~(u32)0 - l.total_bytes) return 0;
    for (u32 i = 0; i < l.stub_count; i++) {
        u32 off = l.stub_off + i * l.stub_stride;
        u32 displacement = (l.ls_off + 8u) - (off + 4u);
        put_be32(page + off, 0x38000000u | i); /* li r0, slot */
        put_be32(page + off + 4u, 0x48000000u | (displacement & 0x03fffffcu));
    }
    put_be32(page + l.ls_off, 0x38600000u | ((u32)OS32_ERR_NOSYS & 0xffffu));
    put_be32(page + l.ls_off + 4u, 0x4e800020u); /* blr */
    put_be32(page + l.ls_off + 8u, 0x44000002u); /* sc */
    put_be32(page + l.ls_off + 12u, 0x4e800020u); /* blr */
    return 1;
}

void arch_syscall_publish(void *dst, u32 bytes)
{
    arch_exec_publish(dst, bytes);
}
