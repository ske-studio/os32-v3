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
#endif
