"""TASK_FS_TYPE §3: 種別が「分からない」とき cp / mv / rm が断ること (受け手の試験)。

票:   docs/archive/shell/TASK_FS_TYPE.md §3 (fs_is_dir は「不明」を運べない)
記録: tools/tests/fs_kind_callers_tdd.md

tools/tests/fs_kind_callers_host.c が実物の userland/shell/cmd_fs_shared.c と
userland/shell/cmd_file.c を 1 行も写さずそのまま #include し、KernelAPI と
shell.c 側の 2 本 (shell_print_help / shell_register_cmds) だけを贋物にして回す。
贋 FS は fs_kind_host.c と共有する (tools/tests/fs_kind_fake.h)。

sys_stat と sys_ls の両方に OS32_ERR_IO を注入して「種別が分からない」を作り、
cmd_file.c の呼び出し元 5 箇所 (cp の宛先 / cp の入力 / mv の FS またぎ /
mv の宛先 / rm) が**断りを出し、open / mkdir / rename / unlink を呼ばない**
ことを見る。いちばん重い反例は

    cp -r /src /u  (/u は読めないディレクトリ)  ->  /u/a.txt を上書き

6 章は別の欠陥 (継承バグ台帳): **`cp -r` が失敗時に空のディレクトリを
残す**。do_copy_recursive_impl は sys_mkdir(dst) を件数の確認より前に呼び、
しかも戻り値を見ていなかった。収集してから mkdir する / 戻り値を見る /
既に在る**ディレクトリ**への上書きコピーだけは通す、の 3 つを見る。

  python3 -B tools/tests/test_fs_kind_callers.py [--target] [--mutate]

--target を付けると、実機と同じ i386-elf クロスコンパイラでも
cmd_fs_shared.c / cmd_file.c が -Werror で通ることを確かめる ([C1] GNU11)。
--mutate は**否定側**。mkdir を収集の前へ戻した版 / 戻り値を見ない版 /
EXIST を型を見ずに通す版 / 列挙の失敗を無視する版を作り、この試験が
ちゃんと RED になることを見る。make・エミュレータ・実配備には一切触れない。
"""
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]

HOST_FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
              "-D__cdecl=", "-D__OS32_USERLAND__"]
HOST_INC = ["-I" + str(ROOT / p) for p in
            (".", "include", "sdk/include", "sdk/include/os32",
             "userland/lib", "userland/shell")]

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt

TARGET_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
                "-Isdk/include/os32", "-Iuserland/lib", "-Iuserland/shell",
                "-I" + str(CROSS_DIR / "i386-elf/include")]


# 否定側。cmd_file.c の写し (一時ディレクトリの木) を書き換えて、この試験が RED になることを見る。
MKDIR_BLOCK = r"""    rc = g_api->sys_mkdir(dst);
    if (rc != 0 && !(rc == OS32_ERR_EXIST && fs_path_kind(dst) == FS_KIND_DIR)) {
        g_api->kprintf(ATTR_RED, "cp -r: cannot create directory '%s': %s\n",
                       dst, fs_strerror(rc));
        g_api->mem_free(local_entries);
        return SH_STATUS_ERROR;
    }
"""

LS_BLOCK = r"""    rc = os32_ls(src, collect_entries_cb, (void *)0);
    if (rc < 0) {
        g_api->kprintf(ATTR_RED, "cp -r: cannot read directory '%s': %s\n",
                       src, fs_strerror(rc));
        g_api->mem_free(local_entries);
        return SH_STATUS_ERROR;
    }
"""

MUTATIONS = [
    # 変異 1 = 欠陥そのもの: mkdir を収集の**前**へ戻す。
    # 件数超過や列挙の失敗で引き返す経路が空のディレクトリを残す。
    ("mkdir_before_collect",
     "    /* **\u53ce\u96c6\u304c\u5148\u3001mkdir \u306f\u5f8c**\u3002",
     "    g_api->sys_mkdir(dst);\n    /* **\u53ce\u96c6\u304c\u5148\u3001mkdir \u306f\u5f8c**\u3002"),
    # 変異 2 = mkdir の戻り値を見ない版 (作れていないのに中へ進む)。
    ("mkdir_ret_ignored", MKDIR_BLOCK, "    (void)g_api->sys_mkdir(dst);\n"),
    # 変異 3 = EXIST を型を見ずに通す版 (同名のファイルへ展開する)。
    ("exist_type_not_checked", MKDIR_BLOCK,
     MKDIR_BLOCK.replace(
         "if (rc != 0 && !(rc == OS32_ERR_EXIST && fs_path_kind(dst) == FS_KIND_DIR)) {",
         "if (rc != 0 && rc != OS32_ERR_EXIST) {")),
    # 変異 4 = 列挙の失敗を無視する版 (空の宛先を作って終わる)。
    ("ls_err_ignored", LS_BLOCK,
     "    rc = os32_ls(src, collect_entries_cb, (void *)0);\n"
     "    (void)rc;\n"),
]


MUT_TARGET = "userland/shell/cmd_file.c"


def host_cmd(exe):
    return ["gcc", *HOST_FLAGS, *HOST_INC,
            str(ROOT / "tools/tests/fs_kind_callers_host.c"), "-o", str(exe)]


def one_mutation(item):
    """変異 1 本を一時ディレクトリの写しで組んで回す。(印字, 見逃し) を返す。"""
    name, old, new = item
    original = (ROOT / MUT_TARGET).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-24s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-fs-kind-callers-mut-") as td:
        exe = pathlib.Path(td) / ("mut-" + name)
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {MUT_TARGET: original.replace(old, new, 1)},
                [host_cmd(exe)])
        except subprocess.CalledProcessError:
            return "MUTATE %-24s RED (コンパイルが通らない)" % name, 0
        out = subprocess.run([str(exe)], cwd=str(tree), timeout=120,
                             capture_output=True)
    if out.returncode == 0:
        return ("MUTATE %-24s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    fails = out.stdout.decode("utf-8", "replace").count("FAIL ")
    return "MUTATE %-24s RED (期待どおりに落ちた: FAIL %d 件)" % (name, fails), 0


def run_mutations(tmp):
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    実物のソースは読むだけ — check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, MUTATIONS, ("control", "", ""))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-fs-kind-callers-") as tmp:
        tmp = pathlib.Path(tmp)

        exe = tmp / "fs-kind-callers"
        subprocess.run(["gcc", *HOST_FLAGS, *HOST_INC,
                        str(ROOT / "tools/tests/fs_kind_callers_host.c"),
                        "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST GNU11 -Werror COMPILE PASS "
              "(real cmd_fs_shared.c + cmd_file.c)", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT fs_kind_callers_host=%d" % rc, flush=True)

        if "--target" in sys.argv:
            for src in ("userland/shell/cmd_fs_shared.c",
                        "userland/shell/cmd_file.c"):
                subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", src,
                                "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                               cwd=ROOT, check=True)
                print("TARGET i386-elf -Werror COMPILE PASS (%s)" % src,
                      flush=True)

        if "--mutate" in sys.argv:
            rc += run_mutations(tmp)

        sys.exit(1 if rc else 0)
