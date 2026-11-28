#include "in2_int.h"
#include "../../input/input_delivery.h"
#include "av_contract.h"
#include "io.h"
#include "kbd.h"

static int acq_owner;
static volatile u32 excl;
static u32 gen, focus_gen;

int in2_excl_keyboard(void)
{
    return !!(excl & IN2_EXCL_KBD);
}

static u8 valid[128];
static u32 saved_gen[128];

int in2_excl_drop_raw(u8 sc, int is_break)
{
    if (sc >= 128 || sc == KEY_STOP || sc == KEY_CTRL ||
        sc == KEY_GRPH || sc == KEY_TAB) return 0;
    return in2_excl_keyboard() ||
           (!is_break && valid[sc] && saved_gen[sc] == input_key_release_gen(sc));
}

/* The caller holds IRQ exclusion through the snapshot, purge and LOST event. */
static void transition(int owner, u32 next, int focus)
{
    if (focus || (excl & ~next & IN2_EXCL_KBD)) {
        for (unsigned sc = 0; sc < 128; ++sc) {
            valid[sc] = input_key_pressed(sc);
            saved_gen[sc] = input_key_release_gen(sc);
        }
    }
    /* An acquisition starts a fresh raw stream, including WM controls. */
    if (!(excl & ~next & IN2_EXCL_KBD) && !focus && owner && (owner != acq_owner || (next & ~excl)))
        legacy_raw_flush();
    else
        kbd_raw_purge_keep();
    if (excl & ~next & IN2_EXCL_KBD) legacy_raw_mark_release();
    acq_owner = owner;
    excl = next;
    input_delivery_flush();
    ++gen;
    in2_ring_lost(IN2_LOST_EXCL);
}

int in2_excl_acquire(int owner, u32 flags)
{
    if (flags & ~(IN2_EXCL_KBD | IN2_EXCL_MOUSE)) return OS32_ERR_INVAL;
    if (!owner || owner != av_focus_owner()) return OS32_ERR_BUSY;
    unsigned f = irq_save();
    if (owner != acq_owner || flags != excl) {
        transition(owner, flags, 0);
        ++focus_gen;
    }
    irq_restore(f);
    return 0;
}

int in2_excl_unacquire(int owner)
{
    if (!owner || owner != av_focus_owner()) return OS32_ERR_BUSY;
    unsigned f = irq_save();
    if (in2_excl_release(owner)) ++focus_gen;
    irq_restore(f);
    return 0;
}

int in2_excl_release(int owner)
{
    if (!acq_owner || acq_owner != owner) return 0;
    transition(0, 0, 0);
    return 1;
}

void in2_excl_focus(int old)
{
    if (acq_owner == old) transition(0, 0, 1);
    else transition(acq_owner, excl, 1);
    ++focus_gen;
}

u32 in2_focus_generation(void)
{
    return focus_gen;
}

void in2_reclaim_generation(void)
{
    ++focus_gen;
}

int in2_excl_wm_state(IN2_WmState *out)
{
    if (!out || !av_focus_is_wm()) return OS32_ERR_INVAL;
    out->size = sizeof(*out);
    out->gen = gen;
    out->excl = excl;
    out->owner = acq_owner;
    return 0;
}

int in2_excl_acquired(int owner)
{
    return owner && owner == acq_owner;
}
