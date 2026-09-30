/* ======================================================================== */
/*  TEE.C -- 分岐出力                                                        */
/*                                                                          */
/*  Usage: tee FILE                                                          */
/*  stdin を stdout と FILE の両方に書き出す                                  */
/*                                                                          */
/*  ストリームループで EOF/エラーまで読み取りを繰り返し、ブロックごとに       */
/*  stdout とファイルへリアルタイムに書き出す。パイプライン対応。             */
/* ======================================================================== */
#include "os32api.h"
#include <string.h>
#include <stdio.h>

static KernelAPI *api;

/* 読み取りバッファ (コンソール入力時のブロックを緩和するため縮小) */
#define TEE_BUF_SIZE 4096

int main(int argc, char **argv, KernelAPI *kapi)
{
    static char buf[TEE_BUF_SIZE];
    int sz, fd;
    api = kapi;

    if (argc < 2) {
        printf("Usage: tee FILE\n");
        return 1;
    }

    /* 出力ファイルを先に開く */
    fd = api->sys_open(argv[1], KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) {
        fprintf(stderr, "tee: cannot open %s\n", argv[1]);
        return 1;
    }

    /* stdin からストリームループで読み取り (EOF/エラーまで繰り返す) */
    while ((sz = api->sys_read(0, buf, TEE_BUF_SIZE)) > 0) {
        /* stdout に書き出し */
        api->sys_write(1, buf, sz);
        /* ファイルにも書き出し */
        api->sys_write(fd, buf, sz);
    }

    api->sys_close(fd);
    return 0;
}
