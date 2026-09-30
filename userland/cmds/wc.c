/* ======================================================================== */
/*  WC.C -- 行・単語・バイトカウンタ                                          */
/*                                                                          */
/*  Usage: wc [-l|-w|-c] [FILE...]                                           */
/*  stdin からも読み取り可能 (パイプ対応)                                     */
/* ======================================================================== */
#include "os32api.h"
#include <string.h>
#include <stdio.h>

static KernelAPI *api;

static void count_buffer(const char *buf, int len, int *lines, int *words, int *bytes)
{
    int i, in_word = 0;
    *lines = 0;
    *words = 0;
    *bytes = len;

    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') (*lines)++;
        if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r') {
            in_word = 0;
        } else {
            if (!in_word) { (*words)++; in_word = 1; }
        }
    }
}

static void wc_file(const char *path, int show_l, int show_w, int show_c)
{
    static char buf[65536];
    int fd, sz;
    int lines, words, bytes;

    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        printf("wc: %s: No such file\n", path);
        return;
    }
    sz = api->sys_read(fd, buf, sizeof(buf));
    api->sys_close(fd);
    if (sz <= 0) { printf("      0 %s\n", path); return; }

    count_buffer(buf, sz, &lines, &words, &bytes);

    if (show_l) printf(" %d", lines);
    if (show_w) printf(" %d", words);
    if (show_c) printf(" %d", bytes);
    printf(" %s\n", path);
}

static void wc_stdin(int show_l, int show_w, int show_c)
{
    static char buf[65536];
    int sz;
    int lines, words, bytes;

    sz = api->sys_read(0, buf, sizeof(buf));
    if (sz <= 0) { printf("      0\n"); return; }

    count_buffer(buf, sz, &lines, &words, &bytes);

    if (show_l) printf(" %d", lines);
    if (show_w) printf(" %d", words);
    if (show_c) printf(" %d", bytes);
    printf("\n");
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    int show_l = 0, show_w = 0, show_c = 0;
    int i, file_start = 1;
    api = kapi;

    /* オプション解析 */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            int j;
            for (j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'l') show_l = 1;
                else if (argv[i][j] == 'w') show_w = 1;
                else if (argv[i][j] == 'c') show_c = 1;
            }
            file_start = i + 1;
        } else {
            break;
        }
    }

    /* デフォルト: 全表示 */
    if (!show_l && !show_w && !show_c) {
        show_l = show_w = show_c = 1;
    }

    if (file_start >= argc) {
        /* ファイル引数なし: stdin から読む */
        wc_stdin(show_l, show_w, show_c);
    } else {
        for (i = file_start; i < argc; i++) {
            wc_file(argv[i], show_l, show_w, show_c);
        }
    }

    return 0;
}
