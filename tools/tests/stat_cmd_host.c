/* ========================================================================= */
/*  STAT_CMD_HOST.C — `stat` コマンド (userland/cmds/stat.c) のホスト TDD     */
/*                                                                           */
/*  実物の userland/cmds/stat.c をそのまま取り込み (main だけ改名)、KAPI の    */
/*  sys_stat だけを表に差し替える。ホストのファイルシステムには触らない。      */
/*  出力は printf を捕まえて文字列で突き合わせる。                             */
/*                                                                           */
/*  見るもの: st_dev の復号 (hd0 / fd0 / 未知の種別 / 0 = unknown)、種別の     */
/*  名前、NOTFOUND の終了コード 1、複数引数で 1 つ落ちても残りを続けること。    */
/*                                                                           */
/*  実行: python3 -B tools/tests/test_stat_cmd.py                            */
/* ========================================================================= */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "os32api.h"

/* ---- 出力の捕捉 -------------------------------------------------------- */
static char cap_out[8192];
static int cap_len;

static int cap_printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(cap_out + cap_len, sizeof(cap_out) - (size_t)cap_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        cap_len += n;
        if (cap_len > (int)sizeof(cap_out) - 1) cap_len = (int)sizeof(cap_out) - 1;
    }
    return n;
}

/* ---- sys_stat の模型 --------------------------------------------------- */
typedef struct {
    const char *path;
    int         rc;         /* != 0 なら st は使わず rc を返す */
    OS32_Stat   st;
} FakeEntry;

#define FAKE_MAX 8
static FakeEntry fake[FAKE_MAX];
static int fake_n;

static void fake_reset(void)
{
    fake_n = 0;
    cap_len = 0;
    cap_out[0] = '\0';
}

static void fake_err(const char *path, int rc)
{
    fake[fake_n].path = path;
    fake[fake_n].rc = rc;
    memset(&fake[fake_n].st, 0, sizeof(OS32_Stat));
    fake_n++;
}

static void fake_ok(const char *path, u16 mode, u32 dev, u32 ino, u32 size)
{
    OS32_Stat *st = &fake[fake_n].st;
    fake[fake_n].path = path;
    fake[fake_n].rc = 0;
    memset(st, 0, sizeof(OS32_Stat));
    st->st_mode = mode;
    st->st_dev = dev;
    st->st_ino = ino;
    st->st_size = size;
    st->st_nlink = 1;
    st->st_mtime = 1757808000u;
    fake_n++;
}

static int host_stat(const char *path, OS32_Stat *buf)
{
    int i;
    for (i = 0; i < fake_n; i++) {
        if (!strcmp(fake[i].path, path)) {
            if (fake[i].rc != 0) return fake[i].rc;
            *buf = fake[i].st;
            return 0;
        }
    }
    return OS32_ERR_NOTFOUND;
}

static KernelAPI host_api;

/* stat.c は printf でしか喋らないので、取り込む直前に差し替える。
 * sprintf (st_dev の復号) は実物のまま使う。 */
#define printf cap_printf
#define main stat_main
#include "../../userland/cmds/stat.c"
#undef main
#undef printf

/* ---- 判定の小物 -------------------------------------------------------- */
static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL %s\n  output: %s\n", what, cap_out);
    }
}

static int run(int argc, char **argv)
{
    return stat_main(argc, argv, &host_api);
}

static int has(const char *needle)
{
    return strstr(cap_out, needle) != NULL;
}

static int lines(void)
{
    int i, n = 0;
    for (i = 0; i < cap_len; i++) if (cap_out[i] == '\n') n++;
    return n;
}

/* VFS の st_dev と同じ式 (fs/vfs.c の vfs_mount_dev_of): (type << 8 | unit) + 1 */
#define DEV(t, u) ((u32)((((t) & 0xFF) << 8) | ((u) & 0xFF)) + 1u)

