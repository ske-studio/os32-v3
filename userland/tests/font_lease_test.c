/* ======================================================================== */
/*  font_lease_test — app 用 font read lease (KAPI v77) のゲスト試験          */
/*                                                                          */
/*  設計 ~/os32-tmp/run/t7bb12/DESIGN.final.md §5 の b1 のゲスト項目          */
/*  (票 docs/tasks/v3/TASK_T7_AND_FOLLOWUPS.md §2-1〜2-4)。                  */
/*  font_source_open / read_at / close を CPL=3 から叩いて戻り値を見る。      */
/*                                                                          */
/*  副命令 (path を省くと FONT_LEASE_TEST_PATH):                             */
/*    lease [path]   open / read_at (SFNT の印・範囲外・位置を持たない) /     */
/*                   生 FD の sys_read・sys_lseek が font を読まない /        */
/*                   close / 再 close (STALE) / close 後の read (STALE)       */
/*    limit [path]   NOSPC まで open → close で 1 本戻る (b1③)               */
/*    other [path]   open した id を子 (別 owner) に渡し、子では INVAL、       */
/*                   親はまだ読める (b1①)                                    */
/*    child <id>     (other が起こす) 渡された id の read_at / close が INVAL */
/*    probe <id>     任意の id (shell の lease など) の read_at / close が     */
/*                   INVAL (b2②、shell の lease が出来る S3b の後)            */
/*    leak [path]    open して close せずに通常終了 (b1⑤、回収は S3b)          */
/*    fault [path]   open して #PF で fault 終了 (b1⑤、回収は S3b)             */
/*  b1⑤ の受入は leak を 20 回 + fault を 1 回の後に lease / limit が通り、   */
/*  空き FD 数が戻ること (S3b の回収 hook の後に回す)。                       */
/*                                                                          */
/*  合否は rt/testresult.h (0 = PASS / 1 = FAIL / 2 = SKIP)。                 */
/* ======================================================================== */

#include "os32api.h"
#include "rt/testresult.h"
#include "memmap.h"   /* KERNEL_LOAD_ADDR (CPL=3 から書けない supervisor 頁) */
#include <stdio.h>
#include <string.h>

#define FONT_LEASE_TEST_PATH  "/data/JF-Dot-Shinonome16.ttf"
#define FONT_LEASE_TEST_SELF  "/usr/bin/font_lease_test.bin"
#define FONT_LEASE_TEST_OPENS 16    /* limit の打ち切り (FD 表の大きさ) */
#define FONT_LEASE_TEST_CMD   160
#define SFNT_HEAD_LEN         12    /* sfnt version + numTables + 3 語 */

static KernelAPI *g_api;
static int g_pass, g_total;
static const char *g_skip;   /* 実行しなかった理由 (NULL = 実行した) */

static void check(int ok, const char *what, int rc);
static int sfnt_magic(const u8 *b);
static int parse_hex(const char *s, u32 *out);
static void run_lease(const char *path);
static void run_limit(const char *path);
static void run_other(const char *path);
static void run_child(const char *arg);
static void run_probe(const char *arg);
static void run_leak(const char *path);
static void run_fault(const char *path);

int main(int argc, char **argv, KernelAPI *api)
{
    const char *cmd = argc > 1 ? argv[1] : "lease";
    const char *arg = argc > 2 ? argv[2] : FONT_LEASE_TEST_PATH;

    g_api = api;
    api->kprintf(0x0E, "font_lease_test: %s %s\n", cmd, arg);
    if (!strcmp(cmd, "lease")) run_lease(arg);
    else if (!strcmp(cmd, "limit")) run_limit(arg);
    else if (!strcmp(cmd, "other")) run_other(arg);
    else if (!strcmp(cmd, "child") && argc > 2) run_child(arg);
    else if (!strcmp(cmd, "probe") && argc > 2) run_probe(arg);
    else if (!strcmp(cmd, "leak")) run_leak(arg);
    else if (!strcmp(cmd, "fault")) run_fault(arg);
    else g_skip = "usage: lease|limit|other|leak|fault [path] / child|probe <id>";
    if (g_skip) return os32_test_summary_skip(api, "font_lease_test", g_skip);
    return os32_test_summary(api, "font_lease_test", g_pass, g_total);
}

