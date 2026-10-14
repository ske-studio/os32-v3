"""Read-only CRT diagnostic: design CRTFREQ_DESIGN.r2.md §3 tests ①–⑦.

Real kdebug/crt_diag.c, independent I/O shim and protected 1MB BIOS memory.
記録: tools/tests/crt_diag_tdd.md
Run: python3 -B tools/tests/test_crt_diag.py [--mutate]
"""
import os
import pathlib
import subprocess
import sys
import tempfile

import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "tools/tests/crt_diag_host.c"
TARGET_SRC = ROOT / "kdebug/crt_diag.c"
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror", "-O2"]
INCLUDES = ["-I" + str(ROOT / p) for p in
            ("include", "arch/x86", "platform/pc98", "sdk/include/os32", "lib")]


def host_cmd(exe):
    return ["gcc", *FLAGS, "-nostdlib", "-static", "-no-pie",
            "-I" + str(ROOT / "tools/tests/crt_diag_hostshim"),
            *INCLUDES, str(SRC), "-o", str(exe)]


MUTATIONS = [
    ("extra_out", "    r->hsync = (u8)inp(PEGC_HSYNC_PORT);",
     "    outpw(CRT_DIAG_SYSPORT_A, 0);\n    r->hsync = (u8)inp(PEGC_HSYNC_PORT);"),
    ("sdip_11", "i < CRT_DIAG_SDIP_COUNT;", "i < CRT_DIAG_SDIP_COUNT - 1;"),
    ("msw_8", "i < CRT_DIAG_MSW_COUNT;", "i < 8;"),
    ("missing_key", '" sysa="', '" ="'),
]


def one_mutation(item):
    name, old, new = item
    source = TARGET_SRC.read_text()
    if old and source.count(old) < 1:
        raise AssertionError("mutation anchor missing: " + name)
    with tempfile.TemporaryDirectory(prefix="crt-mut-") as td:
        exe = pathlib.Path(td) / "crt-host"
        tree = mutpar.build_in_tree(
            ROOT, td, {"kdebug/crt_diag.c": source.replace(old, new, 1) if old else source},
            [host_cmd(exe)], capture_output=True)
        result = host32.run([str(exe)], cwd=tree, capture_output=True, text=True)
    if result.returncode == 0:
        return "MUTATE " + name + " GREEN", 1
    # Compile errors/signals do not count as detection of these four defects.
    if result.returncode != 1 or "FAIL " not in result.stdout:
        raise AssertionError(name + ": unexpected failure " + str(result.returncode))
    return "MUTATE " + name + " RED: " + result.stdout.strip().splitlines()[-1], 0


def main():
    with tempfile.TemporaryDirectory(prefix="crt-diag-") as td:
        exe = pathlib.Path(td) / "crt-host"
        subprocess.run(host_cmd(exe), check=True)
        for runner in os.environ.get("HOST32_RUNNERS", "native qemu").split():
            result = host32.run([str(exe)], runner=runner)
            if result.returncode:
                return 1
            print("HOST CRT PASS runner=" + runner, flush=True)
        cross = pathlib.Path(os.environ.get("CROSS_DIR", "/home/hight/opt/cross")) / "bin/i386-elf-gcc"
        subprocess.run([str(cross), *FLAGS, *INCLUDES, "-c", str(TARGET_SRC),
                        "-o", str(pathlib.Path(td) / "crt_diag.o")], check=True)
        print("TARGET GNU11 -Werror PASS", flush=True)
        if "--mutate" in sys.argv:
            return int(bool(mutpar.run_with_control(one_mutation, MUTATIONS, ("control", "", ""))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
