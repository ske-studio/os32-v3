/* ======================================================================== */
/*  INPUT_TEST.C — libos32input テストプログラム (Phase 1 + Phase 2)          */
/*                                                                          */
/*  キーボード/マウスのバインディングを登録し、                               */
/*  入力状態をCUIでリアルタイム表示する。ESCで終了。                          */
/*                                                                          */
/*  Phase 2 テスト:                                                          */
/*    - コンテキスト切替 (save_context / load_context)                       */
/*    - 複合キー (Ctrl+S / Ctrl+L バインディング)                           */
/*    - キーコンフィグ保存・読み込み                                          */
/* ======================================================================== */

#include "os32api.h"
#include "libos32input.h"
#include "rt/testresult.h"

/* アクションID */
#define ACT_MOVE_UP     0
#define ACT_MOVE_DOWN   1
#define ACT_MOVE_LEFT   2
#define ACT_MOVE_RIGHT  3
#define ACT_CONFIRM     4
#define ACT_CANCEL      5
#define ACT_MENU        6
#define ACT_SAVE        7   /* Ctrl+S — P2 */
#define ACT_LOAD        8   /* Ctrl+L — P2 */


static KernelAPI *api;
static int pass_count;
static int fail_count;

#define TEST(msg, cond) do { \
    if (cond) { \
        pass_count++; \
    } else { \
        api->kprintf(ATTR_RED, "FAIL: %s\n", msg); \
        fail_count++; \
    } \
} while(0)

static void setup_bindings(void)
{
    /* 方向キー */
    input_bind(ACT_MOVE_UP,    INP_DEV_KEYBOARD, 0x3A, FIX16_ONE);
    input_bind(ACT_MOVE_DOWN,  INP_DEV_KEYBOARD, 0x3D, FIX16_ONE);
    input_bind(ACT_MOVE_LEFT,  INP_DEV_KEYBOARD, 0x3B, FIX16_ONE);
    input_bind(ACT_MOVE_RIGHT, INP_DEV_KEYBOARD, 0x3C, FIX16_ONE);

    /* 決定 = Space or マウス左 */
    input_bind(ACT_CONFIRM, INP_DEV_KEYBOARD, 0x34, FIX16_ONE);
    input_bind(ACT_CONFIRM, INP_DEV_MOUSE, MOUSE_BTN_LEFT, FIX16_ONE);

    /* キャンセル = Escape or マウス右 */
    input_bind(ACT_CANCEL, INP_DEV_KEYBOARD, 0x00, FIX16_ONE);
    input_bind(ACT_CANCEL, INP_DEV_MOUSE, MOUSE_BTN_RIGHT, FIX16_ONE);

    /* メニュー = F1 */
    input_bind(ACT_MENU, INP_DEV_KEYBOARD, 0x62, FIX16_ONE);

    /* P2: 複合キーバインディング */
    input_bind_ex(ACT_SAVE, INP_DEV_KEYBOARD, 0x1F, FIX16_ONE, INP_MOD_CTRL);
    input_bind_ex(ACT_LOAD, INP_DEV_KEYBOARD, 0x25, FIX16_ONE, INP_MOD_CTRL);
}

/* ====================================================================== */
/*  P2 自動テスト (API呼び出しの結果検証)                                    */
/* ====================================================================== */

