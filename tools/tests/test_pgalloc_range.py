"""A0 real-source ILP32 harness; no Make, emulator, deployment or libc needed."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement"]
# arch/x86 + platform/pc98: include/io.h は契約だけで、実装は固定名
# arch_io.h / platform_io.h を引く (順序 3)。build/config.mk の INC_COMMON と
# 同じものをここでも渡す。
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "arch/x86", "platform/pc98",
                      "kernel", "lib")]

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-a0-") as tmp:
        exe = pathlib.Path(tmp) / "pgalloc-range"
        subprocess.run(["gcc", *FLAGS, "-Wno-unused-function", "-O0",
                        "-finstrument-functions", "-ffunction-sections", "-Wl,--gc-sections", "-nostdlib", "-static", "-no-pie",
                        *INCLUDES, str(ROOT / "tools/tests/pgalloc_range_host.c"),
                        "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST ILP32 GNU89 COMPILE PASS", flush=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True, timeout=60)
        subprocess.run(["i386-elf-gcc", *FLAGS, "-O2", *INCLUDES,
                        "-c", str(ROOT / "kernel/pgalloc.c"),
                        "-o", str(pathlib.Path(tmp) / "pgalloc.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU89 -Werror COMPILE PASS", flush=True)
