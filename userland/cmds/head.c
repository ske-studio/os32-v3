/* ======================================================================== */
/*  HEAD.C -- 先頭N行表示                                                    */
/*                                                                          */
/*  Usage: head [-n N] [FILE...]                                             */
/*  stdin からも読み取り可能 (パイプ対応)                                     */
/* ======================================================================== */
#include "os32api.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static KernelAPI *api;

static void head_buffer(const char *buf, int len, int max_lines)
{
    int i, line_count = 0;
    for (i = 0; i < len && line_count < max_lines; i++) {
        putchar(buf[i]);
        if (buf[i] == '\n') line_count++;
    }
}

static void head_file(const char *path, int max_lines)
{
    static char buf[65536];
    int fd, sz;

    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        printf("head: %s: No such file\n", path);
        return;
    }
    sz = api->sys_read(fd, buf, sizeof(buf));
    api->sys_close(fd);
    if (sz > 0) head_buffer(buf, sz, max_lines);
}

static void head_stdin(int max_lines)
{
    static char buf[65536];
    int sz;
    sz = api->sys_read(0, buf, sizeof(buf));
    if (sz > 0) head_buffer(buf, sz, max_lines);
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    int max_lines = 10;
    int i, file_start = 1;
    api = kapi;

    /* オプション解析 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_lines = atoi(argv[++i]);
            if (max_lines <= 0) max_lines = 10;
            file_start = i + 1;
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            max_lines = atoi(&argv[i][1]);
            if (max_lines <= 0) max_lines = 10;
            file_start = i + 1;
        } else {
            file_start = i;
            break;
        }
    }

    if (file_start >= argc) {
        head_stdin(max_lines);
    } else {
        for (i = file_start; i < argc; i++) {
            if (argc - file_start > 1) printf("==> %s <==\n", argv[i]);
            head_file(argv[i], max_lines);
        }
    }

    return 0;
}
