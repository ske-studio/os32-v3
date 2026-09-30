/* ======================================================================== */
/*  E2SEEK.C - ext2 大オフセット読み取り検証                                 */
/*                                                                          */
/*  指定ファイルの各境界オフセット (直接/間接/二重間接) から 16 バイト読み、  */
/*  16進で表示する。ホスト側の同ファイルと突き合わせて破損箇所を特定する。   */
/* ======================================================================== */

#include "os32api.h"
#include <stdio.h>

/* 1KB ブロック ext2 の境界: 直接 12KB / 単間接 268KB / それ以降 二重間接 */
static const int offsets[] = {
    0,          /* 直接 */
    12 * 1024,      /* 単間接の先頭 */
    100 * 1024,     /* 単間接の中間 */
    268 * 1024 - 16,/* 単間接の末尾 */
    268 * 1024,     /* 二重間接の先頭 */
    300 * 1024,     /* 二重間接 */
    1024 * 1024,    /* 1MB */
    5 * 1024 * 1024,/* 5MB */
    -1
};

int main(int argc, char **argv, KernelAPI *api)
{
    unsigned char buf[16];
    int fd, i, j, pos, sz;
    const char *path;

    if (argc < 2) {
        printf("Usage: e2seek FILE\n");
        return 1;
    }
    path = argv[1];

    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        printf("open failed: %s (%d)\n", path, fd);
        return 1;
    }

    for (i = 0; offsets[i] >= 0; i++) {
        pos = api->sys_lseek(fd, offsets[i], SEEK_SET);
        sz = api->sys_read(fd, buf, 16);
        printf("off=%d seek=%d read=%d: ", offsets[i], pos, sz);
        for (j = 0; j < sz && j < 16; j++) {
            printf("%02x", buf[j]);
        }
        printf("\n");
    }

    api->sys_close(fd);
    return 0;
}
