/* Declaration/layout fixture only: does not pretend to execute a B/C emulator. */
#include "x87_emu.h"
#include "x87.h"
#include "lib/softfloat_bits.h"
#include "kernel/ver_info.h"
#include <stddef.h>

#define TYPE(fn, signature) STATIC_ASSERT( \
    __builtin_types_compatible_p(__typeof__(&(fn)), signature), fn##_signature)
TYPE(arch_fp_present, int (*)(void));
TYPE(x87_probe_nm, int (*)(u32, u32, u32 *));
TYPE(softfp_f64_to_i32_trunc, i32 (*)(unsigned long long));
TYPE(x87_emu_decode, enum x87_emu_result (*)(const struct x87_emu_trap *,
    x87_emu_fetch_fn, void *, struct x87_emu_decoded *));
TYPE(x87_emu_dispatch, enum x87_emu_result (*)(struct x87_emu_trap *,
    const struct addrspace *, struct x87_emu_state *));
TYPE(x87_emu_read, enum x87_emu_result (*)(const struct x87_emu_access *,
    const struct x87_emu_decoded *, u8 *));
TYPE(x87_emu_write, enum x87_emu_result (*)(const struct x87_emu_access *,
    const struct x87_emu_decoded *, const u8 *));
TYPE(x87_emu_state_init, void (*)(struct x87_emu_state *, bool));
TYPE(x87_emu_state_encode, void (*)(const struct x87_emu_state *, u8 *, bool, bool));
TYPE(x87_emu_state_decode, void (*)(struct x87_emu_state *, const u8 *, bool, bool));
TYPE(x87_emu_execute, enum x87_emu_result (*)(const struct x87_emu_trap *,
    const struct x87_emu_access *, struct x87_emu_decoded *, struct x87_emu_effect *));
TYPE(x87_emu_scope_begin, void (*)(u16, struct x87_emu_softfloat_scope *));
TYPE(x87_emu_scope_end, void (*)(const struct x87_emu_softfloat_scope *, struct x87_emu_effect *));
TYPE(x87_emu_exception, enum x87_emu_result (*)(const struct x87_emu_state *,
    const struct x87_emu_decoded *, struct x87_emu_effect *));

static int check(void)
{
    /* Assembly adapter and codec writers depend on these independently fixed
     * architectural sizes, indices and offsets, not sizeof-derived constants. */
    if (X87_EMU_INSN_BYTES != 15 || X87_EMU_MEMORY_BYTES != 108) return 1;
    if (X87_EMU_ENV_BYTES != 28 || X87_EMU_ENV16_BYTES != 14 ||
        X87_EMU_SAVE16_BYTES != 94) return 2;
    if (sizeof(struct arch_fp_context) != 108 ||
        __alignof__(struct arch_fp_context) != 1) return 3;
    if (sizeof(struct x87_emu_ext80) != 10 ||
        sizeof(struct x87_emu_state) != 108 ||
        offsetof(struct x87_emu_state, reg) != 28) return 4;
    if (offsetof(struct x87_emu_state, fip) != 12 ||
        offsetof(struct x87_emu_state, fop) != 18 ||
        offsetof(struct x87_emu_state, fdp) != 20) return 5;
    if (X87_EAX != 0 || X87_ESP != 4 || X87_EBP != 5 || X87_EDI != 7 ||
        X87_CS != 1 || X87_SS != 2 || X87_DS != 3) return 6;
    if (sizeof(((struct x87_emu_effect *)0)->output) != 108 ||
        sizeof(((struct x87_emu_softfloat_scope *)0)->ext80_precision) != 1 ||
        sizeof(((VerSnapshot *)0)->fpu_present) != 4) return 7;
    if ((X87_COMMIT_STATUS | X87_COMMIT_ENV | X87_COMMIT_REGS |
         X87_COMMIT_MEMORY | X87_COMMIT_GPR | X87_COMMIT_EIP) != 63) return 8;
    return 0;
}
void _start(void)
{
    int code = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) {}
}
