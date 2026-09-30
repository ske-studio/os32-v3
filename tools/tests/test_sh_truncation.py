"""切り詰め: シェルが入力を黙って切り詰める経路を実物のソースで押さえる。

票:   docs/archive/shell/TASK_SH_TRUNCATION.md §5 の段 2 (T1 と §2-1)
記録: tools/tests/sh_truncation_tdd.md

  python3 -B tools/tests/test_sh_truncation.py [--mutate]

--mutate は**否定側**。この段の中心規則は

  (1) 切り詰めた値では比べない (T1)
  (2) スクリプト実行中に行を断ったら打ち切る (§2-1)
  (3) 対話 / rshell / 起動時の profile では打ち切らない

の 3 つなので、それぞれをわざと壊した版を作って**試験が落ちること**を見る。
GREEN のまま通ってしまう変異があれば、その規則を試験が見ていないということ。

test_sh_shell.py と同じ様式 — ホスト ILP32 GNU89 で走らせ、同じソースが外部
プログラムと同じ形の i386-elf-gcc でも通ることを別に見る ([C1] C89/GNU89)。
Make・エミュレータは使わない。

ホスト側は tools/tests/sh_truncation_host.c が userland/shell/main.c を
(したがって sh_exec.inc / sh_args.inc / sh_launch.inc / sh_pipe.inc も)
そのまま #include し、登録表と execute_command を**実物のまま**通す。
libc は使わない (-nostdlib) ので、shell.h が引く <string.h> と
<stdio.h> / <stdlib.h> だけ一時ディレクトリに薄いシムを置く。
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def fd_root_dev():
    """kernel/kernel.c が FD 起動でルートに使うデバイス名 (試験で決め打ちしない)"""
    src = (ROOT / "kernel/kernel.c").read_text(encoding="utf-8")
    m = re.search(r'BOOT_DRIVE_FDD[^{]*\{\s*root_dev\s*=\s*"(\w+)";', src)
    if not m:
        sys.exit("kernel.c から FD ルートのデバイス名を読めない")
    return m.group(1)


BASE_NOAPP = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
              "-fno-stack-protector", "-Wall", "-Wdeclaration-after-statement",
              "-D__OS32_USERLAND__", '-DFD_ROOT_DEV="%s"' % fd_root_dev()]
BASE = BASE_NOAPP + ["-DSHELL_AS_APP"]
INCLUDES = ["-I" + str(ROOT / "sdk/include"), "-I" + str(ROOT / "sdk/include/os32"),
            "-I" + str(ROOT / "include"), "-I" + str(ROOT / "userland/shell"),
            # 段 4 で ui.c / rshell.c / cmd_filer.c を取り込んだぶん
            "-I" + str(ROOT / "userland/lib"),
            # cmd_pci.c が "drivers/pci_decode.h" を引く (実ビルドの
            # PROGRAM_FLAGS の -I. と同じ)。L-A の lspci / pcidump。
            "-I" + str(ROOT),
            "-I" + str(ROOT / "userland/lib/filer")]
HOST_SRC = ROOT / "tools/tests/sh_truncation_host.c"

STRING_SHIM = """/* テスト用の薄い <string.h>。実体は sh_truncation_host.c にある。 */
#ifndef OS32_TEST_STRING_H
#define OS32_TEST_STRING_H
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, unsigned long n);
unsigned long strlen(const char *s);
void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
char *strncpy(char *d, const char *s, unsigned long n);
char *strncat(char *d, const char *s, unsigned long n);
char *strcat(char *d, const char *s);
#endif
"""

STDIO_SHIM = """/* テスト用の薄い <stdio.h>。main.c が使うのは setvbuf / fflush / printf。 */
#ifndef OS32_TEST_STDIO_H
#define OS32_TEST_STDIO_H
#define BUFSIZ 1024
#define _IOLBF 1
extern void *stdout_impl;
#define stdout (stdout_impl)
int printf(const char *fmt, ...);
int fflush(void *stream);
int setvbuf(void *stream, char *buf, int mode, unsigned long sz);
#endif
"""

STDLIB_SHIM = """/* テスト用の薄い <stdlib.h>。cmd_mnt.c が atoi、rshell.c が strtoul。 */
#ifndef OS32_TEST_STDLIB_H
#define OS32_TEST_STDLIB_H
int atoi(const char *s);
unsigned long strtoul(const char *s, char **end, int base);
#endif
"""


def write_shims(tmp):
    (tmp / "string.h").write_text(STRING_SHIM)
    (tmp / "stdio.h").write_text(STDIO_SHIM)
    (tmp / "stdlib.h").write_text(STDLIB_SHIM)
    return ["-I" + str(tmp)]


def build_host(tmp, shim, name):
    exe = tmp / name
    subprocess.run(["gcc", *BASE, "-O0", *shim, *INCLUDES,
                    "-nostdlib", "-static", "-no-pie",
                    str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
    return exe


# ---------------------------------------------------------------------------
#  変異 (否定側)。(ファイル, 置換前, 置換後) — 置換前は 1 か所だけに出ること。
# ---------------------------------------------------------------------------
MUTATIONS = [
    # 変異 1: 段 2 の前の姿。strip_quotes が黙って max-1 文字に切る。
    #         → 先頭 255 文字が同じ 2 つの値が「等しい」になる (T1 / U1 / U2)。
    ("compare_truncated", "userland/shell/cmd_script.c",
     "            if (di >= max - 1) return -1;",
     "            if (di >= max - 1) break;"),
    # 変異 2: 断っても**印を立てない**版 (§2-1 の否定側)。
    #         断り自体は出るので、スクリプトが後続行へ落ちるかどうかだけが変わる。
    ("no_mark", "userland/shell/main.c",
     '    g_api->kprintf(ATTR_RED, "%s too long (max %d)\\n", what, limit);\n'
     "    sh_refused_flag = 1;",
     '    g_api->kprintf(ATTR_RED, "%s too long (max %d)\\n", what, limit);'),
    # 変異 3: 印を**消し忘れる**版。前の行の断りが次の行に持ち越され、
    #         関係のないスクリプトが 1 行目で打ち切られる (誤発火)。
    ("no_clear", "userland/shell/main.c",
     "    if (g_exec_depth == 0) sh_refused_flag = 0;",
     "    if (0) sh_refused_flag = 0;"),
    # 変異 4: 入れ子でも印を消す版。断った段の後ろの段が `time ...` だと
    #         そこで印が消えて後続行へ落ちる (取りこぼし)。
    ("nested_clear", "userland/shell/main.c",
     "    if (g_exec_depth == 0) sh_refused_flag = 0;",
     "    sh_refused_flag = 0;"),
    # 変異 5: **対話でも打ち切る**版。断った行の次の行が動かなくなり、
    #         rshell も起動時の profile も道連れになる。
    ("abort_interactive", "userland/shell/main.c",
     "    if (g_exec_depth == 0) sh_refused_flag = 0;",
     "    if (g_exec_depth == 0 && sh_refused_flag) return;\n"
     "    if (g_exec_depth == 0) sh_refused_flag = 0;"),
    # 変異 6: 入れ子 source の断りを親へ伝えない版 (§2-1 の否定側)。
    ("source_not_propagated", "userland/shell/cmd_script.c",
     "    r = script_source_file(argv[1]);\n"
     "    if (r == SCRIPT_ERR_REFUSED) { sh_refuse_mark(); return SH_STATUS_USAGE; }",
     "    r = script_source_file(argv[1]);\n"
     "    if (r == SCRIPT_ERR_REFUSED) { return SH_STATUS_USAGE; }"),
    # 変異 7: パイプの段ループが印を**見ない**版 (= PM 決裁の前の姿)。
    #         断った段の後続の段が走り、`> file` が O_TRUNC で開かれる。
    ("pipe_no_peek", "userland/shell/main.c",
     "                if (sh_refused_peek()) { status = SH_STATUS_USAGE; break; }",
     "                if (0) { status = SH_STATUS_USAGE; break; }"),
    # 変異 8: 起動時の profile が印を立て直す版 (R2 の否定側)。
    #         profile の断りが起動後の 1 行目を巻き添えにする。
    ("profile_aborts_boot", "userland/shell/cmd_script.c",
     '        g_api->kprintf(ATTR_RED,\n'
     '                       "sh: %s aborted; continuing with defaults\\n", path);',
     '        g_api->kprintf(ATTR_RED,\n'
     '                       "sh: %s aborted; continuing with defaults\\n", path);\n'
     "        sh_refuse_mark();"),

    # ---- 段 3 (ルーター) の否定側 --------------------------------------
    # 経路ごとに「検査を外した版」と、印で伝える経路は「印を立てない版」。
    # どれも **RED になること** が「その規則を試験が見ている」証拠。

    # T3-a: try_exec の長さ検査そのものを外す (= 切り詰めて起動する昔の姿)
    ("t3_no_check", "userland/shell/sh_exec.inc",
     "        if (try_exec_len(bin_path, argc, argv) > TRY_EXEC_BUF_SIZE - 2) {\n"
     '            sh_refuse("sh: argument list", TRY_EXEC_BUF_SIZE - 2);\n'
     "            if (kind) *kind = k;\n"
     "            if (code) *code = c;\n"
     "            return EXEC_ERR_GENERAL;\n"
     "        }",
     "        if (0) {\n"
     "            return EXEC_ERR_GENERAL;\n"
     "        }"),
    # T3-b: クォートの再付与ぶん (+2) を数えない
    ("t3_no_quote_pad", "userland/shell/sh_exec.inc",
     '        if (need_quote) total += 2;          /* 前後の " */',
     "        if (0) total += 2;"),
    # T3-c: エスケープの \\ (2 倍) を数えない
    ("t3_no_escape_count", "userland/shell/sh_exec.inc",
     "            if (*s == '\"' || *s == '\\\\') body++;   /* エスケープの \\ */",
     "            if (0) body++;"),
    # T3-d: 内蔵 exec (255) の検査を外す
    ("t3_exec_no_check", "userland/shell/cmd_mnt.c",
     "        if (need > EXEC_CMDLINE_MAX - 1) {\n"
     '            sh_refuse("exec: command line", EXEC_CMDLINE_MAX - 1);\n'
     "            return SH_STATUS_USAGE;\n"
     "        }",
     "        if (0) {\n"
     "            return SH_STATUS_USAGE;\n"
     "        }"),
    # T3-e: 内蔵 time (510) の検査を外す
    ("t3_time_no_check", "userland/shell/cmd_base.c",
     "        if (need > TIME_CMD_MAX - 2) {\n"
     '            sh_refuse("time: command line", TIME_CMD_MAX - 2);\n'
     "            return SH_STATUS_USAGE;\n"
     "        }",
     "        if (0) {\n"
     "            return SH_STATUS_USAGE;\n"
     "        }"),
    # T4: コマンド名を切って .bin を付ける昔の姿へ戻す
    ("t4_no_check", "userland/shell/sh_exec.inc",
     "        if ((int)strlen(argv[0]) > PATH_MAX_LEN - 5) {\n"
     '            sh_refuse("sh: command name", PATH_MAX_LEN - 5);\n'
     "            return SH_STATUS_USAGE;\n"
     "        }",
     "        if (0) {\n"
     "            return SH_STATUS_USAGE;\n"
     "        }"),
    # T5-a: 9 段目以降を黙って捨てる
    ("t5_drop_stages", "userland/shell/main.c",
     "        if (count >= max_stages) {\n"
     '            sh_refuse("sh: pipeline", max_stages);\n'
     "            return -1;\n"
     "        }",
     "        if (count >= max_stages) {\n"
     "            return count;\n"
     "        }"),
    # T5-b: 空の段を黙って捨てる (`echo ok |` が 1 段として走る昔の姿)
    ("t5_empty_stage", "userland/shell/main.c",
     '            g_api->kprintf(ATTR_RED, "%s", "sh: empty pipeline stage\\n");\n'
     "            sh_refuse_mark();\n"
     "            return -1;",
     "            if (*p == '|') { p++; continue; }\n"
     "            break;"),
    # T5-c: 空の段は報せるが **印を立てない** (スクリプトが後続行へ落ちる)
    ("t5_empty_no_mark", "userland/shell/main.c",
     '            g_api->kprintf(ATTR_RED, "%s", "sh: empty pipeline stage\\n");\n'
     "            sh_refuse_mark();\n",
     '            g_api->kprintf(ATTR_RED, "%s", "sh: empty pipeline stage\\n");\n'),
    # T6: glob の確保失敗で **印を立てず** に戻る (一部だけ渡す昔の姿)
    ("t6_no_mark", "userland/shell/sh_args.inc",
     "        if (!full) { ctx->alloc_failed = 1; return; }",
     "        if (!full) { return; }"),
    # T7-a: パターンを切って照合する昔の姿
    ("t7_pattern_truncates", "userland/shell/sh_args.inc",
     "        if (n >= GLOB_PATTERN_MAX - 1) {\n"
     '            sh_refuse("sh: glob pattern", GLOB_PATTERN_MAX - 1);\n'
     "            return -1;\n"
     "        }",
     "        if (n >= GLOB_PATTERN_MAX - 1) break;"),
    # T7-b: ディレクトリ部を切って照合する昔の姿
    ("t7_dir_truncates", "userland/shell/sh_args.inc",
     "                if (dirlen > PATH_MAX_LEN - 1) {\n"
     '                    sh_refuse("sh: glob directory", PATH_MAX_LEN - 1);\n'
     "                    *argc_out = argc;\n"
     "                    return -1;\n"
     "                }\n"
     "                for (i = 0; i < dirlen; i++) dir_path[i] = start[i];",
     "                for (i = 0; i < dirlen && i < PATH_MAX_LEN - 1; i++)\n"
     "                    dir_path[i] = start[i];"),
    # T11: 送る前に測らない (launch_req の INVAL を「GUI 外」と読む昔の姿)
    ("t11_no_check", "userland/shell/sh_launch.inc",
     "        if (len >= LAUNCH_CMDLINE_MAX) {\n"
     '            sh_refuse("sh: launch command line", LAUNCH_CMDLINE_MAX - 1);\n'
     "            /* 印が立つので呼び手はそちらを先に見る。種別は NONE のまま。 */\n"
     "            return EXEC_ERR_GENERAL;\n"
     "        }",
     "        if (0) {\n"
     "            return EXEC_ERR_GENERAL;\n"
     "        }"),
    # T13-a: execute_command が長大行を空行と同じ扱いで黙って捨てる
    ("t13_line_silent", "userland/shell/main.c",
     "    if (strlen(cmd) >= CMD_BUF_SIZE) {\n"
     '        sh_refuse("sh: command line", CMD_BUF_SIZE - 1);\n'
     "        return SH_STATUS_USAGE;\n"
     "    }",
     "    if (strlen(cmd) >= CMD_BUF_SIZE) {\n"
     "        return status;\n"
     "    }"),
    # T13-b: execute_single が同上
    ("t13_single_silent", "userland/shell/main.c",
     "    if (strlen(cmd) >= CMD_BUF_SIZE) {\n"
     '        sh_refuse("sh: command", CMD_BUF_SIZE - 1);\n'
     "        return SH_STATUS_USAGE;\n"
     "    }",
     "    if (strlen(cmd) >= CMD_BUF_SIZE) {\n"
     "        return status;\n"
     "    }"),
    # T17-a: PATH 項目の区切りを見失ったまま進む昔の姿
    ("t17_entry_split", "userland/shell/sh_exec.inc",
     "        if (*p && *p != ':') {\n"
     '            sh_refuse("sh: PATH entry", PATH_MAX_LEN - 2);\n'
     "            *kind = EXEC_KIND_NONE;\n"
     "            return EXEC_ERR_GENERAL;\n"
     "        }",
     "        if (0) {\n"
     "            return EXEC_ERR_GENERAL;\n"
     "        }"),
    # T17-b: dir + '/' + name の連結が入り切らなくても切って試す昔の姿
    ("t17_join_truncates", "userland/shell/sh_exec.inc",
     "            if (need > PATH_MAX_LEN - 1) {\n"
     '                sh_refuse("sh: command path", PATH_MAX_LEN - 1);\n'
     "                *kind = EXEC_KIND_NONE;\n"
     "                return EXEC_ERR_GENERAL;\n"
     "            }",
     "            if (0) {\n"
     "                return EXEC_ERR_GENERAL;\n"
     "            }"),
    # 走査を止めない版: 断っても次の PATH 候補へ回してしまう
    # (2026-09-16 票 TASK_EXIT_STATUS) 断りの検査を外すだけでは、種別
    # (EXEC_KIND_NONE) でも走査が止まるので歯が立たない。断りを「見つからない」
    # に化けさせて **次の候補へ回す** 版にする (= 直す前の姿そのもの)。
    ("scan_not_stopped", "userland/shell/sh_exec.inc",
     "        if (!was_refused && sh_refused_peek()) return EXEC_ERR_GENERAL;\n"
     "        if (*kind == EXEC_KIND_INVALID) {",
     "        if (!was_refused && sh_refused_peek()) *kind = EXEC_KIND_NOT_FOUND;\n"
     "        if (*kind == EXEC_KIND_INVALID) {"),
    # I1: 引数が多すぎて行を捨てるときに **印を立てない** 版 (PM 決裁の前の姿)。
    #     赤字は出るので、スクリプトが後続行へ落ちるかどうかだけが変わる。
    ("i1_no_mark", "userland/shell/sh_args.inc",
     "            if (ctx.overflow) {\n"
     '                g_api->kprintf(ATTR_RED, "%s", "sh: too many arguments\\n");\n'
     "                sh_refuse_mark();",
     "            if (ctx.overflow) {\n"
     '                g_api->kprintf(ATTR_RED, "%s", "sh: too many arguments\\n");'),

    # ---- 段 4 (内蔵と入口) の否定側 ------------------------------------
    # 経路ごとに「検査を外した版 = 直す前の姿」。どれも RED になることが
    # 「その規則を試験が見ている」証拠。

    # T2-a: 長い行を黙って 255 文字へ切る (直す前の姿)
    ("t2_line_truncates", "userland/shell/cmd_script.c",
     "            if (li > SCRIPT_MAX_LINE - 1) {\n"
     '                sh_refuse("source: script line", SCRIPT_MAX_LINE - 1);\n'
     "                refused = 1;\n"
     "                break;\n"
     "            }",
     "            if (li > SCRIPT_MAX_LINE - 1) li = SCRIPT_MAX_LINE - 1;"),
    # T2-b: 129 行目を捨てて先頭 128 行を実行する (直す前の姿)
    ("t2_lines_continue", "userland/shell/cmd_script.c",
     "                    sh_refuse_mark();\n"
     "                    refused = 1;\n"
     "                    break;",
     "                    break;"),
    # T2-c: 読み切れたかを確かめない (32KB 超が黙って切れる)
    ("t2_read_no_probe", "userland/shell/cmd_script.c",
     "    if (sz == raw_buf_size - 1) {\n"
     "        char probe;\n"
     "        if (g_api->sys_read(fd, &probe, 1) > 0) more = 1;\n"
     "    }",
     "    if (0) {\n"
     "        more = 1;\n"
     "    }"),
    # T2-d: 起動スクリプトの断りで印を残す (起動後の 1 行目が巻き添え)
    ("t2_profile_keeps_mark", "userland/shell/cmd_script.c",
     "    if (r < 0) (void)sh_refused_take();",
     "    if (0) (void)sh_refused_take();"),

    # T8-a: 登録口 (env_set) が長さを見ない
    ("t8_env_set_no_check", "userland/shell/cmd_env.c",
     "    if ((int)strlen(name) > ENV_NAME_MAX - 1) {\n"
     '        sh_refuse("set: variable name", ENV_NAME_MAX - 1);\n'
     "        return;\n"
     "    }\n"
     "    if ((int)strlen(value) > ENV_VALUE_MAX - 1) {\n"
     '        sh_refuse("set: variable value", ENV_VALUE_MAX - 1);\n'
     "        return;\n"
     "    }\n",
     ""),
    # T8-b: cmd_set が名前を 31 文字で切る
    ("t8_set_name_truncates", "userland/shell/cmd_env.c",
     "        while (*arg && *arg != '=') {\n"
     "            if (ni >= ENV_NAME_MAX - 1) {\n"
     '                sh_refuse("set: variable name", ENV_NAME_MAX - 1);\n'
     "                return SH_STATUS_USAGE;\n"
     "            }\n"
     "            name[ni++] = *arg++;\n"
     "        }",
     "        while (*arg && *arg != '=' && ni < ENV_NAME_MAX - 1)\n"
     "            name[ni++] = *arg++;"),

    # T9: 変数名が 31 文字を超えても打ち切って素通しする (直す前の姿)
    ("t9_name_no_check", "userland/shell/cmd_env.c",
     "                if (vi >= ENV_NAME_MAX - 1) return ENV_EXPAND_ERR_NAME;",
     "                if (vi >= ENV_NAME_MAX - 1) break;"),

    # T10-a: 126 文字で読み取りを止める (残りが次の入力になる)
    ("t10_stop_reading", "userland/shell/rshell.c",
     "        while (ch >= 0 && ch != '\\n' && ch != '\\r') {\n"
     "            if (rpos >= RSHELL_LINE_MAX - 2) overflow = 1;\n"
     "            else rbuf[rpos++] = (char)ch;",
     "        while (ch >= 0 && ch != '\\n' && ch != '\\r' &&\n"
     "               rpos < RSHELL_LINE_MAX - 2) {\n"
     "            rbuf[rpos++] = (char)ch;"),
    # T10-b: 断ったときに EOT を返さない (票 §2-2 の blocker そのもの)
    ("t10_no_eot", "userland/shell/rshell.c",
     "            sh_status_set(SH_STATUS_USAGE);\n"
     "            rshell_end_reply();\n"
     "            continue;",
     "            sh_status_set(SH_STATUS_USAGE);\n"
     "            continue;"),
    # T10-c: 抜け口 (ホストの `exit` / 行の途中の ESC) で EOT を返さない。
    #        直す前の姿そのもの — /api/cmd が 15 秒待ってタイムアウトする。
    ("t10_exit_no_eot", "userland/shell/rshell.c",
     "    if (rpos > 0) rshell_end_reply();\n"
     "\n"
     "    g_api->rshell_set_active(0);",
     "    g_api->rshell_set_active(0);"),
    # T10-d: rshell が断りの印を下ろさない。rshell は常に入れ子なので
    #        main.c の「いちばん外側だけ消す」が効かず、一度断ると以降の
    #        スクリプトが全部 1 行目で打ち切られる (2026-09-16 の退行)。
    ("t10_flag_leaks_between_lines", "userland/shell/rshell.c",
     "        (void)sh_refused_take();\n"
     "\n"
     "        rshell_end_reply();",
     "        rshell_end_reply();"),

    # T12: join_args が黙って切る (切れたコマンド行を実行する)
    ("t12_join_truncates", "userland/shell/cmd_script.c",
     "    for (i = start; i < argc; i++) {\n"
     "        if (i > start) {\n"
     "            if (bi >= max - 1) return -1;\n"
     "            buf[bi++] = ' ';\n"
     "        }\n"
     "        for (j = 0; argv[i][j]; j++) {\n"
     "            if (bi >= max - 1) return -1;\n"
     "            buf[bi++] = argv[i][j];\n"
     "        }\n"
     "    }",
     "    for (i = start; i < argc; i++) {\n"
     "        if (i > start && bi < max - 1) buf[bi++] = ' ';\n"
     "        for (j = 0; argv[i][j] && bi < max - 1; j++) {\n"
     "            buf[bi++] = argv[i][j];\n"
     "        }\n"
     "    }"),

    # T14-a: 切れた行を履歴に入れる
    ("t14_hist_add_truncates", "userland/shell/ui.c",
     "    if ((int)str_len(s) > HIST_LINE_MAX - 1) return;",
     "    if (0) return;"),
    # T14-b: .history の切れた行を読み込む
    ("t14_hist_load_truncates", "userland/shell/ui.c",
     "            int cut = (li > HIST_LINE_MAX - 1) || (bi == sz && more);",
     "            int cut = 0;\n"
     "            if (li > HIST_LINE_MAX - 1) li = HIST_LINE_MAX - 1;"),
    # T14-c: .history を読み切れたか確かめない
    ("t14_hist_no_probe", "userland/shell/ui.c",
     "    if (sz == HIST_SIZE * HIST_LINE_MAX - 1) {\n"
     "        char probe;\n"
     "        if (g_api->sys_read(fd, &probe, 1) > 0) more = 1;\n"
     "    }",
     "    if (0) {\n"
     "        more = 1;\n"
     "    }"),

    # T15-a: 4092 バイトで打鍵を黙って捨てる (接頭辞が実行される)
    ("t15_drop_silent", "userland/shell/ui.c",
     "                    /* T15: ここで黙って捨てると接頭辞が実行される。\n"
     "                     * 印を立てて ENTER のところで行ごと断る。 */\n"
     "                    cmd_dropped = 1;",
     "                    cmd_dropped = 0;"),
    # T15-b: ESC で印を消さない (誤発火の側)
    ("t15_esc_keeps_mark", "userland/shell/ui.c",
     "cmd_len=cmd_pos=cmd_buf[0]=0; cmd_dropped=0; redraw_line",
     "cmd_len=cmd_pos=cmd_buf[0]=0; redraw_line"),

    # T16-a: 63 バイトで切った名前を一覧に載せる
    ("t16_name_truncates", "userland/shell/cmd_filer.c",
     "    for (i = 0; name[i]; i++) {}\n"
     "    if (i > FL_MAX_NAME_LEN - 1) { fl_state.dropped++; return; }\n"
     "\n"
     "    if (fl_state.count >= FL_MAX_ENTRIES) { fl_state.dropped++; return; }\n"
     "\n"
     "    e = &fl_state.entries[fl_state.count];\n"
     "    for (i = 0; name[i]; i++) e->name[i] = name[i];",
     "    if (fl_state.count >= FL_MAX_ENTRIES) return;\n"
     "\n"
     "    e = &fl_state.entries[fl_state.count];\n"
     "    for (i = 0; name[i] && i < FL_MAX_NAME_LEN - 1; i++)\n"
     "        e->name[i] = name[i];"),
    # T16-b: fl_path_join が黙って切る (別のファイルを起動する)
    ("t16_join_truncates", "userland/shell/cmd_filer.c",
     "    while (dir[j]) {\n"
     "        if (i >= out_sz - 2) return -1;\n"
     "        out[i++] = dir[j++];\n"
     "    }\n"
     "    if (i > 0 && out[i - 1] != '/') out[i++] = '/';\n"
     "    j = 0;\n"
     "    while (name[j]) {\n"
     "        if (i >= out_sz - 1) return -1;\n"
     "        out[i++] = name[j++];\n"
     "    }",
     "    while (dir[j] && i < out_sz - 2) out[i++] = dir[j++];\n"
     "    if (i > 0 && out[i - 1] != '/') out[i++] = '/';\n"
     "    j = 0;\n"
     "    while (name[j] && i < out_sz - 1) out[i++] = name[j++];"),
    # T16-c: /etc/filetypes を読み切れたか確かめない
    ("t16_ft_no_probe", "userland/shell/cmd_filer.c",
     "    if (sz == FL_FILETYPES_MAXSZ - 1) {\n"
     "        char probe;\n"
     "        if (g_api->sys_read(fd, &probe, 1) > 0) {\n"
     "            g_api->sys_close(fd);\n"
     "            g_api->mem_free(ft_buf);\n"
     "            ft_buf = NULL;\n"
     "            return;\n"
     "        }\n"
     "    }",
     "    if (0) {\n"
     "        return;\n"
     "    }"),
    # 8.3 の /etc/filetype を FD 以外 (HDD) でも読む (Codex 実装レビュー、25n)
    ("fd83_any_root", "userland/shell/cmd_filer.c",
     "    if (fd < 0 && ft_etc_on_fd_root()) fd = g_api->sys_open(FL_FILETYPES_PATH_83, O_RDONLY);",
     "    if (fd < 0 && (ft_etc_on_fd_root() || 1)) fd = g_api->sys_open(FL_FILETYPES_PATH_83, O_RDONLY);"),
    # /etc のマウントを見ずにルートのデバイスだけで決める (往復 2、25q)
    ("fd83_root_only", "userland/shell/cmd_filer.c",
     "    if (st_root.st_dev != st_etc.st_dev) return 0;\n",
     "    if (st_root.st_dev != st_etc.st_dev) (void)0;\n"),

    # T18-a: ask が 255 文字目以降を黙って捨てる
    ("t18_ask_input_silent", "userland/shell/cmd_script.c",
     "            if (len >= ASK_INPUT_MAX - 2) {\n"
     "                dropped = 1;\n"
     "                continue;\n"
     "            }",
     "            if (len >= ASK_INPUT_MAX - 2) {\n"
     "                continue;\n"
     "            }"),
    # T18-b: ask のプロンプトを黙って切る
    ("t18_ask_prompt_truncates", "userland/shell/cmd_script.c",
     "        for (j = 0; argv[i][j]; j++) {\n"
     "            if (argv[i][j] == '\"') continue;\n"
     "            if (pi >= ASK_PROMPT_MAX - 2) {\n"
     '                sh_refuse("ask: prompt", ASK_PROMPT_MAX - 2);\n'
     "                return SH_STATUS_USAGE;\n"
     "            }\n"
     "            prompt[pi++] = argv[i][j];\n"
     "        }",
     "        for (j = 0; argv[i][j] && pi < ASK_PROMPT_MAX - 2; j++) {\n"
     "            if (argv[i][j] != '\"') prompt[pi++] = argv[i][j];\n"
     "        }"),

    # T19: resolve_host_path が黙って切る (別のパスを O_TRUNC で作る)
    ("t19_host_path_truncates", "userland/shell/rshell.c",
     "    while (*p) {\n"
     "        if (i >= max - 1) return -1;\n"
     "        out[i++] = *p++;\n"
     "    }",
     "    while (*p && i < max - 1) { out[i++] = *p++; }"),

    # T20-a: 履歴のパスで HOME を切る (別ディレクトリの .history)
    ("t20_hist_path_truncates", "userland/shell/ui.c",
     "    while (*h) {\n"
     "        if (pi >= max - HIST_FILE_ROOM) return -1;\n"
     "        path[pi++] = *h++;\n"
     "    }",
     "    while (*h && pi < max - HIST_FILE_ROOM) path[pi++] = *h++;"),
    # T20-b: .profile のパスで HOME を切る (別ディレクトリの .profile)
    ("t20_profile_truncates", "userland/shell/ui.c",
     "            while (*h) {\n"
     "                if (pi >= PATH_MAX_LEN - PROFILE_PATH_ROOM) { too_long = 1; break; }\n"
     "                profile_path[pi++] = *h++;\n"
     "            }",
     "            while (*h && pi < PATH_MAX_LEN - PROFILE_PATH_ROOM)\n"
     "                profile_path[pi++] = *h++;"),

    # T21-a: コマンド名補完が 63 バイトで切る
    ("t21_cmd_comp_truncates", "userland/shell/ui.c",
     "        if (base_len > (int)sizeof(ctx->name_store[0]) - 1) return;",
     "        if (base_len >= 63) base_len = 63;"),
    # T21-b: ファイル名補完が 126 バイトで切る
    ("t21_file_comp_truncates", "userland/shell/ui.c",
     "    {\n"
     "        int need = nlen + ((entry->type == OS32_FILE_TYPE_DIR) ? 1 : 0);\n"
     "        if (need > (int)sizeof(ctx->name_store[0]) - 1) return;\n"
     "    }\n"
     "    for (i = 0; i < nlen; i++)\n"
     "        ctx->name_store[ctx->count][i] = name[i];\n"
     "    if (entry->type == OS32_FILE_TYPE_DIR) {\n"
     "        ctx->name_store[ctx->count][i++] = '/';\n"
     "    }",
     "    for (i = 0; i < nlen && i < 126; i++)\n"
     "        ctx->name_store[ctx->count][i] = name[i];\n"
     "    if (entry->type == OS32_FILE_TYPE_DIR && i < 127) {\n"
     "        ctx->name_store[ctx->count][i++] = '/';\n"
     "    }"),

    # T22: system.cfg を読み切れたか確かめない (切れたまま書き戻す)
    ("t22_cfg_no_probe", "userland/shell/cmd_sys.c",
     "        if (r == (int)sizeof(buf) - 1) {\n"
     "            char probe;\n"
     "            if (g_api->sys_read(fd, &probe, 1) > 0) more = 1;\n"
     "        }",
     "        if (0) {\n"
     "            more = 1;\n"
     "        }"),

    # ---- 継承バグ「source が ESC 以外も食う」の否定側 -------------------

    # 変異 A: 直す前の姿。行ごとの監視が kbd_trygetkey で**取り出して捨てる**。
    #         ESC 以外の打鍵が消え、スクリプトの後の入力の先頭が欠ける。
    ("esc_watch_eats_key", "userland/shell/cmd_script.c",
     "            int k = g_api->kbd_peekkey();\n"
     "            if (k >= 0 && (k & 0xFF) == 0x1B) {\n"
     "                (void)g_api->kbd_trygetkey();   /* ESC 自身は取り除く */\n",
     "            int k = g_api->kbd_trygetkey();\n"
     "            if (k >= 0 && (k & 0xFF) == 0x1B) {\n"),
    # 変異 B: 覗くだけで **ESC も取り除かない** 版。打ち切りは効くが、ESC が
    #         キューに残ってスクリプトの後の行編集が即 ESC を食う。
    ("esc_not_removed", "userland/shell/cmd_script.c",
     "                (void)g_api->kbd_trygetkey();   /* ESC 自身は取り除く */\n",
     ""),
    # 変異 C: ESC の打ち切りそのものを外す版 (今の挙動を弱めていないかの裏)。
    ("esc_no_abort", "userland/shell/cmd_script.c",
     "            int k = g_api->kbd_peekkey();\n"
     "            if (k >= 0 && (k & 0xFF) == 0x1B) {",
     "            int k = g_api->kbd_peekkey();\n"
     "            if (0) {"),

    # ---- 行ごとの譲り (GUI 端末) の否定側 -------------------------------

    # 変異 D: 譲りを外した版 = 2026-09-16 の退行そのもの。ESC 監視を
    #         kbd_peekkey に替えたとき、kbd_trygetkey の副作用で出ていた
    #         exec_park_poll ごと消えた姿 (GUI で WM が 1 度も回らない)。
    ("script_no_yield", "userland/shell/cmd_script.c",
     "        script_yield_gui();",
     "        ((void)0);"),
    # 変異 E: 間引きを外した版。行ごとに必ず park / resume の往復が出るので、
    #         `goto` で回る軽い行のループが往復ぶんだけ遅くなる。
    ("script_yield_no_throttle", "userland/shell/cmd_script.c",
     "    if (now == g_script_yield_tick) return;   /* 同じ tick の中では譲らない */",
     "    if (0) return;"),

    # T24: push が sys_read を 1 回しか呼ばない (先頭 4KB だけ送る)
    ("t24_push_single_read", "userland/shell/rshell.c",
     "    total = 0;\n"
     "    for (;;) {\n"
     "        n = g_api->sys_read(fd_in, xfer_buf, sizeof(xfer_buf));",
     "    total = 0;\n"
     "    while (total == 0) {\n"
     "        n = g_api->sys_read(fd_in, xfer_buf, sizeof(xfer_buf));"),
]


def cui_has_no_yield(tmp, shim):
    """CUI (常駐シェル = SHELL_AS_APP 無し) には譲る呼び出しが 1 つも残らない。

    ホストの枠 (sh_truncation_host.c) は -DSHELL_AS_APP でしか組めないので、
    常駐側は贋 KAPI の呼び出し回数では見られない。代わりに cmd_script.c を
    2 通り前処理し、**cmd_script.c 由来の行** (行標識で切り分ける) に
    sys_yield が何回出るかを数える。ヘッダの KernelAPI 宣言は数に入らない。

    合格は「GUI で 1 回以上 / CUI で 0 回」。GUI 側を一緒に見るのは窓の較正
    — 0 と 0 を見て「CUI では出ない」と安心しないため。

    常駐シェルで譲ると exec_sys_yield は `hlt` 1 回 (PIT 1 tick = 10ms) で
    戻るだけなので、行ごとに呼ぶと 128 行のスクリプトに 1 秒以上足す。
    """
    counts = {}
    for name, extra in (("GUI", ["-DSHELL_AS_APP"]), ("CUI", [])):
        proc = subprocess.run(["gcc", "-E", *BASE_NOAPP, *extra, *shim, *INCLUDES,
                               str(ROOT / "userland/shell/cmd_script.c")],
                              cwd=ROOT, check=True, capture_output=True, text=True)
        cur = None
        n = 0
        for line in proc.stdout.splitlines():
            m = re.match(r'#\s+\d+\s+"([^"]*)"', line)
            if m:
                cur = m.group(1)
                continue
            if cur and cur.endswith("cmd_script.c") and "sys_yield" in line:
                n += 1
        counts[name] = n
    ok = counts["GUI"] >= 1 and counts["CUI"] == 0
    print("CUI YIELD PROBE gui=%d cui=%d %s"
          % (counts["GUI"], counts["CUI"], "PASS" if ok else "**FAIL**"), flush=True)
    return 0 if ok else 1


def run_mutations(tmp, shim):
    bad = 0
    for name, rel, old, new in MUTATIONS:
        target = ROOT / rel
        original = target.read_text(encoding="utf-8")
        if original.count(old) != 1:
            print("MUTATE %-22s SKIP (目印が %d か所)"
                  % (name, original.count(old)), flush=True)
            bad += 1
            continue
        try:
            target.write_text(original.replace(old, new, 1), encoding="utf-8")
            try:
                exe = build_host(tmp, shim, "mut-" + name)
            except subprocess.CalledProcessError:
                print("MUTATE %-22s RED (コンパイルが通らない)" % name, flush=True)
                continue
            rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60,
                                capture_output=True).returncode
            if rc == 0:
                print("MUTATE %-22s **GREEN のまま = 試験が規則を見ていない**"
                      % name, flush=True)
                bad += 1
            else:
                print("MUTATE %-22s RED (期待どおり落ちた)" % name, flush=True)
        finally:
            target.write_text(original, encoding="utf-8")
    return bad


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-sh-trunc-") as tmp:
        tmp = pathlib.Path(tmp)
        shim = write_shims(tmp)
        failed = 0

        exe = build_host(tmp, shim, "sh_truncation")
        print("HOST ILP32 GNU89 COMPILE PASS", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=30).returncode

        subprocess.run(["i386-elf-gcc", *BASE, "-O2", "-nostdlib",
                        "-mno-red-zone", "-fcommon", *shim, *INCLUDES,
                        "-c", str(HOST_SRC), "-o", str(tmp / "sh_truncation.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU89 COMPILE PASS", flush=True)
        print("EXIT sh_truncation_host=%d" % rc, flush=True)
        failed += rc != 0

        failed += cui_has_no_yield(tmp, shim)

        if "--mutate" in sys.argv:
            failed += run_mutations(tmp, shim)

        sys.exit(1 if failed else 0)
