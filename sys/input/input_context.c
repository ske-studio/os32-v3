#include "input_context.h"
#include "input_delivery.h"
#include "os32_kapi_shared.h"

void input_context_init(KbdPolicyContext *state)
{
    state->context = INPUT_CONTEXT_RUNNING;
    state->flags = INPUT_CONTEXT_ISIG;
    state->command_gen = 0;
    state->next_gen = 0;
    state->command_active = 0;
    state->saved_context = 0;
    state->saved_flags = 0;
}

int input_context_set(KbdPolicyContext *state, u32 context, u32 flags)
{
    if (context < INPUT_CONTEXT_LINE_EDIT || context > INPUT_CONTEXT_GUI ||
        (flags & ~(INPUT_CONTEXT_ISIG | INPUT_CONTEXT_ICANON))) return OS32_ERR_INVAL;
    state->context = context;
    state->flags = flags;
    return 0;
}

#include "io.h"

static void (*discard_pending)(void);

void input_context_discard_register(void (*discard)(void))
{
    unsigned int irq = irq_save();
    discard_pending = discard;
    irq_restore(irq);
}

int input_context_command_begin(KbdPolicyContext *state)
{
    unsigned int irq = irq_save();
    if (state->command_active || !discard_pending) {
        irq_restore(irq);
        return OS32_ERR_INVAL;
    }
    state->saved_context = state->context;
    state->saved_flags = state->flags;
    input_delivery_flush();
    discard_pending();
    if (++state->next_gen == 0) ++state->next_gen;
    state->context = INPUT_CONTEXT_RUNNING;
    state->flags = INPUT_CONTEXT_ISIG;
    state->command_active = 1;
    state->command_gen = state->next_gen;
    irq_restore(irq);
    return 0;
}

int input_context_command_end(KbdPolicyContext *state)
{
    unsigned int irq = irq_save();
    if (!state->command_active || !discard_pending) {
        irq_restore(irq);
        return OS32_ERR_INVAL;
    }
    input_delivery_flush();
    discard_pending();
    state->command_gen = 0;
    state->context = state->saved_context;
    state->flags = state->saved_flags;
    state->command_active = 0;
    irq_restore(irq);
    return 0;
}

#include "app_ids.h"

typedef struct {
    KbdPolicyContext policy;
    u32 exec_gen, parent, parent_gen, action_mode;
    enum { OWNER_FREE, OWNER_RESERVED, OWNER_ACTIVE, OWNER_RETIRING } state;
} ContextOwner;
static ContextOwner context_owners[APP_SLOT_COUNT];
static u32 foreground_owner;
static void (*flush_focus)(void);

void input_context_focus_register(void (*flush)(void))
{
    unsigned flags = irq_save();
    flush_focus = flush;
    irq_restore(flags);
}

static void select_foreground(u32 owner)
{
    if (foreground_owner != owner) {
        input_delivery_flush();
        if (flush_focus) flush_focus();
    }
    foreground_owner = owner;
}

/* Call only with interrupts disabled. Generation zero means unregistered. */
static int context_owner_valid(u32 owner, u32 exec_gen)
{
    return owner >= APP_ID_SHELL && owner < APP_SLOT_COUNT && exec_gen &&
           context_owners[owner].state == OWNER_ACTIVE && context_owners[owner].exec_gen == exec_gen;
}

int input_context_owner_reserve(u32 owner, u32 exec_gen, u32 parent, u32 parent_gen, u32 context)
{
    unsigned int irq = irq_save();
    if (owner < APP_ID_SHELL || owner >= APP_SLOT_COUNT || !exec_gen ||
        context_owners[owner].state != OWNER_FREE ||
        (context != INPUT_CONTEXT_RUNNING && context != INPUT_CONTEXT_GUI) ||
        (parent && (!context_owner_valid(parent, parent_gen) || parent == owner)) ||
        (!parent && parent_gen)) {
        irq_restore(irq);
        return -1;
    }
    ContextOwner *entry = &context_owners[owner];
    input_context_init(&entry->policy);
    entry->policy.context = context;
    entry->state = OWNER_RESERVED;
    entry->action_mode = 0;
    entry->parent = parent;
    entry->parent_gen = parent_gen;
    entry->exec_gen = exec_gen;
    irq_restore(irq);
    return 0;
}

/* The reserving exec caller holds the slot until activation. */
void input_context_owner_activate(u32 owner, int foreground)
{
    unsigned irq = irq_save();
    context_owners[owner].state = OWNER_ACTIVE;
    if (foreground) select_foreground(owner);
    irq_restore(irq);
}

int input_context_owner_register(u32 owner, u32 exec_gen, u32 parent, u32 parent_gen)
{
    unsigned irq = irq_save();
    int rc = input_context_owner_reserve(owner, exec_gen, parent, parent_gen,
                                         INPUT_CONTEXT_RUNNING);
    if (!rc) input_context_owner_activate(owner, 0);
    irq_restore(irq);
    return rc;
}

