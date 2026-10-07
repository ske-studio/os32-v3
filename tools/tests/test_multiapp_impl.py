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
    import argparse
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    includes = ["-I" + p for p in INC]
    with tempfile.TemporaryDirectory(prefix="os32-multiapp-impl-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "multiapp-impl"
        source = (ROOT / "exec/exec.c").read_text()
        poll = source.split('} else if (src == APP_RESUME_SRC_POLL) {', 1)[1].split('} else if (src == APP_RESUME_SRC_KBD)', 1)[0]
        (tmp / 'exec_resume_poll.inc').write_text(poll)
        kbd = source.split('} else if (src == APP_RESUME_SRC_KBD) {', 1)[1].split('} else {', 1)[0]
        (tmp / 'exec_resume_kbd.inc').write_text(kbd)
        includes.insert(0, '-I'+str(tmp))
        command = ["gcc", *FLAGS, "-O0", "-nostdlib", "-static", "-no-pie",
                        "-DKBD_INJECT_NO_IRQ_LOCK",
                        *includes, str(SRC), "-o", str(exe)]
        subprocess.run(command, cwd=ROOT, check=True)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        host32.run([str(exe)], cwd=ROOT, check=True, timeout=60)
        if args.mutate:
            old = 'if (kbd_inject_take_for((int)app_id, &ch))'
            assert poll.count(old) == 1
            (tmp / 'exec_resume_poll.inc').write_text(poll.replace(old, 'if (0)'))
            subprocess.run(command, cwd=ROOT, check=True)
            result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True, text=True)
            assert result.returncode != 0 and '22' in result.stdout and 'FAIL' in result.stdout, result.stdout
            print('RED runtime: real exec_resume WAIT_POLL input consumption')
            (tmp / 'exec_resume_poll.inc').write_text(poll)
            (tmp / 'exec_resume_kbd.inc').write_text(kbd.replace(
                'kbd_inject_take_for((int)app_id, &ch)', 'kbd_inject_take_for(appslot_gfx_owner(), &ch)'))
            subprocess.run(command, cwd=ROOT, check=True)
            result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True, text=True)
            assert result.returncode != 0 and 'FAIL F1 hidden shell resume cannot take q' in result.stdout, result.stdout
            print('RED runtime: resume must pass the actual recipient')
        subprocess.run(["i386-elf-gcc", *FLAGS, "-O2", *includes, "-c",
                        str(ROOT / "exec/appslot.c"),
                        "-o", str(tmp / "appslot.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
