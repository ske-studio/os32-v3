#ifndef KDEBUG_GFINIT_DECODE_H
#define KDEBUG_GFINIT_DECODE_H
#include "recover_gf.h"
#define GF_INIT_SCRIPTS 9U
#define GF_INIT_INSNS 193U
#define GF_INIT_MAINS 5U
#define GF_INIT_SCHEDULE 4096U
#define GF_INIT_CALL_DEPTH 16U
#define GF_INIT_REPEAT_DEPTH 8U
#define GF_INIT_WAIT_MAX 5000000U
#define GF_INIT_Q 0x10000U
#define GF_INIT_C 0x11000U
#define GF_INIT_K 0x20000U
#define GF_INIT_SCRIPT_TABLE 0x5d4cU
#define GF_INIT_MACRO_TABLE 0x5d66U
#define GF_INIT_CONDITION_TABLE 0x5d86U
#define GF_INIT_IO_FLAG_TABLE 0x5daaU
#define GF_INIT_PLL_TABLE 0x5bc8U
/* Owned by caller, no allocation or bus access. ROM remains borrowed only
 * during decode. A successful decode is an inventory, not permission to RUN:
 * COPY and non-PLL temporary restore entries remain unresolved until S5. */
struct gfinit_instruction {
    u32 pc, opcode, length, end_repeat, effect_first, effect_count;
    u32 ref, count, port, index, mask, shift, target, value;
};
struct gfinit_scheduled {
    u32 instruction, ctx, main_id, kind, k, count, jump;
};
struct gfinit_decode {
    struct recover_plan plan;
    struct recover_plan_node nodes[RECOVER_NODE_MAX];
    struct recover_plan_arg args[RECOVER_ARG_MAX];
    struct recover_plan_entry entries[GF_INIT_MAINS];
    struct recover_capture captures[RECOVER_CAPTURE_MAX];
    struct gfinit_instruction insns[GF_INIT_INSNS];
    struct gfinit_scheduled schedule[GF_INIT_SCHEDULE];
    u32 heads[GF_INIT_SCHEDULE][4], reachable[GF_INIT_SCHEDULE];
    u32 instruction_count, schedule_count, error_pc, error;
    u32 main_nodes[GF_INIT_MAINS], main_steps[GF_INIT_MAINS], main_wait[GF_INIT_MAINS], max_call, max_repeat;
};
/* checked instruction parser, also used by synthetic host fixtures. No CRC
 * bypass in gfinit_decode_rom: every production script must match S1. */
int gfinit_decode_instruction(const u8 *rom, u32 size, u32 pc,
                             struct gfinit_instruction *out);
int gfinit_decode_rom(const u8 *rom, u32 size, const struct recover_profile *profile,
                      struct gfinit_decode *out);
#endif
