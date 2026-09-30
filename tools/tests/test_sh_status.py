"""終了コード: 子の値と起動失敗をシェルまで届け、`$?` で読めることを押さえる。

票:   docs/archive/shell/TASK_EXIT_STATUS.md (受入 S1〜S15 / R1〜R2)
記録: tools/tests/sh_status_tdd.md

  python3 -B tools/tests/test_sh_status.py [--mutate]

同じ tools/tests/sh_status_host.c を **2 通り** にコンパイルして両方走らせる:

  -DSHELL_AS_APP … 端末の子 (要求表経由の起動)。終了コードは運べない (§2-6)。
  定義なし       … 常駐シェル (exec_run + exec_last_result、KAPI v55)。

種別ごとの写像はビルドで実装が違う (§2-2) ので、片方だけでは配線を見たことに
ならない。ホスト ILP32 GNU89 で走らせ、同じソースが外部プログラムと同じ形の
i386-elf-gcc でも通ることを別に見る ([C1] C89/GNU89)。
Make・エミュレータは使わない。

--mutate は**否定側**。この票の中心規則は

  (1) 種別は呼び手が決める / 値から作らない (exit(-2) は FAULT ではない)
  (2) PATH 走査は種別で止める (EXITED なら値が何でも止まる)
  (3) INVALID を見たら尽きても 126 (127 とは言わない)
  (4) `exit` は値を書いてから要求を立て、段ループを抜けた後も保持する
  (5) `set -e` は cmd_set の先頭で拾う / 入れ子で save-restore する
  (6) `$?` は名前の走査より手前で特別扱いする
  (7) 起動口は sh_exec_result 1 本 (exec_run の直後に exec_last_result)

なので、それぞれをわざと壊した版を作って**試験が落ちること**を見る。
GREEN のまま通ってしまう変異があれば、その規則を試験が見ていないということ。
"""
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
        "-fno-stack-protector", "-Wall", "-Wdeclaration-after-statement",
        "-D__OS32_USERLAND__"]
INCLUDES = ["-I" + str(ROOT / "sdk/include"), "-I" + str(ROOT / "sdk/include/os32"),
            "-I" + str(ROOT / "include"), "-I" + str(ROOT / "userland/shell"),
            "-I" + str(ROOT / "userland/lib"),
            # cmd_pci.c が "drivers/pci_decode.h" を引く (実ビルドの
            # PROGRAM_FLAGS の -I. と同じ)。L-A の lspci / pcidump。
            "-I" + str(ROOT),
            "-I" + str(ROOT / "userland/lib/filer")]
HOST_SRC = ROOT / "tools/tests/sh_status_host.c"

# 常駐 (定義なし) と sh.bin (-DSHELL_AS_APP) の 2 通り
VARIANTS = [("resident", []), ("sh_app", ["-DSHELL_AS_APP"])]

