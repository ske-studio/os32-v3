#include "av_contract.h"
#include "exec_gui.h"
#include "app_ids.h"
#include "io.h"
#include "../input/input_context.h"
#include "../input/input_delivery.h"
#include "../hal/input/in2_int.h"

extern int res_owner_get(void);
extern int ring3_call_from_user(void);
extern volatile int ring3_wm_depth;

static volatile int av_fg_owner;

static void select_owner(int owner)
{
    int old = av_fg_owner;
    if (old == owner) return;
    av_fg_owner = owner;
    in2_on_focus(old, owner);
}

int av_focus_is_wm(void)
{
    if (!exec_gui_registered() ||
        (ring3_wm_depth <= 0 &&
         (res_owner_get() != APP_ID_SHELL || ring3_call_from_user())))
        return 0;
    return 1;
}

int av_focus_set(int owner)
{
    if (!av_focus_is_wm()) return OS32_ERR_INVAL;
    unsigned irq = irq_save();
    u32 destination = owner ? (u32)owner : APP_ID_SHELL;
    int rc = input_context_owner_select(destination, input_context_owner_generation(destination));
    if (!rc) {
        input_delivery_flush();
        select_owner(owner);
    }
    irq_restore(irq);
    return rc ? OS32_ERR_INVAL : 0;
}

int av_focus_owner(void)
{
    return av_fg_owner;
}

int av_focus_get(void)
{
    return av_focus_owner();
}

void av_focus_exec_enter(int id)
{
    if (!exec_gui_registered()) select_owner(id);
}

void av_focus_exec_leave(int id, int parent)
{
    if (!exec_gui_registered()) select_owner(parent);
    else if (av_fg_owner == id) select_owner(0);
}
