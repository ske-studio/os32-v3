#include "input_context.h"

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
        (flags & ~(INPUT_CONTEXT_ISIG | INPUT_CONTEXT_ICANON))) return -1;
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
        return -1;
    }
    state->saved_context = state->context;
    state->saved_flags = state->flags;
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
        return -1;
    }
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
    u32 exec_gen, parent, parent_gen;
} ContextOwner;
static ContextOwner context_owners[APP_SLOT_COUNT];
static u32 foreground_owner;

/* Call only with interrupts disabled. Generation zero means unregistered. */
static int context_owner_valid(u32 owner, u32 exec_gen)
{
    return owner >= APP_ID_SHELL && owner < APP_SLOT_COUNT && exec_gen &&
           context_owners[owner].exec_gen == exec_gen;
}

int input_context_owner_register(u32 owner, u32 exec_gen, u32 parent, u32 parent_gen)
{
    unsigned int irq = irq_save();
    if (owner < APP_ID_SHELL || owner >= APP_SLOT_COUNT || !exec_gen ||
        context_owners[owner].exec_gen ||
        (parent && (!context_owner_valid(parent, parent_gen) || parent == owner)) ||
        (!parent && parent_gen)) {
        irq_restore(irq);
        return -1;
    }
    ContextOwner *entry = &context_owners[owner];
    input_context_init(&entry->policy);
    entry->parent = parent;
    entry->parent_gen = parent_gen;
    entry->exec_gen = exec_gen;
    irq_restore(irq);
    return 0;
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
    foreground_owner = APP_ID_SHELL;
    irq_restore(irq);
    return 0;
}

int input_context_owner_select(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    if (!context_owner_valid(owner, exec_gen)) {
        irq_restore(irq);
        return -1;
    }
    foreground_owner = owner;
    irq_restore(irq);
    return 0;
}

int input_context_owner_release(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    if (!context_owner_valid(owner, exec_gen)) {
        irq_restore(irq);
        return -1;
    }
    ContextOwner *entry = &context_owners[owner];
    if (foreground_owner == owner) {
        foreground_owner = context_owner_valid(entry->parent, entry->parent_gen)
                         ? entry->parent : 0;
    }
    entry->exec_gen = 0;
    irq_restore(irq);
    return 0;
}

int input_context_owner_set(u32 owner, u32 exec_gen, u32 context, u32 flags)
{
    unsigned int irq = irq_save();
    int rc = context_owner_valid(owner, exec_gen)
           ? input_context_set(&context_owners[owner].policy, context, flags) : -1;
    irq_restore(irq);
    return rc;
}

int input_context_owner_begin(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    int rc = context_owner_valid(owner, exec_gen)
           ? input_context_command_begin(&context_owners[owner].policy) : -1;
    irq_restore(irq);
    return rc;
}

int input_context_owner_end(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    int rc = context_owner_valid(owner, exec_gen)
           ? input_context_command_end(&context_owners[owner].policy) : -1;
    irq_restore(irq);
    return rc;
}

int input_context_snapshot(KbdPolicySnapshot *out)
{
    unsigned int irq = irq_save();
    u32 owner = foreground_owner;
    if (!out || !owner) {
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
