#include "av_contract.h"
#include "in2_int.h"

__attribute__((weak)) int in2_pad_state(u32 slot, IN2_PadState *o)
{
    (void)slot;
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_pad_caps(u32 slot, IN2_PadCaps *o)
{
    (void)slot;
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_pad_vibrate(u32 slot, const IN2_Vibration *v)
{
    (void)slot;
    (void)v;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_dev_count(void)
{
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_dev_info(u32 i, IN2_DevInfo *o)
{
    (void)i;
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_read(IN2_Event *out, u32 max)
{
    (void)out;
    (void)max;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_key_state(u8 *out)
{
    (void)out;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_mouse_state(IN2_MouseState *o)
{
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_acquire(u32 flags)
{
    (void)flags;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_unacquire(void)
{
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_status(IN2_Status *o)
{
    (void)o;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int in2_wm_state(IN2_WmState *o)
{
    (void)o;
    return OS32_ERR_NOSYS;
}
