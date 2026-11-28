#include "input_policy_provider.h"
#include "../input/input_policy.h"
#include "appslot.h"
#include "exec_caller_contract.h"
#include "exec_gui.h"
#include "io.h"

extern int res_owner_get(void);

typedef struct {
    u32 owner, gen;
} PolicyCaller;

/* Only the exec slot supplies identity. A reserved or retiring input entry
 * is invisible even if the slot's address is known. */
static int registered_caller(PolicyCaller *caller, u32 owner)
{
    if (!ikm_policy_ready()) return OS32_ERR_NOSYS;
    unsigned irq = irq_save();
    caller->owner = owner;
    AppSlot *slot = appslot_get((int)caller->owner);
    caller->gen = slot ? slot->exec_gen : 0;
    int valid = caller->gen && caller->gen == input_context_owner_generation(caller->owner);
    irq_restore(irq);
    return valid ? 0 : OS32_ERR_STALE;
}

static int current_caller(PolicyCaller *caller)
{
    return registered_caller(caller, res_owner_get());
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
    u32 owner = res_owner_get();
    int callback = ring3_wm_depth > 0 && ring3_gui_pump_busy();
    int wm = exec_gui_registered() && !ring3_call_from_user() &&
             (appslot_present_owner() == APP_ID_SHELL || callback);
    int rc = registered_caller(&caller, wm ? APP_ID_SHELL : owner);
    return rc ? rc : ikm_policy_key_read(caller.owner, caller.gen, wm, out, max);
}
