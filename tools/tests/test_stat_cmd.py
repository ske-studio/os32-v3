"""`stat` コマンド (userland/cmds/stat.c) のホスト TDD。

実物の stat.c をそのまま取り込み、KAPI の sys_stat だけを表に差し替えて
回す。ホストのファイルシステム・配備・エミュレータには一切触らない。
記録は tools/tests/stat_cmd_tdd.md。

  python3 -B tools/tests/test_stat_cmd.py [--target] [case ...]
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

CASES = [
    "dev",        # st_dev の生値と復号 (hd0 / fd0 / 未知の種別 / 0)
    "fields",     # 種別・サイズ・st_ino・mode・時刻
    "notfound",   # エラー行と終了コード 1
    "multi",      # 複数引数、1 つ落ちても続ける
    "usage",      # 引数無し
]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl=",
         "-D__OS32_USERLAND__"]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "sdk/include",
                                           "sdk/include/os32")]

# 実機と同じフラグ (build/config.mk の PROGRAM_FLAGS) でも通ること。
CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
TARGET_FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-Wdeclaration-after-statement", "-D__OS32_USERLAND__",
                "-I.", "-Iinclude", "-Isdk/include", "-Isdk/include/os32",
                "-Iuserland/lib", "-I" + str(CROSS_DIR / "i386-elf/include")]


def build(tmp):
    exe = str(tmp / "stat-cmd-host")
    subprocess.run(["gcc", *FLAGS, *INCLUDES,
                    str(ROOT / "tools/tests/stat_cmd_host.c"), "-o", exe],
                   cwd=ROOT, check=True)
    print("HOST GNU89 -Werror compile PASS (real userland/cmds/stat.c)",
          flush=True)
    return exe


def target_compile(tmp):
    subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c",
                    "userland/cmds/stat.c", "-o", str(tmp / "stat.o")],
                   cwd=ROOT, check=True)
    print("TARGET i386-elf GNU89 -Werror compile PASS (stat.c)", flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-statcmd-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = build(tmp)
        if "--target" in sys.argv:
            target_compile(tmp)
        cases = [x for x in sys.argv[1:] if not x.startswith("--")] or CASES
        failed = 0
        for case in cases:
            rc = subprocess.run([exe, case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
