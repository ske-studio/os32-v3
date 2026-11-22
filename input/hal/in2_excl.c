#include "in2_int.h"
#include "av_contract.h"
#include "io.h"

static int acq_owner;
static volatile u32 excl;
static u32 gen, focus_gen;

int in2_excl_keyboard(void)
{
    return !!(excl & IN2_EXCL_KBD);
}

static void boundary(void)
{
    ++gen;
    kbd_raw_purge_keep();
    in2_ring_lost(IN2_LOST_EXCL);
}

int in2_excl_acquire(int owner, u32 flags)
{
    if (flags & ~(IN2_EXCL_KBD | IN2_EXCL_MOUSE)) return OS32_ERR_INVAL;
    if (!owner || owner != av_focus_owner()) return OS32_ERR_BUSY;
    unsigned f = irq_save();
    if (owner != acq_owner || flags != excl) {
        acq_owner = owner;
        excl = flags;
        boundary();
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
    acq_owner = 0;
    excl = 0;
    boundary();
    return 1;
}

void in2_excl_focus(int old)
{
    if (!in2_excl_release(old)) boundary();
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
