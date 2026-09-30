/* ======================================================================== */
/*  INPUT_CTX.C — コンテキスト切替 (P2)                                      */
/*                                                                          */
/*  ゲーム中とメニュー画面でアクションマッピングを切り替える。                 */
/*  バインディング配列をスロットに保存・復元する。                             */
/* ======================================================================== */

#include "libos32input.h"
#include <string.h>             /* memcpy, memset */

/* ====================================================================== */
/*  外部参照 (input_core.c で定義)                                          */
/* ====================================================================== */

extern InputBinding g_inp_bindings[];
extern int          g_inp_num_bindings;
extern InputContext g_inp_ctx_slots[];

/* ====================================================================== */
/*  API: 現在のバインディングをスロットに保存                                */
/* ====================================================================== */

int input_save_context(int slot)
{
    InputContext *ctx;

    if (slot < 0 || slot >= INPUT_CTX_MAX) {
        return -1;
    }

    ctx = &g_inp_ctx_slots[slot];
    memcpy(ctx->bindings, g_inp_bindings,
           sizeof(InputBinding) * g_inp_num_bindings);
    ctx->num_bindings = g_inp_num_bindings;
    ctx->used = 1;

    return 0;
}

/* ====================================================================== */
/*  API: スロットからバインディングを復元                                     */
/* ====================================================================== */

int input_load_context(int slot)
{
    InputContext *ctx;

    if (slot < 0 || slot >= INPUT_CTX_MAX) {
        return -1;
    }

    ctx = &g_inp_ctx_slots[slot];
    if (!ctx->used) {
        return -1;
    }

    memcpy(g_inp_bindings, ctx->bindings,
           sizeof(InputBinding) * ctx->num_bindings);
    g_inp_num_bindings = ctx->num_bindings;

    return 0;
}
