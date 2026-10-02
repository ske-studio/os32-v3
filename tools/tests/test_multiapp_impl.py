"""K5b-K: the real AppSlot code (exec/appslot.c) under K5a's 84 checks.

Ticket: docs/archive/gui_v13/TASK_K5B_kernel.md (host tests, 3rd item)
Log:    tools/tests/k5b_kernel_tdd.md

K5a's tools/tests/multiapp_model_host.c hand-wrote the state machine as a
model. This harness compiles the shipped kernel source instead
(exec/appslot.c, included verbatim by multiapp_impl_host.c) and runs the same
numbered checks against it, plus case 17 for the OP_WAIT mark (C5/C6) and
case 19 for K7's second park point (WAIT_KEY / parked_from_kbd), which also
pulls in kernel/kbd_inject.c verbatim.  -DKBD_INJECT_NO_IRQ_LOCK swaps that
unit's cli/popfl for an empty lock: CPL=3 cannot execute them.

Same shape as test_multiapp_model.py / test_pgalloc_range.py: build ILP32
freestanding, run it, then prove the same source compiles with the cross
compiler under the kernel's flags ([C1] GNU11).
"""
import host32
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
INC = [str(ROOT / p) for p in ("include", "kernel", "lib", "exec", "fs",
                               "sdk/include/os32")]
SRC = ROOT / "tools/tests/multiapp_impl_host.c"

if __name__ == "__main__":
    includes = ["-I" + p for p in INC]
    with tempfile.TemporaryDirectory(prefix="os32-multiapp-impl-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "multiapp-impl"
        subprocess.run(["gcc", *FLAGS, "-O0", "-nostdlib", "-static", "-no-pie",
                        "-DKBD_INJECT_NO_IRQ_LOCK",
                        *includes, str(SRC), "-o", str(exe)],
                       cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        host32.run([str(exe)], cwd=ROOT, check=True, timeout=60)
        subprocess.run(["i386-elf-gcc", *FLAGS, "-O2", *includes, "-c",
                        str(ROOT / "exec/appslot.c"),
                        "-o", str(tmp / "appslot.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
