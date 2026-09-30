/* ======================================================================== */
/*  HEXDUMP.C -- 16進ダンプ表示                                               */
/*                                                                          */
/*  Usage: hexdump [-n COUNT] [FILE]                                         */
/*  標準的な16進ダンプ形式でファイル内容を表示する。                             */
/*  stdin からも読み取り可能 (パイプ対応)                                      */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>
#include <string.h>

static KernelAPI *api;

static void dump_buffer(const unsigned char *buf, int len)
{
    int i, j;
    int offset = 0;

    while (offset < len) {
        /* オフセット表示 */
        printf("%08x  ", offset);

        /* 16進数表示 */
        for (j = 0; j < 16; j++) {
            if (offset + j < len) {
                printf("%02x ", buf[offset + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }

        /* ASCII表示 */
        printf(" |");
        for (j = 0; j < 16; j++) {
            if (offset + j < len) {
                unsigned char c = buf[offset + j];
                if (c >= 0x20 && c <= 0x7E) {
                    printf("%c", c);
                } else {
                    printf(".");
                }
            }
        }
        printf("|\n");

        offset += 16;
    }

    /* 終端のオフセット */
    printf("%08x\n", len);
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    static unsigned char buf[65536];
    int max_bytes = -1;
    int fd;
    int sz;
    int i;
    const char *filepath = NULL;

    api = kapi;

    /* オプション解析 */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'n' && argv[i][2] == '\0') {
                /* -n COUNT */
                if (i + 1 < argc) {
                    const char *p;
                    i++;
                    max_bytes = 0;
                    p = argv[i];
                    while (*p >= '0' && *p <= '9') {
                        max_bytes = max_bytes * 10 + (*p - '0');
                        p++;
                    }
                }
            } else {
                printf("hexdump: unknown option '%s'\n", argv[i]);
                return 1;
            }
        } else {
            filepath = argv[i];
        }
    }

    if (filepath) {
        fd = api->sys_open(filepath, KAPI_O_RDONLY);
        if (fd < 0) {
            printf("hexdump: %s: No such file\n", filepath);
            return 1;
        }
    } else {
        /* stdin */
        if (api->sys_isatty(0)) {
            printf("Usage: hexdump [-n COUNT] [FILE]\n");
            return 1;
        }
        fd = 0;
    }

    sz = api->sys_read(fd, buf, sizeof(buf));
    if (filepath) {
        api->sys_close(fd);
    }

    if (sz <= 0) {
        return 0;
    }

    if (max_bytes >= 0 && max_bytes < sz) {
        sz = max_bytes;
    }

    dump_buffer(buf, sz);

    return 0;
}
