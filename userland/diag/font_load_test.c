/* ======================================================================== */
/*  FONT_LOAD_TEST.C — kcg_load_font KAPI テスト                            */
/*                                                                          */
/*  旧slotはファイルの有無に関わらずNOSYSを返す。                           */
/*                                                                          */
/*  合否の出し方は票 docs/archive/test/TASK_TEST_RESULT.md §2 に従う。         */
/*  **kcg_load_font の戻り値をそのまま終了コードにしない** — 失敗は負値で、   */
/*  シェルの予約値と衝突しないよう、合否は0/1へ正規化する。                */
/* ======================================================================== */

#include "os32api.h"
#include "rt/testresult.h"
#include <stdio.h>

int main(int argc, char **argv, KernelAPI *api)
{
    const char *path;
    int         ret;

    if (argc > 1) {
        path = argv[1];
    } else {
        path = "/sys/font/default.kcgfont";
    }

    printf("kcg_load_font test\n");
    printf("path: %s\n", path);

    ret = api->kcg_load_font(path);
    printf("result: %d\n", ret);

    return os32_test_summary(api, "font_load_test", (ret == OS32_ERR_NOSYS) ? 1 : 0, 1);
}
