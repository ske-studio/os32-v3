#ifndef INPUT_HAL_INPUT_POLICY_H
#define INPUT_HAL_INPUT_POLICY_H
#include "types.h"
#include "input_delivery.h"
#include "../hal/input/keymap.h"

enum { IKM_HEADER_SIZE = KBD_POLICY_HEADER_SIZE, IKM_ROW_SIZE = KBD_POLICY_ROW_SIZE,
       IKM_MAX_ROWS = KBD_POLICY_MAX_ROWS };
typedef struct {
    u8 kind, profile, context, flags;
    u16 index, selector;
    u32 value, aux;
} IkmRow;
/* Decode framing only; row semantics and publication belong to later W stages. */
int ikm_wire_header(const u8 *wire, u32 length, u32 *count);
int ikm_wire_row(const u8 *wire, u32 length, u32 index, IkmRow *row);
enum { IKM_PLATFORM_PC98 = 1, IKM_PLATFORM_MAC = 2 };
/* FG1, single task, no yield. Initialization and publication are A0z's caller.
 * owner 0 denotes a permanent boot setting; nonzero owners save once. */
int ikm_policy_init(u8 platform, const KmBackend *pc98, const KmBackend *hid,
                    int (*pressed)(void));
int ikm_policy_set(const void *wire, u32 length, u32 owner, u32 gen);
int ikm_policy_reset(u32 owner, u32 gen);
void ikm_policy_owner_exit(u32 owner, u32 gen);
int ikm_policy_get(void *wire, u32 capacity, u32 *needed);
int ikm_capture_terminal(const KmKey *key, u16 map, u16 mods,
                         const KmEvalResult *result, u32 time_ms);
int ikm_capture_gui_key(const KmKey *key, const KmSource *source, const KmState *state,
                         const KmEvalResult *result, u8 repeat, u8 consumed, u8 physical, u32 time_ms);
int ikm_policy_ready(void);
u32 ikm_policy_generation(void);
int ikm_policy_term(u8 profile, u8 context, u16 index, u8 *value);
int ikm_policy_action(u8 profile, u8 context, u16 map, u16 keycode,
                       u8 sc, u16 mods, u8 *action);
int ikm_policy_compat(u8 profile, u8 sc, u8 *byte);
int ikm_policy_action_read(u32 owner, u32 gen, KbdPolicyAction *out, u32 max);
int ikm_policy_key_read(u32 owner, u32 gen, int wm, KbdPolicyKey *out, u32 max);
#endif
