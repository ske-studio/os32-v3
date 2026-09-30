/* ======================================================================== */
/*  INPUT_QUERY.C — アクション状態問い合わせ                                 */
/* ======================================================================== */

#include "libos32input.h"

extern InputActionState g_inp_actions[];

int input_pressed(int action_id)
{
    if (action_id < 0 || action_id >= INPUT_MAX_ACTIONS) return 0;
    return g_inp_actions[action_id].value != 0;
}

int input_triggered(int action_id)
{
    if (action_id < 0 || action_id >= INPUT_MAX_ACTIONS) return 0;
    return (g_inp_actions[action_id].prev_value == 0 &&
            g_inp_actions[action_id].value != 0);
}

int input_released(int action_id)
{
    if (action_id < 0 || action_id >= INPUT_MAX_ACTIONS) return 0;
    return (g_inp_actions[action_id].prev_value != 0 &&
            g_inp_actions[action_id].value == 0);
}

fix16_t input_value(int action_id)
{
    if (action_id < 0 || action_id >= INPUT_MAX_ACTIONS) return 0;
    return g_inp_actions[action_id].value;
}

int input_held(int action_id, int hold, int repeat)
{
    u16 frames;
    int elapsed;

    if (action_id < 0 || action_id >= INPUT_MAX_ACTIONS) return 0;

    frames = g_inp_actions[action_id].hold_frames;
    if (frames == 0) return 0;
    if (frames == 1) return 1;
    if (hold == 0) return 0;
    if ((int)frames == hold + 1) return 1;

    if (repeat > 0 && (int)frames > hold + 1) {
        elapsed = (int)frames - hold - 1;
        if (elapsed % repeat == 0) return 1;
    }
    return 0;
}
