"""T9-K: 起動要求表を実物の exec/launch.c で確かめる。

契約: include/launch.h (起動要求表)
記録: tools/tests/t9_tdd.md
--mutate は子の回収・DONE の受渡し・孤児回収の故障を実ソースへ注入する。

test_con_sink.py と同じ様式 — ホスト ILP32 GNU11 で走らせたあと、同じソースが
カーネルと同じフラグの i386-elf-gcc -Werror でも通ることを別に見る
([C1] GNU11)。Make・エミュレータ・libc は使わない。

ホスト側は exec/appslot.c と exec/launch.c を tools/tests/launch_host.c が
そのまま #include する (模型ではない)。カーネル帯の代わりに要るのは所有者
(res_owner_get/set)・GUI 判定 (con_sink_is_enabled)・kstrncpy の 3 つだけ。
"""
CHECK = dict(name='launch-host', tier=1,
    covers=['tools/tests/host32.py', 'exec/launch.c', 'tools/tests/launch_host.c', 'exec/appslot.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=1.447408)

from kernel_includes import include_flags
import host32
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
INCLUDES = include_flags(ROOT)
HOST_SRC = ROOT / "tools/tests/launch_host.c"
KERNEL_SRC = ROOT / "exec/launch.c"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-launch-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / "launch"
        def build(source):
            subprocess.run(["gcc", *FLAGS, "-O0", "-D__KERNEL_BUILD__", *INCLUDES,
                            "-nostdlib", "-static", "-no-pie",
                            f'-DLAUNCH_SOURCE="{source}"',
                            str(HOST_SRC), "-o", str(exe)], cwd=ROOT, check=True)
        build(KERNEL_SRC)
        print("HOST ILP32 GNU11 COMPILE PASS", flush=True)
        rc = host32.run([str(exe)], cwd=ROOT, timeout=30).returncode
        subprocess.run(["i386-elf-gcc", *FLAGS, "-D__KERNEL_BUILD__",
                        *INCLUDES, "-O2", "-c", str(KERNEL_SRC),
                        "-o", str(tmp / "launch.o")], cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
        print("EXIT launch_host=%d" % rc, flush=True)
        if rc:
            sys.exit(rc)
        if "--mutate" in sys.argv:
            original = KERNEL_SRC.read_text()
            mutants = [
                ("child retained", "r->child = 0;                   /* 再利用",
                 "(void)r;                       /* 再利用", "2s "),
                ("DONE repeated",
                 "if (r->phase == LAUNCH_PHASE_DONE || r->phase == LAUNCH_PHASE_FAILED)",
                 "if (r->phase == LAUNCH_PHASE_FAILED)", "2u "),
                ("orphan not released", "if (r->requester == LAUNCH_REQ_ORPHAN) {",
                 "if (0) {", "8f "),
                ("orphan KILL lost",
                 "r->kind = LAUNCH_KIND_KILL;\n            r->arg = r->child;",
                 "r->kind = LAUNCH_KIND_NONE;\n            r->arg = r->child;", "5e "),
                ("DONE stolen", "if (r->requester != res_owner_get()) return OS32_ERR_INVAL;",
                 "if (r->requester != res_owner_get() && r->phase != LAUNCH_PHASE_DONE) "
                 "return OS32_ERR_INVAL;", "2s2 "),
            ]
            for label, old, new, expected in mutants:
                assert original.count(old) == 1, label
                source = tmp / "launch_mutant.c"
                source.write_text(original.replace(old, new, 1))
                build(source)
                result = host32.run([str(exe)], cwd=ROOT, timeout=30,
                                    capture_output=True, text=True)
                assert result.returncode == 1 and "  FAIL " + expected in result.stdout, (
                    label, result.returncode, result.stdout, result.stderr)
                print("MUTATE RED " + label + " (" + expected.strip() + ")", flush=True)
        sys.exit(rc)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_launch.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
