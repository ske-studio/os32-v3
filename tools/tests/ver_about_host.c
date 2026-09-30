/* =========================================================================
 *  VER_ABOUT_HOST.C — 実物の `ver` (userland/shell/cmd_base.c の cmd_ver) を
 *  ホストで回し、出た行をそのまま標準出力へ書く
 *
 *  実行: python3 -B tools/tests/test_about_info.py [--mutate]
 *  記録: tools/tests/about_info_tdd.md
 *
 *  About (userland/rust/about) は `ver` と同じ中身を出す (ユーザー指示
 *  2026-09-29)。試験はこのハーネスの出力と About の info.rs の出力を
 *  同じ入力で突き合わせ、どちらかの文言・条件が変わったら落ちる。
 *
 *  cmd_base.c を 1 行も写さずに #include し、KernelAPI の 3 本 (kprintf /
 *  sys_get_build_info / boot_image_info) と version だけを贋物にする。
 *  シェル側の関数 (help / 表の登録 / execute_command …) は cmd_ver が
 *  使わないので、リンクを通すだけの空の実体を置く。
 *
 *  引数 (全部 10 進、文字列はそのまま):
 *    ver_about_host <kapi> <build> <bi_rc> <crc_valid> <crc> <size> <source> <commit>
 *  bi_rc は boot_image_info の戻り値 (0 = 成功)。
 *  出力: kprintf に出た文字列をそのまま (改行区切り)。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "os32api.h"

static KernelAPI g_fake;
KernelAPI *g_api = &g_fake;

static const char *fk_build;
static int         fk_bi_rc;
static BootImageInfo fk_bi;

static void fk_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    (void)attr;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void fk_sys_get_build_info(char *buf, int size)
{
    if (size <= 0) return;
    strncpy(buf, fk_build, (size_t)size - 1);
    buf[size - 1] = '\0';
}

static int fk_boot_image_info(BootImageInfo *out)
{
    if (fk_bi_rc == 0) *out = fk_bi;
    return fk_bi_rc;
}

/* 変異試験は cmd_base.c の写しを -DVER_CMD_BASE_C="\"<写し>\"" で差し替える。 */
#ifndef VER_CMD_BASE_C
#define VER_CMD_BASE_C "../../userland/shell/cmd_base.c"
#endif
#include VER_CMD_BASE_C

/* cmd_base.c の他のコマンドが参照するシェル側の関数 (cmd_ver は使わない)。 */
int  os32_help_show(const char *name) { (void)name; return -1; }
int  os32_help_exists(const char *name) { (void)name; return 0; }
const ShellCmd *shell_get_cmds(int *count) { *count = 0; return base_cmds; }
void shell_register_cmds(const ShellCmd *cmds) { (void)cmds; }
int  execute_command(const char *cmd) { (void)cmd; return 0; }
void sh_refuse(const char *what, int limit) { (void)what; (void)limit; }

int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: %s kapi build bi_rc crc_valid crc size source commit\n",
                argv[0]);
        return 2;
    }
    g_fake.kprintf = fk_kprintf;
    g_fake.sys_get_build_info = fk_sys_get_build_info;
    g_fake.boot_image_info = fk_boot_image_info;
    g_fake.version = (u32)strtoul(argv[1], NULL, 10);
    fk_build = argv[2];
    fk_bi_rc = atoi(argv[3]);
    memset(&fk_bi, 0, sizeof(fk_bi));
    fk_bi.crc_valid = (u8)atoi(argv[4]);
    fk_bi.image_crc = (u32)strtoul(argv[5], NULL, 10);
    fk_bi.image_size = (u32)strtoul(argv[6], NULL, 10);
    fk_bi.source = (u8)atoi(argv[7]);
    strncpy(fk_bi.commit, argv[8], sizeof(fk_bi.commit) - 1);
    return cmd_ver(1, argv);
}