/* ========================================================================= */
/*  case: dev — st_dev の生値と復号                                           */
/* ========================================================================= */
static void case_dev(void)
{
    char *av[2];
    av[0] = (char *)"stat";

    /* hd0 = 1 (TASK_S3 §1a) */
    fake_reset();
    fake_ok("/", (u16)OS_S_IFDIR, DEV(0, 0), 2u, 1024u);
    av[1] = (char *)"/";
    check(run(2, av) == 0, "dev: hd0 exit 0");
    check(has("dev=1(hd0)"), "dev: hd0 raw+decode");

    /* fd0 = 257 (TASK_S3 §1a: FDD ブートの判定に使う) */
    fake_reset();
    fake_ok("/", (u16)OS_S_IFDIR, DEV(1, 0), 2u, 1024u);
    check(run(2, av) == 0, "dev: fd0 exit 0");
    check(has("dev=257(fd0)"), "dev: fd0 raw+decode");

    /* unit が 0 以外 */
    fake_reset();
    fake_ok("/", (u16)OS_S_IFDIR, DEV(0, 1), 2u, 1024u);
    check(run(2, av) == 0, "dev: hd1 exit 0");
    check(has("dev=2(hd1)"), "dev: hd1 raw+decode");

    /* 残りの既知の種別 (fs/vfs.h VFS_DEV_*) */
    fake_reset();
    fake_ok("/", (u16)OS_S_IFDIR, DEV(2, 0), 2u, 0u);
    check(run(2, av) == 0 && has("dev=513(ser0)"), "dev: serial");
    fake_reset();
    fake_ok("/", (u16)OS_S_IFDIR, DEV(3, 0), 2u, 0u);
    check(run(2, av) == 0 && has("dev=769(cd0)"), "dev: cd");
    fake_reset();
    fake_ok("/", (u16)OS_S_IFDIR, DEV(4, 0), 2u, 0u);
    check(run(2, av) == 0 && has("dev=1025(host0)"), "dev: hostdrv");

    /* 未知の種別は数字のまま出す (勝手に名前を作らない) */
    fake_reset();
    fake_ok("/", (u16)OS_S_IFDIR, DEV(7, 1), 2u, 0u);
    check(run(2, av) == 0, "dev: unknown type exit 0");
    check(has("dev=1794(type7 unit1)"), "dev: unknown type raw+decode");

    /* 0 は「不明」(+1 のおかげで実在のデバイスと衝突しない) */
    fake_reset();
    fake_ok("/", (u16)OS_S_IFDIR, 0u, 0u, 0u);
    check(run(2, av) == 0, "dev: zero exit 0");
    check(has("dev=0(unknown)"), "dev: zero is unknown");
}

/* ========================================================================= */
/*  case: fields — 種別・サイズ・ino・mode・時刻                              */
/* ========================================================================= */
static void case_fields(void)
{
    char *av[2];
    av[0] = (char *)"stat";
    av[1] = (char *)"/etc/settings.db";

    fake_reset();
    fake_ok("/etc/settings.db", (u16)(OS_S_IFREG | 0644), DEV(0, 0), 12345u, 3072u);
    check(run(2, av) == 0, "fields: exit 0");
    check(lines() == 1, "fields: 1 path = 1 line");
    check(has("/etc/settings.db: FILE "), "fields: path and FILE");
    check(has("size=3072"), "fields: size");
    check(has("ino=12345"), "fields: st_ino");
    check(has("mode=0100644"), "fields: mode in octal");
    check(has("nlink=1"), "fields: nlink");
    check(has("mtime=1757808000"), "fields: mtime");
    check(has("atime=0") && has("ctime=0"), "fields: atime/ctime");

    /* 種別の区分 (os32_kapi_shared.h の OS_S_IF*) */
    fake_reset();
    fake_ok("/dev", (u16)OS_S_IFDIR, DEV(0, 0), 2u, 1024u);
    av[1] = (char *)"/dev";
    check(run(2, av) == 0 && has("/dev: DIR "), "fields: DIR");

    fake_reset();
    fake_ok("/dev/tty", (u16)OS_S_IFCHR, DEV(0, 0), 3u, 0u);
    av[1] = (char *)"/dev/tty";
    check(run(2, av) == 0 && has("/dev/tty: DEV "), "fields: DEV");

    fake_reset();
    fake_ok("/odd", (u16)0, DEV(0, 0), 4u, 0u);
    av[1] = (char *)"/odd";
    check(run(2, av) == 0 && has("/odd: UNKNOWN "), "fields: UNKNOWN");
}

