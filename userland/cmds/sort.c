/* ======================================================================== */
/*  SORT.C -- 行ソートフィルタ                                                */
/*                                                                          */
/*  Usage: sort [-r] [-n] [FILE...]                                          */
/*  stdin またはファイルから全行を読み込み、ソートして出力する。                 */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>
#include <string.h>

static KernelAPI *api;
static int opt_reverse = 0;
static int opt_numeric = 0;

/* 文字列→整数変換 (符号付き) */
static long str_to_long(const char *s)
{
    long val = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

/* 行比較関数 */
static int compare_lines(const char *a, const char *b)
{
    int cmp;
    if (opt_numeric) {
        long va = str_to_long(a);
        long vb = str_to_long(b);
        if (va < vb) cmp = -1;
        else if (va > vb) cmp = 1;
        else cmp = 0;
    } else {
        cmp = strcmp(a, b);
    }
    return opt_reverse ? -cmp : cmp;
}

/* バッファ内の行を分割してポインタ配列に格納 */
#define MAX_LINES 4096
#define BUF_SIZE  65536

static char buf[BUF_SIZE];
static char *lines[MAX_LINES];
static int line_count = 0;

static void parse_lines(int len)
{
    int i;
    int start = 0;
    line_count = 0;

    for (i = 0; i < len && line_count < MAX_LINES; i++) {
        if (buf[i] == '\n') {
            buf[i] = '\0';
            lines[line_count++] = &buf[start];
            start = i + 1;
        }
    }
    /* 最終行 (改行なし) */
    if (start < len && line_count < MAX_LINES) {
        buf[len] = '\0';
        lines[line_count++] = &buf[start];
    }
}

/* シェルソート */
static void shell_sort(void)
{
    int gap, i, j;
    char *tmp;
    for (gap = line_count / 2; gap > 0; gap /= 2) {
        for (i = gap; i < line_count; i++) {
            tmp = lines[i];
            for (j = i; j >= gap && compare_lines(lines[j - gap], tmp) > 0; j -= gap) {
                lines[j] = lines[j - gap];
            }
            lines[j] = tmp;
        }
    }
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    int i;
    int sz = 0;
    int has_file = 0;

    api = kapi;

    /* 引数なし: Usage表示して終了 (stdinブロック防止) */
    if (argc < 2) {
        printf("Usage: sort [-rn] FILE\n");
        return 1;
    }

    /* オプション解析 */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            int j;
            for (j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                case 'r': opt_reverse = 1; break;
                case 'n': opt_numeric = 1; break;
                default:
                    printf("sort: unknown option '-%c'\n", argv[i][j]);
                    return 1;
                }
            }
        }
    }

    /* ファイルまたはstdinから読み込み */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            int fd = api->sys_open(argv[i], KAPI_O_RDONLY);
            if (fd < 0) {
                printf("sort: %s: No such file\n", argv[i]);
                return 1;
            }
            sz = api->sys_read(fd, buf, BUF_SIZE - 1);
            api->sys_close(fd);
            has_file = 1;
            break;
        }
    }

    if (!has_file) {
        /* ファイル引数なし (argc<2でUsage終了済みだがオプションのみの場合) */
        printf("Usage: sort [-rn] FILE\n");
        return 1;
    }

    /* データがなければ何もせず終了 */
    if (sz <= 0) return 0;

    parse_lines(sz);
    shell_sort();

    for (i = 0; i < line_count; i++) {
        printf("%s\n", lines[i]);
    }

    return 0;
}

