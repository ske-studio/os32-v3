/* ======================================================================== */
/*  DIFF.C -- 簡易ファイル比較                                                */
/*                                                                          */
/*  Usage: diff FILE1 FILE2                                                  */
/*  2つのファイルを行単位で比較し、差分を表示する。                             */
/*  簡易実装: 行ハッシュによる逐次比較方式。                                   */
/* ======================================================================== */
#include "os32api.h"
#include <stdio.h>
#include <string.h>

static KernelAPI *api;

#define MAX_LINES 2048
#define BUF_SIZE 65536

static char buf1[BUF_SIZE];
static char buf2[BUF_SIZE];
static char *lines1[MAX_LINES];
static char *lines2[MAX_LINES];
static int count1, count2;

/* バッファを行に分割 */
static int split_lines(char *buf, int len, char **lines, int max_lines)
{
    int count = 0;
    int start = 0;
    int i;

    for (i = 0; i < len && count < max_lines; i++) {
        if (buf[i] == '\n') {
            buf[i] = '\0';
            lines[count++] = &buf[start];
            start = i + 1;
        }
    }
    if (start < len && count < max_lines) {
        buf[len] = '\0';
        lines[count++] = &buf[start];
    }
    return count;
}

/* ファイルを読み込み */
static int read_file(const char *path, char *buf, int buf_size)
{
    int fd, sz;
    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        printf("diff: %s: No such file\n", path);
        return -1;
    }
    sz = api->sys_read(fd, buf, buf_size - 1);
    api->sys_close(fd);
    if (sz < 0) sz = 0;
    return sz;
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    int sz1, sz2;
    int i, j;
    int differences = 0;

    api = kapi;

    if (argc < 3) {
        printf("Usage: diff FILE1 FILE2\n");
        return 2;
    }

    sz1 = read_file(argv[1], buf1, BUF_SIZE);
    if (sz1 < 0) return 2;
    sz2 = read_file(argv[2], buf2, BUF_SIZE);
    if (sz2 < 0) return 2;

    count1 = split_lines(buf1, sz1, lines1, MAX_LINES);
    count2 = split_lines(buf2, sz2, lines2, MAX_LINES);

    /* 簡易逐次比較 */
    i = 0;
    j = 0;
    while (i < count1 || j < count2) {
        if (i < count1 && j < count2) {
            if (strcmp(lines1[i], lines2[j]) == 0) {
                /* 同じ行 */
                i++;
                j++;
            } else {
                /* 異なる行 — 前後で同期を試みる */
                int found = 0;
                int k;

                /* file2で次に一致する行を探す (最大10行先) */
                for (k = j + 1; k < count2 && k < j + 10; k++) {
                    if (strcmp(lines1[i], lines2[k]) == 0) {
                        /* file2に追加された行 */
                        while (j < k) {
                            printf("+%s\n", lines2[j]);
                            j++;
                            differences++;
                        }
                        found = 1;
                        break;
                    }
                }

                if (!found) {
                    /* file1で次に一致する行を探す */
                    for (k = i + 1; k < count1 && k < i + 10; k++) {
                        if (j < count2 && strcmp(lines1[k], lines2[j]) == 0) {
                            /* file1から削除された行 */
                            while (i < k) {
                                printf("-%s\n", lines1[i]);
                                i++;
                                differences++;
                            }
                            found = 1;
                            break;
                        }
                    }
                }

                if (!found) {
                    /* 変更された行 */
                    printf("-%s\n", lines1[i]);
                    printf("+%s\n", lines2[j]);
                    i++;
                    j++;
                    differences++;
                }
            }
        } else if (i < count1) {
            printf("-%s\n", lines1[i]);
            i++;
            differences++;
        } else {
            printf("+%s\n", lines2[j]);
            j++;
            differences++;
        }
    }

    return differences > 0 ? 1 : 0;
}
