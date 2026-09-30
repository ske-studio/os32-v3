"""K5a multi-app state model, ILP32 GNU11; no Make, emulator or libc needed.

Ticket: docs/archive/gui_v13/TASK_K5_multiapp.md (stage K5a, item 8).
Log:    tools/tests/multiapp_model_tdd.md

Same harness shape as test_pgalloc_range.py: build the host C file as a
freestanding ILP32 binary, run it, then prove the same source also compiles
with the cross compiler under the kernel's flags so it can be lifted into
kernel/ in stage K5b without a dialect surprise ([C1] GNU11).
"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
SRC = ROOT / "tools/tests/multiapp_model_host.c"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-multiapp-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "multiapp-model"
        subprocess.run(["gcc", *FLAGS, "-O0", "-nostdlib", "-static", "-no-pie",
                        str(SRC), "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True, timeout=60)
        subprocess.run(["i386-elf-gcc", *FLAGS, "-O2", "-c", str(SRC),
                        "-o", str(tmp / "multiapp_model.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
