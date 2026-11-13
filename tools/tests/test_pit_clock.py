"""PIT の分周 (platform/pc98/pit_math.c) のホスト試験。

記録: tools/tests/pit_clock_tdd.md
票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-0 (PIT の分周をクロック判定に合わせる)

実物の platform/pc98/pit_math.c を 1 行も写さずに #include して回す。算数だけなので
I/O もタイマも触らず、模型は 1 つも要らない。

**NP21/W は 1.9968MHz 固定なので、2.4576MHz 系の分岐はエミュレータでは
一度も通らない** — 実機 PC-9821Ra266 で tick が 8.125ms だった件がこれ。

  python3 -B tools/tests/test_pit_clock.py            # ホストで全ケース
  python3 -B tools/tests/test_pit_clock.py --target   # + i386-elf で idt.c も通す
  python3 -B tools/tests/test_pit_clock.py --mutate   # 否定側 (変異が RED になるか)
"""
CHECK = dict(name='pit-clock-host', tier=4,
    covers=['kdebug/kselftest/pit_setup.inc', 'include/time_units.h', 'arch/x86/kernel/idt.c', 'platform/pc98/pit_math.c', 'kernel/time/sysclk.c', 'tools/tests/pit_clock_host.c', 'include/platform_irq.h', 'platform/pc98/pic.c', 'include/platform_timer.h', 'platform/pc98/clock.c', 'platform/pc98/pit.c', 'platform/pc98/timer.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.2)

from kernel_includes import include_flags
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/pit_clock_host.c"
SRC = ROOT / "platform/pc98/pit_math.c"
SETUP = ROOT / "kdebug/kselftest/pit_setup.inc"
# カーネルと同じ i386-elf で通す実ソース。idt.c まで入れるのは、
# pit_init が pit_compute と sysclk_hz を**実際に**呼ぶ形を固定するため。
# STATIC_ASSERT (中間積が 32bit に収まるか) が効くのもこちら側。
TARGET_SRCS = [
    ("platform/pc98/pit_math.c", []),
    ("kernel/time/sysclk.c", []),
    ("arch/x86/kernel/idt.c", []),
    ("platform/pc98/pit.c", []),
    ("platform/pc98/clock.c", []),
    ("platform/pc98/timer.c", []),
]

CASES = ["selftest_setup", "both_clocks", "reject_hz", "reject_clock", "no_overflow",
         "real_hw_story"]

FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-D__cdecl="]
INCLUDES = include_flags(ROOT)

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (パターン, 置換, 説明)
MUTATIONS = [
    (r"reload = \(unsigned int\)raw;",
     "reload = (unsigned int)(SYSCLK_1997 / hz);",
     "クロックを見ずに 1.9968MHz で割る (= 直す前の姿。実機で tick 8.125ms)"),
    (r"    if \(hz != \(unsigned int\)PIT_HZ && hz != 1000u\) \{\n        return PIT_ERR_HZ;\n    \}\n",
     "",
     "100Hz 以外を受けてしまう (端数のリロード値が積まれ周期がずれる)"),
    (r"raw = \(clk_hz \+ \(unsigned long\)hz / 2\) / \(unsigned long\)hz;",
     "raw = clk_hz / (unsigned long)hz;",
     "最近接を切り捨てに戻す"),
    (r"hz != \(unsigned int\)PIT_HZ && hz != 1000u",
     "hz != (unsigned int)PIT_HZ",
     "1000Hz を断る"),
    (r"    if \(clk_hz != \(unsigned long\)SYSCLK_1997 &&\n"
     r"        clk_hz != \(unsigned long\)SYSCLK_2458\) \{\n"
     r"        return PIT_ERR_CLOCK;\n    \}\n",
     "",
     "知らないクロックで割る (0 除算・桁違いのリロード値)"),
    (r"period = \(reload \* PIT_PERIOD_SCALE\) / \(unsigned int\)\(clk_hz / PIT_CLOCK_DIV\);",
     "period = (reload * USEC_PER_SEC) / (unsigned int)clk_hz;",
     "約分せず 10^6 を掛ける (32bit で溢れて period_us が桁違いになる)"),
    (r"    out->valid = 0;\n",
     "    out->valid = 1;\n",
     "断ったのに valid を立てる (呼び出し側が古い値を本物と読む)"),
    (r"\(sysclk_hz\(\) \+ PIT_IRQ_HZ / 2\) / PIT_IRQ_HZ",
     "sysclk_hz() / PIT_IRQ_HZ",
     "自己試験の reload を切り捨てにする", SETUP),
    (r"p->reload \* PIT_PERIOD_SCALE",
     "(p->reload + 10) * PIT_PERIOD_SCALE",
     "自己試験の period の式を壊す", SETUP),
]


def host_build(tmp, source_text=None, setup_text=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。"""
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "pit-clock-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None or setup_text is not None:
        # 変異させた pit_math.c を一時の kernel/ に置いて、そちらを先に引かせる。
        mut = src_dir / SRC.relative_to(ROOT).parent
        mut.mkdir(parents=True, exist_ok=True)
        (mut / "pit_math.c").write_text(source_text if source_text is not None else SRC.read_text(encoding="utf-8"), encoding="utf-8")
        (mut / "pit_math.h").write_text(
            (ROOT / "platform/pc98/pit_math.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../platform/pc98/pit_math.c"', '"platform/pc98/pit_math.c"'),
            encoding="utf-8")
        (mut.parent.parent / "kdebug/kselftest").mkdir(parents=True, exist_ok=True)
        (mut.parent.parent / "kdebug/kselftest/pit_setup.inc").write_text(setup_text if setup_text is not None else SETUP.read_text(encoding="utf-8"), encoding="utf-8")
        shim.write_text(shim.read_text(encoding="utf-8").replace('"../../kdebug/kselftest/pit_setup.inc"', '"kdebug/kselftest/pit_setup.inc"'), encoding="utf-8")
        cmd = ["gcc", *FLAGS, "-I" + str(src_dir), "-I" + str(mut),
               *include_flags(ROOT), str(shim), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return exe


def run_cases(exe, cases):
    failed = 0
    for case in cases:
        rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
        print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def build_target(tmp):
    """カーネルと同じ i386-elf で arch/x86/kernel/idt.c ごと通す。"""
    for rel, extra in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror",
               "-D__KERNEL_BUILD__",
                *include_flags(ROOT),
               *extra, "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU11 -Werror PASS", flush=True)


def mutate(tmp):
    """実装を 1 か所ずつ壊して、どれも RED になることを見る。"""
    bad = 0
    for i, mutation in enumerate(MUTATIONS, 1):
        pattern, repl, why = mutation[:3]
        source = mutation[3] if len(mutation) == 4 else SRC
        original = source.read_text(encoding="utf-8")
        mutated, n = re.subn(pattern, repl, original)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, mutated if source == SRC else None,
                             mutated if source == SETUP else None)
        except subprocess.CalledProcessError:
            # 自己試験の変異は実行時 RED が必須。
            if source == SETUP:
                print(f"MUTATION {i} COMPILE FAIL: {why}", flush=True)
                bad += 1
                continue
            # 既存の STATIC_ASSERT で落ちる変異は compile RED。
            print(f"MUTATION {i} RED (compile): {why}", flush=True)
            continue
        hits = sum(subprocess.run([str(exe), c], cwd=ROOT,
                                  stderr=subprocess.DEVNULL).returncode != 0
                   for c in CASES)
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-pit-clock-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU11 -Werror compile PASS (real platform/pc98/pit_math.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_pit_clock.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
