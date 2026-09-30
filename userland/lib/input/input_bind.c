/* ======================================================================== */
/*  INPUT_BIND.C — バインディング登録・解除                                  */
/* ======================================================================== */

#include "libos32input.h"

/* ====================================================================== */
/*  外部参照 (input_core.c で定義)                                          */
/* ====================================================================== */

extern InputBinding g_inp_bindings[];
extern int          g_inp_num_bindings;

/* ====================================================================== */
/*  内部関数: バインディング追加 (共通処理)                                  */
/* ====================================================================== */

static int bind_internal(int action_id, int device, int code,
                         fix16_t scale, u8 modifier_mask)
{
    InputBinding *b;

    /* バリデーション */
    if (action_id < 0 || action_id >= INPUT_MAX_ACTIONS) {
        return -1;
    }
    if (g_inp_num_bindings >= INPUT_MAX_BINDINGS) {
        return -1;
    }

    b = &g_inp_bindings[g_inp_num_bindings];
    b->action_id     = (u8)action_id;
    b->device        = (u8)device;
    b->modifier_mask = modifier_mask;
    b->_pad          = 0;
    b->code          = (u16)code;
    b->_pad2         = 0;
    b->scale         = scale;

    g_inp_num_bindings++;
    return 0;
}

/* ====================================================================== */
/*  API: バインディング追加 (修飾キー不問版 — P1互換)                       */
/* ====================================================================== */

int input_bind(int action_id, int device, int code, fix16_t scale)
{
    return bind_internal(action_id, device, code, scale, 0);
}

/* ====================================================================== */
/*  API: バインディング追加 (修飾キー指定版 — P2)                           */
/* ====================================================================== */

int input_bind_ex(int action_id, int device, int code,
                  fix16_t scale, u8 modifier_mask)
{
    return bind_internal(action_id, device, code, scale, modifier_mask);
}

/* ====================================================================== */
/*  API: 指定アクションの全バインディングを解除                              */
/* ====================================================================== */

void input_unbind(int action_id)
{
    int i, j;

    j = 0;
    for (i = 0; i < g_inp_num_bindings; i++) {
        if (g_inp_bindings[i].action_id != (u8)action_id) {
            /* 残すべきバインディングを前方にコピー */
            if (j != i) {
                g_inp_bindings[j] = g_inp_bindings[i];
            }
            j++;
        }
    }
    g_inp_num_bindings = j;
}

/* ====================================================================== */
/*  API: 全バインディングを解除                                             */
/* ====================================================================== */

void input_unbind_all(void)
{
    g_inp_num_bindings = 0;
}
