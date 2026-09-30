"""N3: libos32host / wget・lpr・hclip・hdate のホスト TDD。

実物の userland/lib/host/libos32host.c と userland/cmds/{wget,lpr,hclip,hdate}.c
をそのまま取り込み、KAPI (host_lib_host.c) あるいは libos32host の関数
(host_cmd_host.c) だけを贋物に差し替えて回す。ホストのファイルシステムは
コマンド試験の一時ディレクトリだけを使い、配備・エミュレータには触らない。
記録は tools/tests/n3_tdd.md。

  python3 -B tools/tests/test_host_lib.py [--target]

--target を付けると実機と同じ i386-elf クロスコンパイラでも
libos32host.c と 4 コマンドが -Werror で通ることを確かめる。
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl=", "-D__OS32_USERLAND__"]
LIB_INC = ["-I" + str(ROOT / p) for p in ("include", "sdk/include",
                                          "sdk/include/os32", "userland/lib/host")]
CMD_INC = ["-I" + str(ROOT / "userland/lib/host")]

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt
TARGET_FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror", "-Wdeclaration-after-statement",
                "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
                "-Isdk/include/os32", "-Iuserland/lib", "-Iuserland/lib/host",
                "-I" + str(CROSS_DIR / "i386-elf/include")]

CMDS = ["WGET", "LPR", "HCLIP", "HDATE"]


def build_lib(tmp):
    exe = str(tmp / "host-lib-host")
    subprocess.run(["gcc", *FLAGS, *LIB_INC,
                    str(ROOT / "tools/tests/host_lib_host.c"), "-o", exe],
                   cwd=ROOT, check=True)
    print("HOST GNU89 -Werror compile PASS (real libos32host.c)", flush=True)
    return exe


def build_cmd(tmp, cmd):
    exe = str(tmp / ("host-cmd-" + cmd.lower()))
    subprocess.run(["gcc", *FLAGS, "-DHOST_TEST", "-DCMD_" + cmd, *CMD_INC,
                    str(ROOT / "tools/tests/host_cmd_host.c"), "-o", exe],
                   cwd=ROOT, check=True)
    print(f"HOST GNU89 -Werror compile PASS (real cmds/{cmd.lower()}.c)", flush=True)
    return exe


def target_compile(tmp):
    subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c",
                    "userland/lib/host/libos32host.c", "-o", str(tmp / "libos32host.o")],
                   cwd=ROOT, check=True)
    print("TARGET i386-elf -Werror compile PASS (libos32host.c)", flush=True)
    for c in ("wget", "lpr", "hclip", "hdate"):
        subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c",
                        "userland/cmds/%s.c" % c, "-o", str(tmp / (c + ".o"))],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf -Werror compile PASS (%s.c)" % c, flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-hostlib-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        lib = build_lib(tmp)
        rc = subprocess.run([lib], cwd=ROOT).returncode
        print(f"EXIT host_lib_host={rc}", flush=True)
        failed += rc != 0

        for cmd in CMDS:
            exe = build_cmd(tmp, cmd)
            rc = subprocess.run([exe], cwd=ROOT).returncode
            print(f"EXIT host_cmd_{cmd.lower()}={rc}", flush=True)
            failed += rc != 0

        if "--target" in sys.argv:
            target_compile(tmp)

        n = 1 + len(CMDS)
        print(f"SUMMARY {n - failed}/{n} PASS", flush=True)
        sys.exit(bool(failed))
