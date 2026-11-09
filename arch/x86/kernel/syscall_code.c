/* x86 int80 stubs and the position-independent LS shim. */
#include "syscall_code.h"
#include "ring3_str.h"
#include "ring3_ls.h"
#include "paging.h"
#include "kstring.h"

STATIC_ASSERT(KAPI_STUB_SIZE == 8u, syscall_stub_instruction_bytes);
STATIC_ASSERT(RING3_USTR_STUB_OFF >= sizeof(KernelAPI), syscall_table_before_code);
STATIC_ASSERT(RING3_USTR_STUB_OFF <= RING3_USTR_OFF &&
              KAPI_FUNC_CAPACITY <=
              (RING3_USTR_OFF - RING3_USTR_STUB_OFF) / KAPI_STUB_SIZE,
              syscall_stubs_before_scratch);
STATIC_ASSERT(RING3_USTR_OFF <= RING3_LS_SHIM_OFF &&
              RING3_USTR_CAP <= RING3_LS_SHIM_OFF - RING3_USTR_OFF,
              syscall_scratch_before_ls);
STATIC_ASSERT(RING3_LS_SHIM_OFF <= PAGE_SIZE &&
              RING3_LS_SHIM_CAP <= PAGE_SIZE - RING3_LS_SHIM_OFF,
              syscall_code_one_page);
STATIC_ASSERT(RING3_LS_SHIM_CAP == 192, syscall_ls_capacity);

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
    /* Subtractions bound every addition/multiplication, including capacity=0. */
    if (l.stub_off < sizeof(KernelAPI) || l.stub_off > l.scratch_off ||
        l.stub_count > (l.scratch_off - l.stub_off) / l.stub_stride ||
        l.scratch_off > l.ls_off ||
        l.scratch_bytes > l.ls_off - l.scratch_off ||
        l.ls_off > PAGE_SIZE || l.ls_bytes > PAGE_SIZE - l.ls_off) return 0;
    l.total_bytes = l.ls_off + l.ls_bytes;
    *out = l;
    return 1;
}

int arch_syscall_emit(void *dst, u32 user_base, u32 bytes, unsigned capacity)
{
    struct syscall_layout l;
    u32 shim_bytes = (u32)(ring3_ls_end - ring3_ls_start);
    u8 *page = dst;
    if (!dst || !arch_syscall_layout(capacity, &l) || bytes < l.total_bytes ||
        user_base > ~(u32)0 - l.total_bytes || shim_bytes > l.ls_bytes) return 0;
    /* Include reserved slots: dispatch must kill the child, never jump to NULL. */
    for (u32 i = 0; i < l.stub_count; i++) {
        u8 *st = page + l.stub_off + i * l.stub_stride;
        st[0] = 0xB8;
        st[1] = (u8)(i & 0xFF);
        st[2] = (u8)((i >> 8) & 0xFF);
        st[3] = (u8)((i >> 16) & 0xFF);
        st[4] = (u8)((i >> 24) & 0xFF);
        st[5] = 0xCD;
        st[6] = 0x80;
        st[7] = 0xC3;
    }
    kmemcpy(page + l.ls_off, ring3_ls_start, shim_bytes);
    return 1;
}

void arch_syscall_publish(void *dst, u32 bytes)
{
    /* x86 instruction/data caches are coherent. */
    (void)dst;
    (void)bytes;
}