int input_context_first_shell(u32 exec_gen)
{
    unsigned int irq = irq_save();
    if (foreground_owner || input_context_owner_register(APP_ID_SHELL, exec_gen, 0, 0)) {
        irq_restore(irq);
        return -1;
    }
    input_context_set(&context_owners[APP_ID_SHELL].policy, INPUT_CONTEXT_LINE_EDIT,
                      INPUT_CONTEXT_ISIG | INPUT_CONTEXT_ICANON);
    select_foreground(APP_ID_SHELL);
    irq_restore(irq);
    return 0;
}

u32 input_context_owner_generation(u32 owner)
{
    unsigned irq = irq_save();
    u32 gen = owner < APP_SLOT_COUNT && context_owners[owner].state == OWNER_ACTIVE
            ? context_owners[owner].exec_gen : 0;
    irq_restore(irq);
    return gen;
}

int input_context_owner_select(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    if (!context_owner_valid(owner, exec_gen)) {
        irq_restore(irq);
        return -1;
    }
    select_foreground(owner);
    irq_restore(irq);
    return 0;
}

/* Keep the slot occupied until display cleanup finishes. IRQ snapshots and
 * callers can no longer resolve the departing generation. */
void input_context_owner_quiesce(u32 owner, u32 gen)
{
    unsigned irq = irq_save();
    if (context_owner_valid(owner, gen)) {
        context_owners[owner].state = OWNER_RETIRING;
        if (owner == foreground_owner) input_delivery_flush();
    }
    irq_restore(irq);
}

int input_context_owner_release(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    if (owner < APP_ID_SHELL || owner >= APP_SLOT_COUNT || !exec_gen ||
        context_owners[owner].exec_gen != exec_gen ||
        (context_owners[owner].state != OWNER_ACTIVE && context_owners[owner].state != OWNER_RETIRING)) {
        irq_restore(irq);
        return -1;
    }
    ContextOwner *entry = &context_owners[owner];
    /* Splice every live child before this slot can be reused. */
    for (u32 child = APP_ID_SHELL; child < APP_SLOT_COUNT; ++child) {
        ContextOwner *next = &context_owners[child];
        if (next->exec_gen && next->parent == owner && next->parent_gen == exec_gen) {
            next->parent = entry->parent;
            next->parent_gen = entry->parent_gen;
        }
    }
    if (foreground_owner == owner) {
        u32 next = context_owner_valid(entry->parent, entry->parent_gen)
                         ? entry->parent : 0;
        if (!next && owner != APP_ID_SHELL &&
            context_owners[APP_ID_SHELL].state == OWNER_ACTIVE)
            next = APP_ID_SHELL;
        select_foreground(next);
    }
    entry->state = OWNER_FREE;
    entry->exec_gen = 0;
    irq_restore(irq);
    return 0;
}

int input_context_owner_set(u32 owner, u32 exec_gen, u32 context, u32 flags)
{
    unsigned int irq = irq_save();
    int rc = context_owner_valid(owner, exec_gen)
           ? input_context_set(&context_owners[owner].policy, context, flags) : OS32_ERR_STALE;
    irq_restore(irq);
    return rc;
}

int input_context_action_mode(u32 owner, u32 gen)
{
    return context_owner_valid(owner, gen) && context_owners[owner].action_mode;
}

/* Caller holds IRQ exclusion through the command or mode transition. */
static int foreground_status(u32 owner, u32 gen)
{
    if (!context_owner_valid(owner, gen)) return OS32_ERR_STALE;
    return foreground_owner == owner ? 0 : OS32_ERR_BUSY;
}

int input_context_delivery_set(u32 owner, u32 gen, u32 mode)
{
    unsigned irq = irq_save();
    int rc = foreground_status(owner, gen);
    if (!rc && mode > 1) rc = OS32_ERR_INVAL;
    if (!rc && context_owners[owner].action_mode != mode) {
        if (flush_focus) flush_focus();
        if (discard_pending) discard_pending();
        input_delivery_flush();
        context_owners[owner].action_mode = mode;
    }
    irq_restore(irq);
    return rc;
}

int input_context_owner_begin(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    int rc = foreground_status(owner, exec_gen);
    if (!rc) rc = input_context_command_begin(&context_owners[owner].policy);
    irq_restore(irq);
    return rc;
}

int input_context_owner_end(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    int rc = foreground_status(owner, exec_gen);
    if (!rc) rc = input_context_command_end(&context_owners[owner].policy);
    irq_restore(irq);
    return rc;
}

int input_context_snapshot(KbdPolicySnapshot *out)
{
    unsigned int irq = irq_save();
    u32 owner = foreground_owner;
    if (!out || !owner || context_owners[owner].state != OWNER_ACTIVE) {
        irq_restore(irq);
        return -1;
    }
    const ContextOwner *entry = &context_owners[owner];
    out->owner = owner;
    out->exec_gen = entry->exec_gen;
    out->command_gen = entry->policy.command_gen;
    out->context = entry->policy.context;
    out->flags = entry->policy.flags;
    irq_restore(irq);
    return 0;
}