static void run_unit_tests(void)
{
    int ret;

    api->kprintf(ATTR_CYAN, "\n--- Phase 2 unit tests ---\n");

    /* --- コンテキスト切替テスト --- */
    api->kprintf(ATTR_WHITE, "[ctx] save_context / load_context ...\n");

    input_init(api);
    setup_bindings();

    /* 現在のバインディング数を確認 (setup_bindings: 11個) */
    {
        extern int g_inp_num_bindings;
        TEST("ctx: bindings after setup == 11",
             g_inp_num_bindings == 11);

        /* スロット0に保存 */
        ret = input_save_context(0);
        TEST("ctx: save_context(0) == 0", ret == 0);

        /* 全バインディング解除 */
        input_unbind_all();
        TEST("ctx: unbind_all -> 0 bindings",
             g_inp_num_bindings == 0);

        /* スロット0から復元 */
        ret = input_load_context(0);
        TEST("ctx: load_context(0) == 0", ret == 0);
        TEST("ctx: after load bindings == 11",
             g_inp_num_bindings == 11);

        /* 範囲外のスロット */
        ret = input_save_context(-1);
        TEST("ctx: save_context(-1) == -1", ret == -1);
        ret = input_save_context(INPUT_CTX_MAX);
        TEST("ctx: save_context(max) == -1", ret == -1);

        /* 未保存スロットからのload */
        ret = input_load_context(3);
        TEST("ctx: load_context(unused=3) == -1", ret == -1);
    }

    /* --- 別コンテキストの切替テスト --- */
    api->kprintf(ATTR_WHITE, "[ctx] context switching ...\n");
    {
        extern int g_inp_num_bindings;

        /* スロット1にメニュー用バインディングを保存 */
        input_unbind_all();
        input_bind(ACT_CONFIRM, INP_DEV_KEYBOARD, 0x34, FIX16_ONE);
        input_bind(ACT_CANCEL,  INP_DEV_KEYBOARD, 0x00, FIX16_ONE);
        TEST("ctx: menu bindings == 2", g_inp_num_bindings == 2);

        ret = input_save_context(1);
        TEST("ctx: save_context(1) == 0", ret == 0);

        /* スロット0 (ゲーム用) に戻す */
        ret = input_load_context(0);
        TEST("ctx: restored game bindings == 11",
             g_inp_num_bindings == 11);

        /* スロット1 (メニュー用) に切替 */
        ret = input_load_context(1);
        TEST("ctx: restored menu bindings == 2",
             g_inp_num_bindings == 2);
    }

    /* --- 複合キー (modifier_mask) テスト --- */
    api->kprintf(ATTR_WHITE, "[mod] modifier_mask ...\n");
    {
        extern InputBinding g_inp_bindings[];
        extern int g_inp_num_bindings;

        input_unbind_all();

        /* Ctrl+S のバインディング */
        ret = input_bind_ex(ACT_SAVE, INP_DEV_KEYBOARD, 0x1F,
                            FIX16_ONE, INP_MOD_CTRL);
        TEST("mod: bind_ex(Ctrl+S) == 0", ret == 0);
        TEST("mod: binding[0].modifier_mask == INP_MOD_CTRL",
             g_inp_bindings[0].modifier_mask == INP_MOD_CTRL);

        /* 修飾キーなしの通常バインディング */
        ret = input_bind(ACT_CONFIRM, INP_DEV_KEYBOARD, 0x34, FIX16_ONE);
        TEST("mod: bind(Space) == 0", ret == 0);
        TEST("mod: binding[1].modifier_mask == 0",
             g_inp_bindings[1].modifier_mask == 0);
    }

    /* --- キーコンフィグ保存・読み込みテスト --- */
    api->kprintf(ATTR_WHITE, "[cfg] save_config / load_config ...\n");
    {
        extern int g_inp_num_bindings;
        int loaded;

        /* テスト用バインディングを設定 */
        input_unbind_all();
        input_bind(ACT_CONFIRM, INP_DEV_KEYBOARD, 0x34, FIX16_ONE);
        input_bind(ACT_CANCEL,  INP_DEV_KEYBOARD, 0x00, FIX16_ONE);
        input_bind_ex(ACT_SAVE, INP_DEV_KEYBOARD, 0x1F,
                      FIX16_ONE, INP_MOD_CTRL);
        TEST("cfg: 3 bindings before save", g_inp_num_bindings == 3);

        /* 保存 */
        ret = input_save_config("/tmp/test_input.cfg");
        TEST("cfg: save_config == 0", ret == 0);

        /* 全解除してから読み込み */
        input_unbind_all();
        TEST("cfg: 0 bindings after unbind_all",
             g_inp_num_bindings == 0);

        loaded = input_load_config("/tmp/test_input.cfg");
        TEST("cfg: load_config == 3", loaded == 3);
        TEST("cfg: bindings restored == 3",
             g_inp_num_bindings == 3);

        /* 不正なファイルの読み込み */
        loaded = input_load_config("/tmp/nonexistent_file.cfg");
        TEST("cfg: load_config(nonexistent) == -1", loaded == -1);

        /* NULLパスのテスト */
        ret = input_save_config(NULL);
        TEST("cfg: save_config(NULL) == -1", ret == -1);
        loaded = input_load_config(NULL);
        TEST("cfg: load_config(NULL) == -1", loaded == -1);
    }

    /* --- 修飾キー定数の整合性テスト --- */
    api->kprintf(ATTR_WHITE, "[const] modifier constants ...\n");
    {
        TEST("const: INP_MOD_SHIFT == 0x01", INP_MOD_SHIFT == 0x01);
        TEST("const: INP_MOD_CAPS  == 0x02", INP_MOD_CAPS  == 0x02);
        TEST("const: INP_MOD_KANA  == 0x04", INP_MOD_KANA  == 0x04);
        TEST("const: INP_MOD_GRPH  == 0x08", INP_MOD_GRPH  == 0x08);
        TEST("const: INP_MOD_CTRL  == 0x10", INP_MOD_CTRL  == 0x10);
    }

    input_shutdown();

    api->kprintf(ATTR_WHITE, "\n");
}

/* ====================================================================== */
/*  メイン                                                                   */
/* ====================================================================== */

int main(int argc, char **argv, KernelAPI *_api)
{
    api = _api;
    (void)argc; (void)argv;

    api->kprintf(ATTR_WHITE, "=== libos32input test (P1+P2) ===\n");

    pass_count = 0;
    fail_count = 0;

    run_unit_tests();

    return os32_test_summary(api, "input_test",
                             pass_count, pass_count + fail_count);
}