STRING_SHIM = """/* テスト用の薄い <string.h>。実体は sh_status_host.c にある。 */
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


def build_host(tmp, shim, name, extra):
    exe = tmp / name
    subprocess.run(["gcc", *BASE, *extra, "-O0", *shim, *INCLUDES,
                    "-nostdlib", "-static", "-no-pie",
                    str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
    return exe


def run_variants(tmp, shim, prefix):
    """2 通りとも走らせる。1 つでも落ちたら非 0 を返す。"""
    bad = 0
    for vname, extra in VARIANTS:
        exe = build_host(tmp, shim, "%s-%s" % (prefix, vname), extra)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        if rc != 0:
            bad += 1
    return bad


# ---------------------------------------------------------------------------
#  変異 (否定側)。(ファイル, 置換前, 置換後) — 置換前は 1 か所だけに出ること。
# ---------------------------------------------------------------------------
MUTATIONS = [
    # (1) 種別を値から作る版。exit(-2) が fault に、exit(-3) が not found に化ける。
    ("kind_from_value", "userland/shell/main.c",
     "    case EXEC_KIND_EXITED:\n"
     "        /* 0〜255 に丸める。負値 (exit(-1) = 255 など) も下位 8 ビットで\n"
     "         * そのまま識別できる。 */\n"
     "        return code & 0xFF;",
     "    case EXEC_KIND_EXITED:\n"
     "        if (code == EXEC_ERR_FAULT) return SH_STATUS_FAULT;\n"
     "        if (code == EXEC_ERR_NOT_FOUND) return SH_STATUS_NOTFOUND;\n"
     "        return code & 0xFF;"),

    # (2) PATH 走査を **値** で止める昔の姿 (実害 1 そのもの)。
    #     exit(-1) / exit(-3) の子が NOT_FOUND と同じ値になり、次の候補へ進む。
    ("scan_by_value", "userland/shell/sh_exec.inc",
     "    } else if (kind != EXEC_KIND_NOT_FOUND) {",
     "    } else if (kind != EXEC_KIND_NOT_FOUND &&\n"
     "               rc != EXEC_ERR_NOT_FOUND && rc != EXEC_ERR_GENERAL) {"),

    # (2b) PATH 走査の中でも値で判断する版。
    ("scan_by_value_path", "userland/shell/sh_exec.inc",
     "        if (*kind != EXEC_KIND_NOT_FOUND) {\n"
     "            return rc;\n"
     "        }",
     "        if (rc != EXEC_ERR_NOT_FOUND && rc != EXEC_ERR_GENERAL) {\n"
     "            return rc;\n"
     "        }"),

    # (3) INVALID を覚えない版 → 候補が尽きたとき 126 ではなく 127 になる。
    ("forget_invalid", "userland/shell/sh_exec.inc",
     "    if (saw_invalid) {\n"
     '        g_api->kprintf(ATTR_RED, "%s: not a valid executable\\n", argv[0]);\n'
     "        return SH_STATUS_NOEXEC;\n"
     "    }",
     "    if (0) {\n"
     "        return SH_STATUS_NOEXEC;\n"
     "    }"),

    # (3b) cwd の INVALID で止めてしまう版 (S2c の否定側)。
    ("invalid_stops_scan", "userland/shell/sh_exec.inc",
     "    if (kind == EXEC_KIND_INVALID) {\n"
     "        /* 受入 S2c: cwd の壊れた foo.bin で止めず、PATH の正しい方を試す\n"
     "         * (意図した変更 — 以前はここで止まっていた)。 */\n"
     "        saw_invalid = 1;\n"
     "    } else if (kind != EXEC_KIND_NOT_FOUND) {",
     "    if (0) {\n"
     "        saw_invalid = 1;\n"
     "    } else if (kind != EXEC_KIND_NOT_FOUND) {"),

    # (4) `exit` の handler が **要求を先に立てて値を捨てる** 版
    #     (往復 5 の注意 1 の否定側)。段ループを抜けた後に $? が 0 になる。
    ("exit_value_lost", "userland/shell/cmd_script.c",
     "    sh_status_set(code);        /* 先に値 */\n"
     "    sh_exit_code = code;\n"
     "    sh_exit_flag = 1;           /* そのあと要求 (script_exec / 段ループが見る) */\n"
     "    return code;",
     "    sh_exit_flag = 1;           /* 先に要求 */\n"
     "    sh_exit_code = 0;\n"
     "    sh_status_set(0);\n"
     "    return 0;"),

    # (4b) 段ループが `exit` を見ない版 (常駐で後段が走る、往復 2 所見 2)。
    ("pipe_ignores_exit", "userland/shell/main.c",
     "                if (sh_exit_flag) { status = sh_exit_code; break; }\n"
     "\n"
     "                if (sh_refused_peek()) { status = SH_STATUS_USAGE; break; }",
     "                if (0) { status = sh_exit_code; break; }\n"
     "\n"
     "                if (sh_refused_peek()) { status = SH_STATUS_USAGE; break; }"),

    # (4c) 要求と値を 1 つの変数にまとめた版 → `exit 0` で終われない。
    ("exit_flag_is_value", "userland/shell/cmd_script.c",
     "    sh_status_set(code);        /* 先に値 */\n"
     "    sh_exit_code = code;\n"
     "    sh_exit_flag = 1;           /* そのあと要求 (script_exec / 段ループが見る) */",
     "    sh_status_set(code);\n"
     "    sh_exit_code = code;\n"
     "    sh_exit_flag = code;"),

    # (4d) 常駐が `exit` の要求を下ろさない版 → 1 度打つと以降の行が全部
    #      「終了要求つき」になり、次のスクリプトが 1 行目で止まる。
    ("exit_flag_leaks", "userland/shell/main.c",
     "    if (g_exec_depth == 0) sh_exit_flag = 0;",
     "    if (0) sh_exit_flag = 0;"),

    # (5) `set -e` を cmd_set の先頭で拾わない版 → `-e: not set` に落ちる。
    ("set_e_not_caught", "userland/shell/cmd_env.c",
     '    if (str_eq(argv[1], "-e") || str_eq(argv[1], "+e")) {\n'
     "        script_errexit_set(argv[1][0] == '-');\n"
     "        return 0;\n"
     "    }",
     "    if (0) {\n"
     "        return 0;\n"
     "    }"),

    # (5b) errexit を入れ子で戻さない版 → 内側の `set -e` が外へ漏れる。
    ("errexit_not_restored", "userland/shell/cmd_script.c",
     "    script_errexit = saved_errexit;",
     "    script_errexit = script_errexit;"),

    # (5c) errexit が失敗行で止めない版。
    ("errexit_no_abort", "userland/shell/cmd_script.c",
     "        if (script_errexit && status != SH_STATUS_OK) {",
     "        if (0 && status != SH_STATUS_OK) {"),

    # (6) `$?` を名前の走査より **後ろ** で見る版 → `?` が変数名として扱われ、
    #     `set ?=99` があるとそちらが出る / 無ければ空になる。
    ("status_expand_late", "userland/shell/cmd_env.c",
     "            if (src[si] == '?') {\n"
     "                char num[12];",
     "            if (0) {\n"
     "                char num[12];"),

    # (7) 起動口が記録を読まない版 (exec_run の戻り値から種別を決め打ち)。
    ("no_last_result", "userland/shell/sh_exec.inc",
     "    rc = g_api->exec_run(cmdline);\n"
     "    if (g_api->exec_last_result(&k, &c) != 0) k = EXEC_KIND_NONE;",
     "    rc = g_api->exec_run(cmdline);\n"
     "    k = (rc == EXEC_ERR_NOT_FOUND) ? EXEC_KIND_NOT_FOUND : EXEC_KIND_EXITED;\n"
     "    c = rc;\n"
     "    if (0) (void)g_api->exec_last_result(&k, &c);"),

    # (7b) 記録が無いときに「成功」に倒す版 (S15 の否定側)。
    ("stale_record_ok", "userland/shell/sh_exec.inc",
     "    if (k == EXEC_KIND_NONE) { k = EXEC_KIND_GENERAL; c = 0; }",
     "    if (k == EXEC_KIND_NONE) { k = EXEC_KIND_EXITED; c = 0; }"),

    # (8) 組み込みの結果を捨てる版 (E2 の否定側)。
    ("builtin_result_dropped", "userland/shell/sh_exec.inc",
     "            return g_cmds[j].handler(argc, argv);",
     "            (void)g_cmds[j].handler(argc, argv);\n"
     "            return SH_STATUS_OK;"),

    # (8b) handler に届かない行を 0 にする版 (S4 / set -e のすり抜け)。
    ("unreached_is_zero", "userland/shell/main.c",
     "        if (argc < 0) {\n"
     "            /* 構文エラー / リダイレクト先が開けない — handler へ届かない\n"
     "             * (票 §2-3 の表)。 */\n"
     "            status = SH_STATUS_USAGE;",
     "        if (argc < 0) {\n"
     "            status = SH_STATUS_OK;"),

    # (8c) command not found を 0 にする版。
    ("notfound_is_zero", "userland/shell/sh_exec.inc",
     '    g_api->kprintf(ATTR_RED, "%s: command not found\\n", argv[0]);\n'
     "    return SH_STATUS_NOTFOUND;",
     '    g_api->kprintf(ATTR_RED, "%s: command not found\\n", argv[0]);\n'
     "    return SH_STATUS_OK;"),

    # (9) `source` が最後の行の値を返さない版 (一律 0 = 直す前の姿)。
    ("source_always_zero", "userland/shell/cmd_script.c",
     "        if (script_exec(&status)) result = SCRIPT_ERR_REFUSED;\n"
     "        else result = status;",
     "        if (script_exec(&status)) result = SCRIPT_ERR_REFUSED;\n"
     "        else result = 0;"),

    # (10) `exit` の引数検査を外す版 → `exit abc` が 0 で終了要求を立てる。
    ("exit_arg_unchecked", "userland/shell/main.c",
     "        if (*s < '0' || *s > '9') {\n"
     '            g_api->kprintf(ATTR_RED, "exit: %s: numeric argument required\\n",\n'
     "                           argv[1]);\n"
     "            return -1;\n"
     "        }",
     "        if (*s < '0' || *s > '9') {\n"
     "            break;\n"
     "        }"),

    # (11) profile の結果を `$?` に残す版 (§2-5-1 の否定側)。
    ("profile_leaks_status", "userland/shell/cmd_script.c",
     "    sh_status_set(SH_STATUS_OK);\n"
     "}",
     "    if (0) sh_status_set(SH_STATUS_OK);\n"
     "}"),

    # (12) パイプラインの値を最初の段にする版。
    ("pipe_first_stage", "userland/shell/main.c",
     "                status = execute_single(seg_buf + i * CMD_BUF_SIZE);",
     "                if (i == 0) status = execute_single(seg_buf + i * CMD_BUF_SIZE);\n"
     "                else (void)execute_single(seg_buf + i * CMD_BUF_SIZE);"),

    # (13) `if` が偽のときに前の値を返す版 (§2-3: 偽は 0)。
    ("if_false_keeps_status", "userland/shell/cmd_script.c",
     "        return execute_command(cmd_buf);\n"
     "    }\n"
     "    return SH_STATUS_OK;",
     "        return execute_command(cmd_buf);\n"
     "    }\n"
     "    return sh_status_get();"),
]


def host_cmd(shim, exe, extra):
    return ["gcc", *BASE, *extra, "-O0", *shim, *INCLUDES,
            "-nostdlib", "-static", "-no-pie", str(HOST_SRC), "-o", str(exe)]


def one_mutation(item):
    """変異 1 本を一時ディレクトリの写しで 2 通り組んで回す (実物は読むだけ)。
    (印字, 見逃し) を返す。"""
    name, rel, old, new = item
    original = (ROOT / rel).read_text(encoding="utf-8")
    if original.count(old) != 1 and old:
        return ("MUTATE %-24s SKIP (目印が %d か所)"
                % (name, original.count(old)), 1)
    with tempfile.TemporaryDirectory(prefix="os32-sh-status-mut-") as td:
        td = pathlib.Path(td)
        shim = write_shims(td)
        edits = {rel: original.replace(old, new, 1)}
        tree = mutpar.mutant_tree(
            ROOT, td / "tree", edits,
            gcc_cmds=[host_cmd(shim, td / "dep", extra)
                      for _, extra in VARIANTS])
        red = False
        compiled = False
        for vname, extra in VARIANTS:
            exe = td / ("mut-%s-%s" % (name, vname))
            try:
                subprocess.run(mutpar.rebase(host_cmd(shim, exe, extra),
                                             ROOT, tree),
                               cwd=str(tree), check=True)
            except subprocess.CalledProcessError:
                red = True
                continue
            compiled = True
            rc = subprocess.run([str(exe)], cwd=str(tree), timeout=60,
                                capture_output=True).returncode
            if rc != 0:
                red = True
    if red:
        return ("MUTATE %-24s RED (期待どおり落ちた%s)"
                % (name, "" if compiled else " — コンパイル不能"), 0)
    return ("MUTATE %-24s **GREEN のまま = 試験が規則を見ていない**" % name,
            1)


def run_mutations(tmp, shim):
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    check-par で回せる)。tmp / shim は使わない (変異ごとに写しの中で作る)。"""
    return mutpar.run_with_control(
        one_mutation, MUTATIONS, ("control", "userland/shell/main.c", "", ""))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-sh-status-") as tmp:
        tmp = pathlib.Path(tmp)
        shim = write_shims(tmp)
        failed = 0

        for vname, extra in VARIANTS:
            exe = build_host(tmp, shim, "sh_status-" + vname, extra)
            print("HOST ILP32 GNU89 COMPILE PASS (%s)" % vname, flush=True)
            rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
            print("EXIT sh_status_host(%s)=%d" % (vname, rc), flush=True)
            failed += rc != 0

            subprocess.run(["i386-elf-gcc", *BASE, *extra, "-O2", "-nostdlib",
                            "-mno-red-zone", "-fcommon", *shim, *INCLUDES,
                            "-c", str(HOST_SRC),
                            "-o", str(tmp / ("sh_status-%s.o" % vname))],
                           cwd=ROOT, check=True)
            print("TARGET i386-elf GNU89 COMPILE PASS (%s)" % vname, flush=True)

        if "--mutate" in sys.argv:
            failed += run_mutations(tmp, shim)

        sys.exit(1 if failed else 0)
