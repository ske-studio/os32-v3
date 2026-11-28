#ifndef OS32_INPUT_DELIVERY_H
#define OS32_INPUT_DELIVERY_H

#include "input_context.h"

#include "os32_kbd_policy.h"

void input_delivery_flush(void);
void input_delivery_discard_keys(void);
int input_delivery_action(const KbdPolicySnapshot *dst, const KbdPolicyAction *action);
int input_delivery_key(const KbdPolicySnapshot *dst, const KbdPolicyKey *key);
u32 input_delivery_read_actions(KbdPolicyAction *out, u32 max);
u32 input_delivery_read_keys(KbdPolicyKey *out, u32 max);

#endif
