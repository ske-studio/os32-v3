"""票 B8 / P1-4: HostDrv の OPEN 失敗を「不存在」と読み替えない。

票:   docs/archive/shell/TASK_FS_TYPE.md §2
記録: tools/tests/b8_tdd.md §5

tools/tests/b8_hostdrv_host.c が **実物の fs/hostdrvfs.c** をそのまま
#include し、include/io.h だけを tools/tests/hostdrv_hostshim/io.h で
差し替える (特権命令のインライン asm はホストで走らないため)。
ハイパーコールのコマンド列が完成した時点で**贋の NP21/W** が応答を書くので、
IRP_MJ_CREATE が返す NTSTATUS を 1 つずつ指定して
`hdrv_get_file_size()` などを**直接通す**ことができる。

前の回は判定を純関数に切り出してそこだけ試験したため、
**その手前の hostdrv_create() の失敗が全部 NOTFOUND に畳まれている**ことを
見逃した。純関数だけでは足りない。

  python3 -B tools/tests/test_b8_hostdrv.py [--target]

--target を付けると fs/hostdrvfs.c が実ビルドの素性でも通ることを見る。
make・エミュレータ・実配備には一切触れない。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

# u32 は unsigned long (include/types.h)。Np2* 構造体のオフセットが変わるので
# **必ず ILP32 で組む**。
HOST_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
              "-fno-stack-protector", "-nostdlib", "-static", "-O1",
              "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter", "-Wno-sign-compare",
              "-Wno-address-of-packed-member",
              "-D__cdecl="]
# hostdrv_hostshim は**必ず先頭** — include/io.h を差し替えるため。
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("tools/tests/hostdrv_hostshim",
                      "include", "fs", "lib", "kernel", "drivers",
                      "sdk/include/os32")]
SRC = ROOT / "tools/tests/b8_hostdrv_host.c"

TARGET_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2", "-Wall",
                "-Wno-address-of-packed-member",
                # arch/x86 + platform/pc98: include/io.h は契約だけで、実装は
                # 固定名 arch_io.h / platform_io.h を引く (順序 3)。
                "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
                "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
                "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet",
                "-Ifs", "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-b8hd-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "b8-hostdrv"
        subprocess.run(["gcc", *HOST_FLAGS, *INCLUDES, str(SRC), "-o", str(exe)],
                       cwd=ROOT, check=True)
        print("HOST GNU11 -Werror COMPILE PASS (real fs/hostdrvfs.c)", flush=True)

        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=120).returncode
        print("EXIT b8_hostdrv_host=%d" % rc, flush=True)

        if "--target" in sys.argv:
            subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", "fs/hostdrvfs.c",
                            "-o", str(tmp / "hostdrvfs.o")], cwd=ROOT, check=True)
            print("TARGET i386-elf COMPILE PASS (fs/hostdrvfs.c)", flush=True)

        sys.exit(rc)
