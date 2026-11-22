#include "av_contract.h"
#include "../input/hal/in2_export.h"
#include "io.h"

extern int res_owner_get(void);

static int foreground(void)
{
    int owner = res_owner_get();
    return owner && owner == av_focus_owner();
}

/* The generated wrappers validate CPL3 ranges. N5 supplies connected pads. */
int in2_pad_state(u32 slot, IN2_PadState *o)
{
    if (slot >= IN2_MAX_PADS || !o) return OS32_ERR_INVAL;
    return OS32_ERR_NOTFOUND;
}

int in2_pad_caps(u32 slot, IN2_PadCaps *o)
{
    if (slot >= IN2_MAX_PADS || !o) return OS32_ERR_INVAL;
    return OS32_ERR_NOTFOUND;
}

int in2_pad_vibrate(u32 slot, const IN2_Vibration *v)
{
    if (slot >= IN2_MAX_PADS || !v) return OS32_ERR_INVAL;
    if (!foreground()) return OS32_ERR_BUSY;
    return OS32_ERR_NOTFOUND;
}

int in2_dev_count(void)
{
    return input_src_enumerate(0, 0);
}

int in2_dev_info(u32 i, IN2_DevInfo *o)
{
    if (!o) return OS32_ERR_INVAL;
    return input_src_enumerate(i, o);
}

int in2_read(IN2_Event *out, u32 max)
{
    if (max > IN2_RING_CAPACITY || (max && !out)) return OS32_ERR_INVAL;
    if (!foreground()) return OS32_ERR_BUSY;
    InputEvent event;
    u16 seq;
    u32 n = 0;
    while (n < max && in2_ring_pop(&event, &seq)) {
        in2_export(&event, seq, &out[n++]);
    }
    return n;
}

int in2_key_state(u8 *out)
{
    if (!out) return OS32_ERR_INVAL;
    if (!foreground()) return OS32_ERR_BUSY;
    unsigned f = irq_save();
    for (int sc = 0; sc < 128; ++sc) out[sc] = input_key_pressed(sc);
    irq_restore(f);
    return 0;
}

int in2_mouse_state(IN2_MouseState *o)
{
    if (!o) return OS32_ERR_INVAL;
    if (!foreground()) return OS32_ERR_BUSY;
    unsigned f = irq_save();
    mouse_ext_read(&o->dx, &o->dy);
    o->buttons = pointer_buttons_locked();
    o->wheel = mouse_ext_read_wheel();
    irq_restore(f);
    return 0;
}

/* Enable with the 7d gshell exclusive-input handling. */
#ifndef IN2_PUBLIC_EXCLUSIVE
#define IN2_PUBLIC_EXCLUSIVE 0
#endif

int in2_acquire(u32 flags)
{
    if (!IN2_PUBLIC_EXCLUSIVE) return OS32_ERR_NOSYS;
    return in2_excl_acquire(res_owner_get(), flags);
}

int in2_unacquire(void)
{
    return in2_excl_unacquire(res_owner_get());
}

int in2_status(IN2_Status *o)
{
    if (!o) return OS32_ERR_INVAL;
    unsigned f = irq_save();
    *o = (IN2_Status){ .size = sizeof(*o), .acquired = in2_excl_acquired(res_owner_get()),
                       .focus_owner = av_focus_owner(), .focus_gen = in2_focus_generation(),
                       .dropped = in2_ring_dropped() };
    irq_restore(f);
    return 0;
}

int in2_wm_state(IN2_WmState *o)
{
    unsigned f = irq_save();
    int rc = in2_excl_wm_state(o);
    irq_restore(f);
    return rc;
}
