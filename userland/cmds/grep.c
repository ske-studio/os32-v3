/* ======================================================================== */
/*  GREP.C -- パターンマッチフィルタ                                          */
/*                                                                          */
/*  Usage: grep [-invc] PATTERN [FILE...]                                    */
/*  オプション:                                                              */
/*    -i  大文字小文字を区別しない                                            */
/*    -n  行番号を表示                                                        */
/*    -v  マッチしなかった行を表示 (反転)                                     */
/*    -c  マッチ行数のみ表示                                                  */
/*  stdin からも読み取り可能 (パイプ対応)                                     */
/* ======================================================================== */
#include "os32api.h"
#include <string.h>
#include <stdio.h>

static KernelAPI *api;
static int match_count = 0;

/* オプションフラグ */
static int opt_ignore_case = 0;
static int opt_line_number = 0;
static int opt_invert = 0;
static int opt_count_only = 0;

/* 1文字を小文字に変換 */
static char to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

/* 簡易部分文字列検索 (大文字小文字無視対応) */
static int str_contains(const char *haystack, const char *needle)
{
    int hlen, nlen, i, j;
    if (!needle[0]) return 1;
    hlen = strlen(haystack);
    nlen = strlen(needle);
    if (nlen > hlen) return 0;
    for (i = 0; i <= hlen - nlen; i++) {
        int ok = 1;
        for (j = 0; j < nlen; j++) {
            char h = haystack[i + j];
            char n = needle[j];
            if (opt_ignore_case) {
                h = to_lower(h);
                n = to_lower(n);
            }
            if (h != n) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

/* バッファ内の各行を検索してマッチした行を出力 */
static void grep_buffer(const char *buf, int len, const char *pattern,
                         const char *filename, int show_filename)
{
    char line[1024];
    int li = 0;
    int i;
    int line_num = 0;
    int local_count = 0;

    for (i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            int matched;
            line[li] = '\0';
            line_num++;
            matched = str_contains(line, pattern);

            /* -v: マッチを反転 */
            if (opt_invert) matched = !matched;

            if (matched) {
                local_count++;
                match_count++;
                if (!opt_count_only) {
                    if (show_filename) printf("%s:", filename);
                    if (opt_line_number) printf("%d:", line_num);
                    printf("%s\n", line);
                }
            }
            li = 0;
        } else if (li < 1022) {
            line[li++] = buf[i];
        }
    }

    /* -c: ファイルごとのマッチ数を表示 */
    if (opt_count_only) {
        if (show_filename) printf("%s:", filename);
        printf("%d\n", local_count);
    }
}

/* ファイルを読み取って grep */
static void grep_file(const char *path, const char *pattern, int show_filename)
{
    static char buf[65536];
    int fd, sz;

    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        printf("grep: %s: No such file\n", path);
        return;
    }
    sz = api->sys_read(fd, buf, sizeof(buf) - 1);
    api->sys_close(fd);
    if (sz <= 0) return;
    buf[sz] = '\0';
    grep_buffer(buf, sz, pattern, path, show_filename);
}

/* stdin から読み取って grep */
static void grep_stdin(const char *pattern)
{
    static char buf[65536];
    int sz;

    sz = api->sys_read(0, buf, sizeof(buf) - 1);
    if (sz <= 0) return;
    buf[sz] = '\0';
    grep_buffer(buf, sz, pattern, "(stdin)", 0);
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    int i;
    int pattern_idx = -1;
    int file_start = -1;
    int file_count;

    api = kapi;

    /* オプション解析 */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            /* オプション文字列を1文字ずつ解析 (-inv 等の結合に対応) */
            int j;
            for (j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                case 'i': opt_ignore_case = 1; break;
                case 'n': opt_line_number = 1; break;
                case 'v': opt_invert = 1; break;
                case 'c': opt_count_only = 1; break;
                default:
                    printf("grep: unknown option '-%c'\n", argv[i][j]);
                    return 2;
                }
            }
        } else {
            /* 最初の非オプション引数がパターン */
            if (pattern_idx < 0) {
                pattern_idx = i;
            } else if (file_start < 0) {
                file_start = i;
            }
        }
    }

    if (pattern_idx < 0) {
        printf("Usage: grep [-invc] PATTERN [FILE...]\n");
        return 2;
    }

    if (file_start < 0) {
        /* パターンのみ: stdin から読む */
        if (api->sys_isatty(0)) {
            printf("Usage: grep [-invc] PATTERN [FILE...]\n");
            return 2;
        }
        grep_stdin(argv[pattern_idx]);
    } else {
        /* ファイル数をカウント */
        file_count = 0;
        for (i = file_start; i < argc; i++) {
            if (argv[i][0] != '-') file_count++;
        }

        for (i = file_start; i < argc; i++) {
            if (argv[i][0] == '-') continue; /* 遅延オプションをスキップ */
            grep_file(argv[i], argv[pattern_idx], (file_count > 1));
        }
    }

    return (match_count > 0) ? 0 : 1;
}
