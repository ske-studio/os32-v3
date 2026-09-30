/* ======================================================================== */
/*  INPUT_CORE.C — 入力抽象化ライブラリ コア処理                             */
/*                                                                          */
/*  初期化 / 毎フレーム更新 / 終了処理 / マウスキャッシュ管理                 */
/* ======================================================================== */

#include "libos32input.h"
#include <string.h>             /* memset */

/* ====================================================================== */
/*  内部グローバル状態                                                      */
/* ====================================================================== */

/* KernelAPIポインタ (input_config.c からも参照されるため非static) */
KernelAPI *g_api;

/* アクション状態配列 */
InputActionState g_inp_actions[INPUT_MAX_ACTIONS];

/* バインディング配列 */
InputBinding g_inp_bindings[INPUT_MAX_BINDINGS];
int          g_inp_num_bindings;

/* マウスキャッシュ */
static InputMouseCache g_mouse_cache;

/* コンテキスト保存スロット (P2) */
InputContext g_inp_ctx_slots[INPUT_CTX_MAX];

/* ====================================================================== */
/*  内部関数: マウスのレイジーポーリング                                     */
/* ====================================================================== */

static void ensure_mouse_polled(void)
{
    MouseInfo mi;

    if (g_mouse_cache.polled) {
        return;
    }

    g_api->mouse_poll(&mi);

    g_mouse_cache.x       = mi.x;
    g_mouse_cache.y       = mi.y;
    g_mouse_cache.dx      = mi.dx;
    g_mouse_cache.dy      = mi.dy;
    g_mouse_cache.buttons = mi.buttons;
    g_mouse_cache.polled  = 1;
}

/* ====================================================================== */
/*  API: 初期化                                                             */
/* ====================================================================== */

int input_init(KernelAPI *api)
{
    if (!api) {
        return -1;
    }

    g_api = api;

    memset(g_inp_actions,  0, sizeof(g_inp_actions));
    memset(g_inp_bindings, 0, sizeof(g_inp_bindings));
    g_inp_num_bindings = 0;

    memset(&g_mouse_cache, 0, sizeof(g_mouse_cache));
    memset(g_inp_ctx_slots, 0, sizeof(g_inp_ctx_slots));

    return 0;
}

/* ====================================================================== */
/*  API: 毎フレーム更新                                                     */
/* ====================================================================== */

void input_update(void)
{
    int i;
    fix16_t raw;
    u32 cur_mod;

    /* 現在の修飾キー状態を1回だけ取得 */
    cur_mod = g_api->kbd_get_modifiers();

    /* 1. prev_value <- value をシフト, value リセット */
    for (i = 0; i < INPUT_MAX_ACTIONS; i++) {
        g_inp_actions[i].prev_value = g_inp_actions[i].value;
        g_inp_actions[i].value = 0;
    }

    /* 2. マウスキャッシュ無効化 */
    g_mouse_cache.polled = 0;

    /* 3. バインディング走査 */
    for (i = 0; i < g_inp_num_bindings; i++) {
        InputBinding *b = &g_inp_bindings[i];
        int aid = b->action_id;

        /* 修飾キーマスクの確認 (P2)
         * modifier_mask が非0の場合、要求される修飾キーが
         * 全て押されていなければこのバインディングをスキップ
         */
        if (b->modifier_mask != 0) {
            if ((cur_mod & b->modifier_mask) != b->modifier_mask) {
                continue;
            }
        }

        raw = 0;

        switch (b->device) {
        case INP_DEV_KEYBOARD:
            raw = g_api->kbd_is_pressed(b->code) ? FIX16_ONE : 0;
            break;

        case INP_DEV_MOUSE:
            ensure_mouse_polled();
            raw = (g_mouse_cache.buttons & (u8)b->code) ? FIX16_ONE : 0;
            break;

        case INP_DEV_GAMEPAD:
            /* 将来: シリアルブリッジ経由パケット受信 */
            break;
        }

        if (raw != 0) {
            g_inp_actions[aid].value += fix16_mul(raw, b->scale);
        }
    }

    /* 4. クランプ + hold_frames 更新 */
    for (i = 0; i < INPUT_MAX_ACTIONS; i++) {
        g_inp_actions[i].value = fix16_clamp(
            g_inp_actions[i].value, -FIX16_ONE, FIX16_ONE);

        if (g_inp_actions[i].value != 0) {
            if (g_inp_actions[i].hold_frames < 0xFFFF) {
                g_inp_actions[i].hold_frames++;
            }
        } else {
            g_inp_actions[i].hold_frames = 0;
        }
    }
}

/* ====================================================================== */
/*  API: 終了                                                               */
/* ====================================================================== */

void input_shutdown(void)
{
    memset(g_inp_actions,  0, sizeof(g_inp_actions));
    memset(g_inp_bindings, 0, sizeof(g_inp_bindings));
    g_inp_num_bindings = 0;
    memset(&g_mouse_cache, 0, sizeof(g_mouse_cache));
    memset(g_inp_ctx_slots, 0, sizeof(g_inp_ctx_slots));
    g_api = NULL;
}

/* ====================================================================== */
/*  ユーティリティ: 修飾キー                                                */
/* ====================================================================== */

u32 input_modifiers(void)
{
    if (!g_api) return 0;
    return g_api->kbd_get_modifiers();
}

/* ====================================================================== */
/*  ユーティリティ: マウス座標・差分・ボタン                                 */
/* ====================================================================== */

void input_get_mouse(i16 *x, i16 *y)
{
    ensure_mouse_polled();
    if (x) *x = g_mouse_cache.x;
    if (y) *y = g_mouse_cache.y;
}

void input_get_mouse_delta(i16 *dx, i16 *dy)
{
    ensure_mouse_polled();
    if (dx) *dx = g_mouse_cache.dx;
    if (dy) *dy = g_mouse_cache.dy;
}

int input_mouse_btn(int btn)
{
    ensure_mouse_polled();
    return (g_mouse_cache.buttons & (u8)btn) ? 1 : 0;
}
