#include "input_policy_provider.h"
#include "../input/input_policy.h"
#include "appslot.h"
#include "exec_caller_contract.h"
#include "av_contract.h"
#include "io.h"

extern int res_owner_get(void);

typedef struct {
    u32 owner, gen;
} PolicyCaller;

/* Only the exec slot supplies identity. A reserved or retiring input entry
 * is invisible even if the slot's address is known. */
static int current_caller(PolicyCaller *caller)
{
    if (!ikm_policy_ready()) return OS32_ERR_NOSYS;
    unsigned irq = irq_save();
    caller->owner = res_owner_get();
    AppSlot *slot = appslot_get((int)caller->owner);
    caller->gen = slot ? slot->exec_gen : 0;
    int valid = caller->gen && caller->gen == input_context_owner_generation(caller->owner);
    irq_restore(irq);
    return valid ? 0 : OS32_ERR_STALE;
}

int input_provider_set(const void *wire, u32 length)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    return rc ? rc : ikm_policy_set(wire, length, caller.owner, caller.gen);
}

int input_provider_get(void *wire, u32 capacity, u32 *needed)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    return rc ? rc : ikm_policy_get(wire, capacity, needed);
}

int input_provider_reset(void)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    return rc ? rc : ikm_policy_reset(caller.owner, caller.gen);
}

int input_provider_context_set(u32 context, u32 flags)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    return rc ? rc : input_context_owner_set(caller.owner, caller.gen, context, flags);
}

int input_provider_command_begin(void)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    return rc ? rc : input_context_owner_begin(caller.owner, caller.gen);
}

int input_provider_command_end(void)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    return rc ? rc : input_context_owner_end(caller.owner, caller.gen);
}

int input_provider_delivery_set(u32 mode)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    if (rc) return rc;
    KbdPolicySnapshot now;
    if (input_context_snapshot(&now) || now.owner != caller.owner) return OS32_ERR_BUSY;
    if (mode > 1) return OS32_ERR_INVAL;
    if (input_context_action_mode(caller.owner, caller.gen) == (int)mode) return 0;
    unsigned irq;
    if (input_display_restore_lock(&irq)) return OS32_ERR_BUSY;
    rc = input_context_delivery_set(caller.owner, caller.gen, mode);
    irq_restore(irq);
    return rc;
}

int input_provider_action_read(KbdPolicyAction *out, u32 max)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    return rc ? rc : ikm_policy_action_read(caller.owner, caller.gen, out, max);
}

int input_provider_key_read(KbdPolicyKey *out, u32 max)
{
    PolicyCaller caller;
    int rc = current_caller(&caller);
    if (rc) return rc;
    int wm = caller.owner == APP_ID_SHELL && !ring3_call_from_user() && av_focus_is_wm();
    return ikm_policy_key_read(caller.owner, caller.gen, wm, out, max);
}
