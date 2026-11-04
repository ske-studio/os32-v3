/* C89 wire shared with the shell; channel and module types stay private. */
#ifndef KDEBUG_GF_COMMAND_WIRE_H
#define KDEBUG_GF_COMMAND_WIRE_H
#include "types.h"
#define GF_OP_COMMAND 17U
/* case 8 bits, connector 8 bits, head and phase one bit each. */
#define GF4_DISPLAY_SESSION_MAX ((1U << 18) - 1U)
enum gf2_case {
    GF2_STATUS, GF2_START, GF2_REFERENCE, GF2_STOP, GF2_RECOVER,
    GF2_CYCLE, GF2_BAD_HANDLE, GF2_BAD_METHOD, GF2_VBLANK, GF3_ACCEL, GF4_DISPLAY,
    GF5_FEATURE
};
/* GF5 session: op[7:0], operand index[15:8], halfword[31:16].
 * ARG_LO/HI set one of eight u32 operands, without truncating VRAM offsets.
 * Other operations require session's upper 24 bits to be zero.
 * Operand meanings (unused operands ignored):
 * CLOCK_CALC: core,shader,mem kHz; PERF_GET/SELECT: index;
 * FAN_MODE: mode; FAN_CSTATE: percent,signed direction;
 * VOLT_SET: requested uV; VOLT_ID: id,min_id,signed condition;
 * GPIO_FIND: tag; GPIO_GET/SET: line,log0,log1,logical value;
 * TILE_SET: slot,base,bytes,pitch,reservation_id,generation;
 * TILE_CLEAR: slot; WAIT: microseconds. RESTORE/FREE use the held image.
 * CLOCK_PROG/RAM_PROG use the last successful CALC (args invalidate it).
 * POLL drives the full-width clock/fan_tick through service_poll. */
#define GF5_WIRE_BYTE_MASK 0xffU
#define GF5_WIRE_HALF_MASK 0xffffU
#define GF5_WIRE_INDEX_SHIFT 8U
#define GF5_WIRE_VALUE_SHIFT 16U
#define GF5_WIRE_OPERANDS 8U
enum gf5_case {
    GF5T_INFO, GF5T_ARG_LO, GF5T_ARG_HI, GF5T_CLOCK_READ,
    GF5T_CLOCK_CALC, GF5T_CLOCK_PROG, GF5T_RAM_PROG,
    GF5T_PERF_COUNT, GF5T_PERF_GET, GF5T_PERF_SELECT,
    GF5T_THERM, GF5T_FAN_MODE, GF5T_FAN_GET, GF5T_FAN_CSTATE,
    GF5T_VOLT_GET, GF5T_VOLT_SET, GF5T_VOLT_ID,
    GF5T_GPIO_FIND, GF5T_GPIO_GET, GF5T_GPIO_SET,
    GF5T_TILE_FIND, GF5T_TILE_SET, GF5T_TILE_CLEAR,
    GF5T_SAVE, GF5T_RESTORE, GF5T_FREE, GF5T_POLL, GF5T_WAIT,
    GF5T_LAST_RC
};
struct gf2_request { u32 size, test, session; int rc; };
#endif
