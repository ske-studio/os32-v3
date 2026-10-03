/* ======================================================================== */
/*  FONT_LOAD_TEST.C — kcg_load_font KAPI テスト                            */
/*                                                                          */
/*  boot後の読込がNOSYSで拒否されることを確認する。                          */
/*                                                                          */
/*  合否の出し方は票 docs/archive/test/TASK_TEST_RESULT.md §2 に従う。         */
/*  **kcg_load_font の戻り値をそのまま終了コードにしない** — 失敗は負値で、   */
/*  シェルの予約値 (126/127/130/139) と衝突しうる。フォントが無いのは        */
/*  「不合格」ではなく「実行しなかった」なので SKIP (終了コード 2)。          */
/* ======================================================================== */

#include "os32api.h"
#include "rt/testresult.h"
#include <stdio.h>

int main(int argc, char **argv, KernelAPI *api)
{
    const char *path;
    OS32_Stat   st;
    int         ret;

    if (argc > 1) {
        path = argv[1];
    } else {
        path = "/sys/font/default.kcgfont";
    }

    printf("kcg_load_font test\n");
    printf("path: %s\n", path);

    if (api->sys_stat(path, &st) != 0) {
        return os32_test_summary_skip(api, "font_load_test",
                                      "font file not found (NOSYS test prerequisite)");
    }

    ret = api->kcg_load_font(path);
    printf("result: %d\n", ret);

    return os32_test_summary(api, "font_load_test", (ret == OS32_ERR_NOSYS) ? 1 : 0, 1);
}
