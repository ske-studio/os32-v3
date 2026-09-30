/* ======================================================================== */
/*  TAIL.C -- 末尾N行表示                                                    */
/*                                                                          */
/*  Usage: tail [-n N] [FILE...]                                             */
/*  stdin からも読み取り可能 (パイプ対応)                                     */
/*                                                                          */
/*  ファイル・stdin 共にストリームループで EOF まで全データを蓄積し、          */
/*  末尾N行を出力する。64KB超のファイルにも対応。                             */
/* ======================================================================== */
#include "os32api.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static KernelAPI *api;

/* 読み取りバッファサイズ (ファイルストリーム用チャンク) */
#define TAIL_READ_SIZE 4096
/* 蓄積バッファサイズ */
#define TAIL_BUF_SIZE  65536

/* ------------------------------------------------------------------ */
/*  バッファ内容から末尾 N 行を出力                                     */
/* ------------------------------------------------------------------ */
static void tail_buffer(const char *buf, int len, int max_lines)
{
    int i, total_lines = 0;
    int skip_lines, line_count = 0;

    /* まず総行数をカウント */
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') total_lines++;
    }
    /* 最後の文字が改行でない場合も1行とカウント */
    if (len > 0 && buf[len - 1] != '\n') total_lines++;

    skip_lines = total_lines - max_lines;
    if (skip_lines < 0) skip_lines = 0;

    /* skip_lines行スキップしてから出力 */
    for (i = 0; i < len; i++) {
        if (line_count >= skip_lines) {
            putchar(buf[i]);
        }
        if (buf[i] == '\n') line_count++;
    }
}

/* ------------------------------------------------------------------ */
/*  ファイル対象の tail (ストリームループで64KB超にも対応)               */
/* ------------------------------------------------------------------ */
static void tail_file(const char *path, int max_lines)
{
    static char buf[TAIL_BUF_SIZE];
    int fd, sz, total = 0;

    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        printf("tail: %s: No such file\n", path);
        return;
    }

    /* ストリームループでバッファに蓄積 */
    while (total < TAIL_BUF_SIZE) {
        int chunk;
        chunk = TAIL_BUF_SIZE - total;
        if (chunk > TAIL_READ_SIZE) chunk = TAIL_READ_SIZE;
        sz = api->sys_read(fd, buf + total, chunk);
        if (sz <= 0) break;
        total += sz;
    }
    api->sys_close(fd);

    if (total > 0) tail_buffer(buf, total, max_lines);
}

/* ------------------------------------------------------------------ */
/*  stdin 対象の tail (ストリームループで EOF まで蓄積)                  */
/* ------------------------------------------------------------------ */
static void tail_stdin(int max_lines)
{
    static char buf[TAIL_BUF_SIZE];
    int sz, total = 0;

    /* ストリームループで蓄積 (tail_file と同様) */
    while (total < TAIL_BUF_SIZE) {
        int chunk;
        chunk = TAIL_BUF_SIZE - total;
        if (chunk > TAIL_READ_SIZE) chunk = TAIL_READ_SIZE;
        sz = api->sys_read(0, buf + total, chunk);
        if (sz <= 0) break;
        total += sz;
    }
    if (total > 0) tail_buffer(buf, total, max_lines);
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
        tail_stdin(max_lines);
    } else {
        for (i = file_start; i < argc; i++) {
            if (argc - file_start > 1) printf("==> %s <==\n", argv[i]);
            tail_file(argv[i], max_lines);
        }
    }

    return 0;
}
