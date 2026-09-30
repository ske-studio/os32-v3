/* ========================================================================
 *  T_EXIT.C — 終了コードを返すだけの小さな試験バイナリ
 *
 *  票: docs/archive/shell/TASK_EXIT_STATUS.md §4-2 (ゲスト受入)
 *
 *  使い方:
 *    t_exit            0 で終わる
 *    t_exit N          N で終わる (負も書ける: `t_exit -1`)
 *    t_exit N FILE     終わる前に FILE へ `r` を 1 文字足してから N で終わる
 *
 *  FILE は **実行回数を数える**ための窓 (受入 S2b)。`/sbin` に 1 本だけ置いて
 *  `t_exit -1 /tmp/c` を 1 回打ったとき、ファイルが 1 バイトしか増えないことを
 *  確かめる — 2 バイト増えたら、シェルが「子の -1」を「見つからない」と読んで
 *  PATH の次の候補へ進んでいる (票 §1 の実害 1)。
 *
 *  KAPI に追記モードが無いので **読んでから書き直す**。上限 (COUNT_MAX) を
 *  超えたら足さない — 黙って切り詰めて「数えたつもり」になるのを避ける。
 *
 *  main() が **ファイルの最初の関数** であること (OS32X の約束)。
 * ======================================================================== */
#include "os32api.h"

#define COUNT_MAX 512

static int parse_int(const char *s);
static int bump_count(KernelAPI *api, const char *path);

int main(int argc, char **argv, KernelAPI *api)
{
    int code = 0;

    if (argc > 1) code = parse_int(argv[1]);

    if (argc > 2) {
        if (bump_count(api, argv[2]) < 0) {
            api->kprintf(0x0C, "t_exit: cannot count in %s\n", argv[2]);
        }
    }

    api->kprintf(0x07, "t_exit: exiting with %d\n", code);
    return code;
}

static int parse_int(const char *s)
{
    int v = 0;
    int neg = 0;

    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* 0 = 1 バイト足した / -1 = 足せなかった (開けない・上限) */
static int bump_count(KernelAPI *api, const char *path)
{
    static char buf[COUNT_MAX + 1];
    int fd, n = 0;

    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd >= 0) {
        n = api->sys_read(fd, buf, COUNT_MAX);
        api->sys_close(fd);
        if (n < 0) n = 0;
    }
    if (n >= COUNT_MAX) return -1;      /* 切り詰めて数えたふりをしない */
    buf[n++] = 'r';

    fd = api->sys_open(path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) return -1;
    if (api->sys_write(fd, buf, (u32)n) != n) {
        api->sys_close(fd);
        return -1;
    }
    api->sys_close(fd);
    return 0;
}
