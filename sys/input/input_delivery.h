#ifndef OS32_INPUT_DELIVERY_H
#define OS32_INPUT_DELIVERY_H

#include "input_context.h"

typedef struct {
    u32 action, command_gen, time_ms, value;
} KbdPolicyAction;

typedef struct {
    u32 src_generation, map_id, keycode, sc_down_repeat;
    u32 physical_mods, logical_mods, time_ms, map_generation;
    u32 keysym, kind, action, value;
} KbdPolicyKey;

void input_delivery_flush(void);
int input_delivery_action(const KbdPolicySnapshot *dst, const KbdPolicyAction *action);
int input_delivery_key(const KbdPolicySnapshot *dst, const KbdPolicyKey *key);
u32 input_delivery_read_actions(KbdPolicyAction *out, u32 max);
u32 input_delivery_read_keys(KbdPolicyKey *out, u32 max);

#endif
