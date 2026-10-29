#ifndef KDEBUG_GFINIT_DECODE_H
#define KDEBUG_GFINIT_DECODE_H
#include "recover_gf.h"
#include "gfinit_wire.h" /* GF_INIT_SCHEDULE / GF_INIT_MAINS */
/* MIT nouveau bios/init.c instruction names (see gfinit_decode.c). */
#define GF_INIT_IO_RESTRICT_PROG 0x32
#define GF_INIT_REPEAT 0x33
#define GF_INIT_END_REPEAT 0x36
#define GF_INIT_COPY 0x37
#define GF_INIT_NOT 0x38
#define GF_INIT_IO_FLAG_CONDITION 0x39
#define GF_INIT_IO_RESTRICT_PLL2 0x4a
#define GF_INIT_PLL2 0x4b
#define GF_INIT_CR_INDEX_ADDRESS_LATCHED 0x51
#define GF_INIT_CR 0x52
#define GF_INIT_ZM_CR 0x53
#define GF_INIT_ZM_CR_GROUP 0x54
#define GF_INIT_ZM_REG_SEQUENCE 0x58
#define GF_INIT_SUB_DIRECT 0x5b
#define GF_INIT_COPY_NV_REG 0x5f
#define GF_INIT_ZM_INDEX_IO 0x62
#define GF_INIT_COMPUTE_MEM 0x63
#define GF_INIT_RESET 0x65
#define GF_INIT_SUB 0x6b
#define GF_INIT_NV_REG 0x6e
#define GF_INIT_MACRO 0x6f
#define GF_INIT_DONE 0x71
#define GF_INIT_RESUME 0x72
#define GF_INIT_TIME 0x74
#define GF_INIT_CONDITION 0x75
#define GF_INIT_INDEX_IO 0x78
#define GF_INIT_ZM_REG 0x7a
/* Encoded shifts below GF_SHIFT_LEFT shift right; the rest shift left. */
#define GF_SHIFT_LEFT 0x80U
#define GF_SHIFT_WRAP 0x100U
#define GF_SHIFT_BITS BITS_PER_U32
#define GF_INIT_SUB_FIRST 6U
#define GF_INIT_SUB_LAST 8U
#define GF_INIT_COUNT_MAX 256U
#define GF_INIT_RESET_KEEP (~0xf00U)
#define GF_INIT_RESET_US 10U
#define GF_INIT_SCRIPTS 9U
#define GF_INIT_INSNS 193U
#define GF_INIT_ARGS 6144U /* includes S6 PREPARE and maximum BAR1 verification */
#define GF_INIT_CALL_DEPTH 16U
#define GF_INIT_REPEAT_DEPTH 8U
#define GF_INIT_WAIT_MAX 5000000U
#define GF_INIT_Q 0x10000U
#define GF_INIT_C 0x11000U
#define GF_INIT_K 0x20000U
#define GF_INIT_SCRIPT_TABLE 0x5d4cU
#define GF_INIT_MACRO_TABLE 0x5d66U
#define GF_INIT_MACRO_ENTRY 8U
#define GF_INIT_MACRO_COUNT 4U
#define GF_INIT_CONDITION_TABLE 0x5d86U
#define GF_INIT_CONDITION_ENTRY 12U
#define GF_INIT_CONDITION_COUNT 3U
#define GF_INIT_IO_FLAG_TABLE 0x5daaU
#define GF_INIT_IO_FLAG_ENTRY 9U
#define GF_INIT_IO_FLAG_COUNT 8U
#define GF_INIT_IO_FLAG_INDEX 2U
#define GF_INIT_IO_FLAG_REG_MASK 3U
#define GF_INIT_IO_FLAG_SHIFT 4U
#define GF_INIT_IO_FLAG_DATA 5U
#define GF_INIT_IO_FLAG_MASK 7U
#define GF_INIT_IO_FLAG_VALUE 8U
#define GF_INIT_CONDITION_MASK 4U
#define GF_INIT_CONDITION_VALUE 8U
#define GF_INIT_MACRO_VALUE 4U
#define GF_INIT_PLL_TABLE 0x5bc8U
#define GF_INIT_A2 0x203U
#define GF_INIT_RUNNER 0x202U /* one approval boundary per main */
#define GF_INIT_EXEC_US 60000000U
#define GF_INIT_WAIT_CHUNK_US 1000U
struct gfinit_exec_ops {
    /* Checked monotonic microseconds of execution, excluding measured UART
     * time. Error (including an unavailable calibrated clock) stops RUN. */
    int (*clock_us)(void *opaque, u32 *value);
    /* Wait at least us, with a finite hardware polling limit. */
    int (*wait_us)(void *opaque, u32 us);
};
struct gfinit_exec_frame { u32 pc, ctx, count; };
/* Owned by caller, no allocation or bus access. ROM remains borrowed only
 * during decode. A successful decode is an inventory, not permission to RUN:
 * PREPARE and the five main markers are bound/armed before runner entry. */
struct gfinit_instruction {
    u32 pc, opcode, length, end_repeat;
    u32 ref, count, port, index, mask, shift, target, value;
};
struct gf_init_program {
    u32 count, main_pc[GF_INIT_MAINS];
    struct gfinit_instruction insns[GF_INIT_INSNS];
};
struct gfinit_decode {
    struct recover_plan plan;
    struct recover_plan_node nodes[RECOVER_NODE_MAX];
    struct recover_plan_arg args[GF_INIT_ARGS];
    struct recover_plan_entry entries[GF_INIT_MAINS];
    struct recover_capture captures[RECOVER_CAPTURE_MAX];
    struct gf_init_program program;
    u32 error_pc, error;
};
/* checked instruction parser, also used by synthetic host fixtures. No CRC
 * bypass in gfinit_decode_rom: every production script must match S1. */
int gfinit_decode_instruction(const u8 *rom, u32 size, u32 pc,
                             struct gfinit_instruction *out);
int gfinit_decode_rom(const u8 *rom, u32 size, const struct recover_profile *profile,
                      struct gfinit_decode *out);
#endif