static void check(int ok, const char *what, int rc)
{
    g_total++;
    if (ok) {
        g_pass++;
        g_api->kprintf(0x0A, "[PASS] %s (rc=%d)\n", what, rc);
    } else {
        g_api->kprintf(0x4F, "[FAIL] %s (rc=%d)\n", what, rc);
    }
}

/* TrueType 1.0 / 'true' / 'OTTO' */
static int sfnt_magic(const u8 *b)
{
    return (b[0] == 0 && b[1] == 1 && b[2] == 0 && b[3] == 0) ||
           (b[0] == 't' && b[1] == 'r' && b[2] == 'u' && b[3] == 'e') ||
           (b[0] == 'O' && b[1] == 'T' && b[2] == 'T' && b[3] == 'O');
}

static int parse_hex(const char *s, u32 *out)
{
    u32 v = 0;
    int n = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++, n++) {
        char c = *s;
        if (n >= 8) return 0;
        if (c >= '0' && c <= '9') v = (v << 4) | (u32)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (u32)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return n > 0;
}

/* 開けなければ SKIP (font は配っていない、deploy.yaml の注記) */
static int open_or_skip(const char *path, u32 *id, u32 *size)
{
    i32 r = g_api->font_source_open(path, id, size);
    if (r != 0) {
        g_api->kprintf(0x4F, "[SKIP] font_source_open(%s) -> %d\n", path, (int)r);
        g_skip = "font_source_open failed (font not on /data?)";
        return 1;
    }
    return 0;
}

static int parse_or_skip(const char *arg, u32 *id)
{
    if (parse_hex(arg, id)) return 0;
    g_skip = "id must be hex";
    return 1;
}

static void run_lease(const char *path)
{
    u32 id = 0, size = 0, id2 = 0, size2 = 0;
    u8 head[SFNT_HEAD_LEN], again[SFNT_HEAD_LEN], buf[4];
    i32 r;
    int i, same = 1;

    if (open_or_skip(path, &id, &size)) return;
    check(size > SFNT_HEAD_LEN, "open: size", (int)size);
    r = g_api->font_source_read_at(id, 0, head, SFNT_HEAD_LEN);
    check(r == SFNT_HEAD_LEN && sfnt_magic(head), "read_at 0: SFNT magic", (int)r);
    r = g_api->font_source_read_at(id, 0, again, SFNT_HEAD_LEN);
    for (i = 0; i < SFNT_HEAD_LEN; i++) same &= head[i] == again[i];
    check(r == SFNT_HEAD_LEN && same, "read_at keeps no position", (int)r);
    r = g_api->font_source_read_at(id, size - 2, buf, 4);
    check(r == OS32_ERR_INVAL, "read_at past end -> INVAL", (int)r);
    r = g_api->font_source_read_at(id, size + 1, buf, 0);
    check(r == OS32_ERR_INVAL, "read_at offset > size -> INVAL", (int)r);
    r = g_api->font_source_read_at(id, size, buf, 0);
    check(r == 0, "read_at len 0 at end -> 0", (int)r);

    /* 生 FD の公開経路 (sys_read/sys_lseek) が FONT を断ることは host の
     * check-font-source-host が見る。ここで 3..15 を総なめすると、owner を見ない
     * vfs_read_fd が常駐側の FEP 辞書・redirect の位置を動かすので、ゲストではしない。 */

    r = g_api->font_source_open("/__font_lease_test_missing__.ttf", &id2, &size2);
    check(r < 0 && id2 == 0, "open missing -> error", (int)r);
    r = g_api->font_source_open("/", &id2, &size2);
    check(r < 0 && id2 == 0, "open dir -> error", (int)r);

    r = g_api->font_source_close(id);
    check(r == 0, "close", (int)r);
    r = g_api->font_source_close(id);
    check(r == OS32_ERR_STALE, "close again -> STALE", (int)r);
    r = g_api->font_source_read_at(id, 0, buf, 4);
    check(r == OS32_ERR_STALE, "read_at after close -> STALE", (int)r);
}