/* ========================================================================= */
/*  case: notfound — エラー行と終了コード                                     */
/* ========================================================================= */
static void case_notfound(void)
{
    char *av[2];
    av[0] = (char *)"stat";
    av[1] = (char *)"/nope";

    fake_reset();
    check(run(2, av) == 1, "notfound: exit 1");
    check(has("stat: /nope: No such file or directory"), "notfound: error line");
    check(lines() == 1, "notfound: only the error line");

    /* 他の OS32_ERR_* も生の番号ではなく言葉で出す */
    fake_reset();
    fake_err("/bad", OS32_ERR_IO);
    av[1] = (char *)"/bad";
    check(run(2, av) == 1 && has("stat: /bad: I/O error"), "notfound: IO error");

    fake_reset();
    fake_err("/x", OS32_ERR_NOMOUNT);
    av[1] = (char *)"/x";
    check(run(2, av) == 1 && has("stat: /x: Not mounted"), "notfound: NOMOUNT");
}

/* ========================================================================= */
/*  case: multi — 複数引数、失敗しても続ける                                  */
/* ========================================================================= */
static void case_multi(void)
{
    char *av[4];
    av[0] = (char *)"stat";
    av[1] = (char *)"/a";
    av[2] = (char *)"/gone";
    av[3] = (char *)"/b";

    fake_reset();
    fake_ok("/a", (u16)OS_S_IFREG, DEV(0, 0), 10u, 11u);
    fake_ok("/b", (u16)OS_S_IFDIR, DEV(1, 0), 20u, 1024u);
    check(run(4, av) == 1, "multi: exit 1 when one fails");
    check(lines() == 3, "multi: 3 lines (2 stats + 1 error)");
    check(has("/a: FILE size=11"), "multi: first before the failure");
    check(has("stat: /gone: No such file or directory"), "multi: error in the middle");
    check(has("/b: DIR "), "multi: continued after the failure");
    check(has("dev=257(fd0)"), "multi: last entry decoded");

    /* 全部成功なら 0 */
    fake_reset();
    fake_ok("/a", (u16)OS_S_IFREG, DEV(0, 0), 10u, 11u);
    fake_ok("/b", (u16)OS_S_IFDIR, DEV(0, 0), 20u, 1024u);
    av[2] = (char *)"/b";
    check(run(3, av) == 0, "multi: exit 0 when all succeed");
    check(lines() == 2, "multi: 2 lines");
}

/* ========================================================================= */
/*  case: usage — 引数無し                                                    */
/* ========================================================================= */
static void case_usage(void)
{
    char *av[1];
    av[0] = (char *)"stat";

    fake_reset();
    check(run(1, av) == 1, "usage: exit 1");
    check(has("Usage: stat PATH..."), "usage: message");
}

/* ========================================================================= */
int main(int argc, char **argv)
{
    const char *name;

    host_api.sys_stat = host_stat;

    if (argc < 2) {
        fprintf(stderr, "usage: stat-cmd-host <case>\n");
        return 2;
    }
    name = argv[1];

    if (!strcmp(name, "dev"))           case_dev();
    else if (!strcmp(name, "fields"))   case_fields();
    else if (!strcmp(name, "notfound")) case_notfound();
    else if (!strcmp(name, "multi"))    case_multi();
    else if (!strcmp(name, "usage"))    case_usage();
    else { fprintf(stderr, "unknown case %s\n", name); return 2; }

    if (!failures) printf("PASS %s\n", name);
    return failures ? 1 : 0;
}
