"""B5: シェルの種別判定が「列挙の成否」を使わないこと / cp -r の宛先階層。

票:   docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md (H1) / Codex 実装レビュー 往復 3 の B5
記録: tools/tests/h1_tdd.md

tools/tests/fs_kind_host.c が実物の userland/shell/cmd_fs_shared.c と
userland/shell/cmd_file.c を 1 行も写さずそのまま #include し、KernelAPI と
shell.c 側の 2 本 (shell_print_help / shell_register_cmds) だけを贋物にして回す。

贋 FS は `sys_stat` が正しく答えるまま `sys_ls` だけを OS32_ERR_FULL /
OS32_ERR_IO にできる (1000 件超のディレクトリ / 途中で切れた列挙の再現)。
いちばん大事な検査は **cp -r の宛先階層**:

    cp -r /src /big   ->   /big/src/a.txt   (× /big/a.txt を上書き)

  python3 -B tools/tests/test_fs_kind.py [--target]

--target を付けると、実機と同じ i386-elf クロスコンパイラでも
cmd_fs_shared.c / cmd_file.c が -Werror で通ることを確かめる ([C1] C89/GNU89)。
make・エミュレータ・実配備には一切触れない。
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

HOST_FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
              "-Wdeclaration-after-statement",
              "-D__cdecl=", "-D__OS32_USERLAND__"]
HOST_INC = ["-I" + str(ROOT / p) for p in
            (".", "include", "sdk/include", "sdk/include/os32",
             "userland/lib", "userland/shell")]

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt

TARGET_FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-Wdeclaration-after-statement",
                "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
                "-Isdk/include/os32", "-Iuserland/lib", "-Iuserland/shell",
                "-I" + str(CROSS_DIR / "i386-elf/include")]


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-fs-kind-") as tmp:
        tmp = pathlib.Path(tmp)

        exe = tmp / "fs-kind"
        subprocess.run(["gcc", *HOST_FLAGS, *HOST_INC,
                        str(ROOT / "tools/tests/fs_kind_host.c"),
                        "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST GNU89 -Werror COMPILE PASS "
              "(real cmd_fs_shared.c + cmd_file.c)", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT fs_kind_host=%d" % rc, flush=True)

        if "--target" in sys.argv:
            for src in ("userland/shell/cmd_fs_shared.c",
                        "userland/shell/cmd_file.c"):
                subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", src,
                                "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                               cwd=ROOT, check=True)
                print("TARGET i386-elf -Werror COMPILE PASS (%s)" % src,
                      flush=True)

        sys.exit(rc)
