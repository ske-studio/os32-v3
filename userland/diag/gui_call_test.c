/* ======================================================================== */
/*  gui_call_test — GUI v1.1 背骨 (KAPI v41) の入口テスト                    */
/*                                                                          */
/*  票 TASK_K1_gui_call.md の完了条件のうち、アプリ (CPL=3, owner 2) から     */
/*  観測できる部分を検証する:                                                */
/*    - WM 未登録のとき gui_call は OS32_ERR_NOSYS (-10) を返す。            */
/*    - アプリ (owner 2) からの gui_register は OS32_ERR_INVAL (-9)。         */
/*      (登録は shell 帯 owner 1 からのみ)                                    */
/*                                                                          */
/*  ダミーハンドラ dummy_handler は op をそのまま返す。shell 帯から           */
/*  gui_register した後の転送検証 (gui_call(5,7)->5, owner=2) に使う。         */
/*  (その全経路は WM 実装 W1 と結合してから検証する)                          */
/* ======================================================================== */

#include "os32api.h"
#include "os32_gui_shared.h"
#include "rt/testresult.h"

/* main() は必ず先頭。ヘルパは後ろに前方宣言 (SDK 規約)。 */
static i32 dummy_handler(u32 op, u32 arg, int owner);

int main(int argc, char **argv, KernelAPI *api)
{
    i32 r;
    int pass = 0;
    int fail = 0;

    (void)argc;
    (void)argv;

    api->kprintf(0x0E, "gui_call_test: KAPI v41 GUI backbone\n");

    /* (1) 未登録の gui_call は OS32_ERR_NOSYS (-10) */
    r = api->gui_call(GUI_OP_INIT, 0);
    if (r == OS32_ERR_NOSYS) {
        pass++;
        api->kprintf(0x0A, "[PASS] gui_call(INIT) unregistered -> %d\n", (int)r);
    } else {
        fail++;
        api->kprintf(0x4F, "[FAIL] gui_call(INIT) -> %d (want %d)\n",
                     (int)r, OS32_ERR_NOSYS);
    }

    /* (2) アプリ (owner 2) からの gui_register は OS32_ERR_INVAL (-9) */
    r = api->gui_register((void *)dummy_handler, (void *)0);
    if (r == OS32_ERR_INVAL) {
        pass++;
        api->kprintf(0x0A, "[PASS] gui_register from app (owner 2) -> %d\n",
                     (int)r);
    } else {
        fail++;
        api->kprintf(0x4F, "[FAIL] gui_register from app -> %d (want %d)\n",
                     (int)r, OS32_ERR_INVAL);
    }

    return os32_test_summary(api, "gui_call_test", pass, pass + fail);
}

/* op をそのまま返すダミー WM ハンドラ (契約 T4 の C 署名)。 */
static i32 dummy_handler(u32 op, u32 arg, int owner)
{
    (void)arg;
    (void)owner;
    return (i32)op;
}
