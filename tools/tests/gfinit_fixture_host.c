/* Emit the actual compiled C tables, without libc or hardware access.
 * Linux i386 host ABI only; run via host32 (qemu in worktree sandbox).
 */
#include "gfinit_fixture.h"

static void emit(const void *data, unsigned int size)
{
    const unsigned char *p = data;
    while (size) {
        int done;
        __asm__ volatile ("int $0x80" : "=a"(done)
                          : "0"(4), "b"(1), "c"(p), "d"(size) : "memory");
        if (done <= 0) {
            __asm__ volatile ("int $0x80" : : "a"(1), "b"(2) : "memory");
            __builtin_unreachable();
        }
        p += done;
        size -= (unsigned int)done;
    }
}

void _start(void)
{
    _Static_assert(sizeof(unsigned int) == 4, "fixture u32");
    _Static_assert(sizeof(struct gf_instruction) == 68, "instruction wire size");
    _Static_assert(sizeof(struct gf_effect) == 44, "effect wire size");
    _Static_assert(GF_N1_MAIN == 3 && GF_N1_PC == 0xe81e, "N1 only E81E main3");
    _Static_assert(GF_N1_CFG50 == 0x1850 && GF_N1_CFG54 == 0x1854 &&
                   GF_N1_REQUIRED_VALUE == 0, "N1 zero guards");
    emit(gf_main_order, sizeof(gf_main_order));
    emit(gf_table_spans, sizeof(gf_table_spans));
    emit(gf_scripts, sizeof(gf_scripts));
    emit(gf_instructions, sizeof(gf_instructions));
    emit(gf_effects, sizeof(gf_effects));
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(0) : "memory");
    __builtin_unreachable();
}
