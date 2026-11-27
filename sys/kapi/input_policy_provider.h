#ifndef OS32_INPUT_POLICY_PROVIDER_H
#define OS32_INPUT_POLICY_PROVIDER_H

#include "../input/input_delivery.h"

/* Private targets following the caller's length and page checks. */
int input_provider_set(const void *wire, u32 length);
int input_provider_get(void *wire, u32 capacity, u32 *needed);
int input_provider_reset(void);
int input_provider_context_set(u32 context, u32 flags);
int input_provider_command_begin(void);
int input_provider_command_end(void);
int input_provider_delivery_set(u32 mode);
int input_provider_action_read(KbdPolicyAction *out, u32 max);
int input_provider_key_read(KbdPolicyKey *out, u32 max);

#endif
