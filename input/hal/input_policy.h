#ifndef INPUT_HAL_INPUT_POLICY_H
#define INPUT_HAL_INPUT_POLICY_H
#include "types.h"
#include "keymap.h"

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
/* FG1, single task, no yield. Initialization and publication are A0z's caller.
 * owner 0 denotes a permanent boot setting; nonzero owners save once. */
int ikm_policy_init(u8 platform, const KmBackend *pc98, const KmBackend *hid,
                    int (*pressed)(void));
int ikm_policy_set(const void *wire, u32 length, u32 owner, u32 gen);
int ikm_policy_reset(u32 owner, u32 gen);
void ikm_policy_owner_exit(u32 owner, u32 gen);
int ikm_policy_get(void *wire, u32 capacity, u32 *needed);
u32 ikm_policy_generation(void);
int ikm_policy_term(u8 profile, u8 context, u16 index, u8 *value);
int ikm_policy_action(u8 profile, u8 context, u16 map, u16 keycode,
                       u8 sc, u16 mods, u8 *action);
int ikm_policy_compat(u8 profile, u8 sc, u8 *byte);
#endif
