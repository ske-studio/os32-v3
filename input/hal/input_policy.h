#ifndef INPUT_HAL_INPUT_POLICY_H
#define INPUT_HAL_INPUT_POLICY_H
#include "types.h"

enum { IKM_HEADER_SIZE = 16, IKM_ROW_SIZE = 16, IKM_MAX_ROWS = 8192 };
typedef struct {
    u8 kind, profile, context, flags;
    u16 index, selector;
    u32 value, aux;
} IkmRow;
/* Decode framing only; row semantics and publication belong to later W stages. */
int ikm_wire_header(const u8 *wire, u32 length, u32 *count);
int ikm_wire_row(const u8 *wire, u32 length, u32 index, IkmRow *row);
/* W[2]: caller owns and clears this scratch space before each snapshot. */
enum { IKM_PLATFORM_PC98 = 1, IKM_PLATFORM_MAC = 2,
       IKM_SEEN_BITS = 170660, IKM_SEEN_BYTES = (IKM_SEEN_BITS + 7) / 8 };
typedef struct { u8 bits[IKM_SEEN_BYTES]; u8 term_bytes[20 * 32]; u16 term_ext_count; } IkmCheck;
/* Look up a validated TERM row by ID, preferring the requested context. */
int ikm_term_lookup(const IkmRow *rows, u32 count, u8 profile, u8 context,
                    u16 index, u8 *value);
void ikm_check_init(IkmCheck *check);
int ikm_check_row(IkmCheck *check, u8 platform, const IkmRow *row);
#endif
