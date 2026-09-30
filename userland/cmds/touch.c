/* ======================================================================== */
/*  TOUCH.C -- 空ファイル作成                                                 */
/*                                                                          */
/*  Usage: touch FILE...                                                     */
/*  ファイルが存在しなければ空ファイルを作成する。                               */
/*  ファイルが既に存在する場合は何もしない。                                    */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>

int main(int argc, char **argv, KernelAPI *api)
{
    int i;
    int errors = 0;

    if (argc < 2) {
        printf("Usage: touch FILE...\n");
        return 1;
    }

    for (i = 1; i < argc; i++) {
        OS32_Stat st;
        int ret;

        /* ファイルが既に存在するか確認 */
        ret = api->sys_stat(argv[i], &st);
        if (ret == 0) {
            /* 既存ファイル: 何もしない */
            continue;
        }

        /* ファイルを作成して即座に閉じる */
        {
            int fd = api->sys_open(argv[i], KAPI_O_WRONLY | KAPI_O_CREAT);
            if (fd < 0) {
                printf("touch: cannot create '%s'\n", argv[i]);
                errors++;
            } else {
                api->sys_close(fd);
            }
        }
    }

    return errors ? 1 : 0;
}
