"""H1: hsync の「同サイズ更新の検出」を実物のソースで確かめる。

票:   docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §8 の H1 行 / §9 の A01〜A13
記録: tools/tests/h1_tdd.md

tools/tests/hsync_h1_host.c が userland/system/hsync.c を 1 行も写さず
そのまま #include し、KernelAPI だけをオンメモリの贋ファイルシステムへ
差し替えて回す (模型ではない)。fs/hostdrv_stat_rules.inc (HostDrv の
stat 失敗の是正) も同じ翻訳単位で直接叩き、CRC は lib/crc32.c の既存
一括版と同じ実行ファイルにリンクして値の一致を見る。

  python3 -B tools/tests/test_hsync_h1.py [--target]

--target を付けると、実機と同じ i386-elf クロスコンパイラでも
hsync.c / lib/crc32.c / fs/hostdrvfs.c が -Werror で通ることを確かめる
([C1] C89/GNU89)。make・エミュレータ・実配備には一切触れない。

ビルドは 2 本:
  1. 通常      … A01/A02/A05〜A13
  2. CRC 贋物  … -DHSYNC_CRC_STUB。CRC が常に同値を返しても既定の
                 バイト比較が内容差を検出することの否定側 (A08)
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

HOST_FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
              "-Wdeclaration-after-statement",
              "-D__cdecl=", "-D__OS32_USERLAND__"]
HOST_INC = ["-I" + str(ROOT / p) for p in
            (".", "include", "sdk/include", "sdk/include/os32",
             "userland/lib", "userland/system", "lib")]

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt

TARGET_COMMON = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
                 "-fno-pie", "-fno-stack-protector", "-nostdlib",
                 "-mno-red-zone", "-fcommon", "-O2",
                 "-Wall", "-Wextra", "-Werror",
                 "-Wdeclaration-after-statement"]
# 外部プログラム (build/config.mk の PROGRAM_FLAGS と同じ形)
TARGET_USER = TARGET_COMMON + [
    "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
    "-Isdk/include/os32", "-Iuserland/lib",
    "-I" + str(CROSS_DIR / "i386-elf/include")]
# カーネル (build/config.mk の KERNEL_CFLAGS + INC_KERNEL)。hostdrvfs.c は
# 元から -Waddress-of-packed-member が出るので、そこだけ外して見る。
TARGET_KERNEL = TARGET_COMMON + [
    # arch/x86 + platform/pc98: include/io.h は契約だけで、実装は固定名
    # arch_io.h / platform_io.h を引く (順序 3)。
    "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
    "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
    "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet", "-Ifs",
    "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]


def check_depth_constant():
    """[C4]: hsync.c の HS_MAX_PATH_DEPTH は fs/vfs.h の VFS_MAX_PATH_DEPTH の写し。

    外部プログラムからは fs/vfs.h を引けないので写しを置くしかない。fs/vfs.c は
    上限を越えた要素を**黙って捨てる**ので、写しが本体より大きいと hsync は
    「化けたパス」を通してしまう (Codex 実装レビュー B2)。ここでずれを止める。
    """
    def grab(path, name):
        text = (ROOT / path).read_text()
        m = re.search(r"^#define\s+%s\s+(\d+)" % name, text, re.M)
        if not m:
            raise SystemExit("%s に %s が見つからない" % (path, name))
        return int(m.group(1))

    vfs = grab("fs/vfs.h", "VFS_MAX_PATH_DEPTH")
    hs = grab("userland/system/hsync.c", "HS_MAX_PATH_DEPTH")
    hsp = grab("userland/system/hsync_protect.inc", "HSP_MAX_DEPTH")
    if hs != vfs:
        raise SystemExit(
            "HS_MAX_PATH_DEPTH=%d が fs/vfs.h の VFS_MAX_PATH_DEPTH=%d と違う"
            % (hs, vfs))
    if hsp > vfs:
        raise SystemExit(
            "HSP_MAX_DEPTH=%d が VFS_MAX_PATH_DEPTH=%d より大きい" % (hsp, vfs))
    print("CONST COUPLING PASS (VFS_MAX_PATH_DEPTH=%d == HS_MAX_PATH_DEPTH, "
          "HSP_MAX_DEPTH=%d)" % (vfs, hsp), flush=True)


def build_host(tmp, stub):
    exe = tmp / ("hsync-h1-stub" if stub else "hsync-h1")
    cmd = ["gcc", *HOST_FLAGS, *HOST_INC]
    if stub:
        cmd.append("-DHSYNC_CRC_STUB")
    cmd += [str(ROOT / "tools/tests/hsync_h1_host.c"),
            str(ROOT / "lib/crc32.c"), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    print("HOST GNU89 -Werror COMPILE PASS%s (real hsync.c)"
          % (" [CRC stub]" if stub else ""), flush=True)
    return exe


def target_compile(tmp):
    subprocess.run(["i386-elf-gcc", *TARGET_USER, "-c",
                    "userland/system/hsync.c", "-o", str(tmp / "hsync.o")],
                   cwd=ROOT, check=True)
    print("TARGET i386-elf -Werror COMPILE PASS (hsync.c)", flush=True)
    subprocess.run(["i386-elf-gcc", *TARGET_KERNEL, "-c",
                    "lib/crc32.c", "-o", str(tmp / "crc32.o")],
                   cwd=ROOT, check=True)
    print("TARGET i386-elf -Werror COMPILE PASS (lib/crc32.c)", flush=True)
    subprocess.run(["i386-elf-gcc", *TARGET_KERNEL,
                    "-Wno-address-of-packed-member", "-c",
                    "fs/hostdrvfs.c", "-o", str(tmp / "hostdrvfs.o")],
                   cwd=ROOT, check=True)
    print("TARGET i386-elf -Werror COMPILE PASS (fs/hostdrvfs.c)", flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-hsync-h1-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        check_depth_constant()

        for stub in (False, True):
            exe = build_host(tmp, stub)
            rc = subprocess.run([str(exe)], cwd=ROOT, timeout=120).returncode
            print("EXIT hsync_h1_host%s=%d"
                  % ("_crcstub" if stub else "", rc), flush=True)
            failed += rc != 0

        if "--target" in sys.argv:
            target_compile(tmp)

        sys.exit(1 if failed else 0)