static void run_limit(const char *path)
{
    u32 ids[FONT_LEASE_TEST_OPENS], size = 0, extra = 0;
    i32 r = 0;
    int n = 0, i, closed = 1;

    if (open_or_skip(path, &ids[0], &size)) return;
    n = 1;
    while (n < FONT_LEASE_TEST_OPENS) {
        r = g_api->font_source_open(path, &ids[n], &size);
        if (r != 0) break;
        n++;
    }
    check(r == OS32_ERR_NOSPC, "open until NOSPC", n);
    r = g_api->font_source_close(ids[0]);
    check(r == 0, "close one", (int)r);
    r = g_api->font_source_open(path, &extra, &size);
    check(r == 0, "open again after close", (int)r);
    r = g_api->font_source_open(path, &ids[0], &size);
    check(r == OS32_ERR_NOSPC, "next open -> NOSPC", (int)r);
    if (g_api->font_source_close(extra) != 0) closed = 0;
    for (i = 1; i < n; i++) if (g_api->font_source_close(ids[i]) != 0) closed = 0;
    check(closed, "close all", n);
}

static void run_other(const char *path)
{
    u32 id = 0, size = 0;
    u8 head[4];
    char cmd[FONT_LEASE_TEST_CMD];
    int kind = 0, code = -1, rc;
    i32 r;

    if (open_or_skip(path, &id, &size)) return;
    snprintf(cmd, sizeof(cmd), "%s child %x", FONT_LEASE_TEST_SELF, (unsigned int)id);
    rc = g_api->exec_run(cmd);
    check(rc == 0 && g_api->exec_last_result(&kind, &code) == 0 &&
          kind == EXEC_KIND_EXITED && code == OS32_TEST_EXIT_PASS,
          "child (other owner) refused the id", code);
    r = g_api->font_source_read_at(id, 0, head, 4);
    check(r == 4 && sfnt_magic(head), "parent still reads", (int)r);
    r = g_api->font_source_close(id);
    check(r == 0, "parent close", (int)r);
}

static void run_child(const char *arg)
{
    u32 id;
    u8 buf[4];
    i32 r;
    if (parse_or_skip(arg, &id)) return;
    r = g_api->font_source_read_at(id, 0, buf, 4);
    check(r == OS32_ERR_INVAL, "child read_at parent id -> INVAL", (int)r);
    r = g_api->font_source_close(id);
    check(r == OS32_ERR_INVAL, "child close parent id -> INVAL", (int)r);
}

static void run_probe(const char *arg)
{
    u32 id;
    u8 buf[4];
    i32 r;
    if (parse_or_skip(arg, &id)) return;
    r = g_api->font_source_read_at(id, 0, buf, 4);
    check(r == OS32_ERR_INVAL, "probe read_at -> INVAL", (int)r);
    r = g_api->font_source_close(id);
    check(r == OS32_ERR_INVAL, "probe close -> INVAL", (int)r);
}

static void run_leak(const char *path)
{
    u32 id = 0, size = 0;
    if (open_or_skip(path, &id, &size)) return;
    g_api->kprintf(0x07, "font_lease_test: leak id=%x (exit without close)\n", id);
    check(1, "open and leave it to the reclaim", 0);
}

static void run_fault(const char *path)
{
    u32 id = 0, size = 0;
    if (open_or_skip(path, &id, &size)) return;
    g_api->kprintf(0x07, "font_lease_test: fault id=%x (expect #PF kill)\n", id);
    *(volatile u32 *)KERNEL_LOAD_ADDR = id;
    check(0, "survived the #PF", 0);
}
