#include "input_delivery.h"
#include "input_policy.h"
#include "io.h"
#include "../hal/input/in2_int.h"

enum { CAPACITY = 128 };

static KbdPolicyAction actions[CAPACITY];
static KbdPolicyKey keys[CAPACITY];
static unsigned action_head, action_count, key_head, key_count;

/* The foreground, command and delivery-mode publishers flush in the same
 * IRQ critical section as publication. Late producers present their snapshot. */
void input_delivery_flush(void)
{
    action_head = action_count = 0;
    key_head = key_count = 0;
}

static int current(const KbdPolicySnapshot *dst)
{
    KbdPolicySnapshot now;
    return dst && !in2_excl_keyboard() && !input_context_snapshot(&now) && dst->owner == now.owner &&
           dst->exec_gen == now.exec_gen && dst->command_gen == now.command_gen;
}

int input_delivery_action(const KbdPolicySnapshot *dst, const KbdPolicyAction *action)
{
    unsigned irq = irq_save();
    int ok = action && current(dst) && action->command_gen == dst->command_gen &&
             input_context_action_mode(dst->owner, dst->exec_gen) && action_count < CAPACITY;
    if (ok) {
        actions[(action_head + action_count) % CAPACITY] = *action;
        ++action_count;
    }
    irq_restore(irq);
    return ok;
}

/* Keep the relative order of all surviving transitions, including across wrap. */
static int key_room(const KbdPolicyKey *key)
{
    if (key_count < CAPACITY) return 1;
    if (key->sc_down_repeat & KBD_POLICY_SC_REPEAT) return 0;
    for (unsigned i = 0; i < key_count; ++i) {
        if (!(keys[(key_head + i) % CAPACITY].sc_down_repeat & KBD_POLICY_SC_REPEAT))
            continue;
        while (i > 0) {
            keys[(key_head + i) % CAPACITY] = keys[(key_head + i - 1) % CAPACITY];
            --i;
        }
        key_head = (key_head + 1) % CAPACITY;
        --key_count;
        return 1;
    }
    input_gui_key_lost();
    return 0;
}

int input_delivery_key(const KbdPolicySnapshot *dst, const KbdPolicyKey *key)
{
    unsigned irq = irq_save();
    int ok = key && current(dst) && key->map_generation == ikm_policy_generation() &&
             key_room(key);
    if (ok) {
        keys[(key_head + key_count) % CAPACITY] = *key;
        ++key_count;
    }
    irq_restore(irq);
    return ok;
}

/* Private provider consumers hold IRQ exclusion from authorization to pop. */
u32 input_delivery_read_actions(KbdPolicyAction *out, u32 max)
{
    u32 n = 0;
    while (n < max && action_count) {
        out[n++] = actions[action_head];
        action_head = (action_head + 1) % CAPACITY;
        --action_count;
    }
    return n;
}

u32 input_delivery_read_keys(KbdPolicyKey *out, u32 max)
{
    u32 n = 0;
    while (n < max && key_count) {
        out[n++] = keys[key_head];
        key_head = (key_head + 1) % CAPACITY;
        --key_count;
    }
    return n;
}
