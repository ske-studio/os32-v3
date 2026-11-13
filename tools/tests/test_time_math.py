"""µs 時計の判定と算数 (kernel/time/time_math.c) のホスト試験。

記録: tools/tests/time_math_tdd.md
票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-5 (µs 時計 sys_time_now)

実物の kernel/time/time_math.c を 1 行も写さずに #include して回す。PIT も PIC も
触らないので模型は 1 つも要らない。

**ここで見る分岐は実機でも NP21/W でも撃ち分けられない**:
  - p1/p2 の 3 分岐 — 呼び出しから p1 読みまでに周期境界を越える機械があり、
    位相を待っても 0/0 と 0/1 を作り分けられない (往復 8 の中継 1)。
    kselftest 側は実 PIT の読出し・単調性・クランプ回数・1ms 待ちを観測する。
  - 71 分の桁あふれ — 起動から 71 分は NP21/W でも実機でも 1 度も回していない。

  python3 -B tools/tests/test_time_math.py            # ホストで全ケース
  python3 -B tools/tests/test_time_math.py --target   # + i386-elf で ktime.c も通す
  python3 -B tools/tests/test_time_math.py --mutate   # 否定側 (変異が RED になるか)
"""
CHECK = dict(name='time-math-host', tier=4,
    covers=['platform/pc98/clock_source.c', 'kernel/time/ktime.c', 'kernel/time/time_math.c', 'platform/pc98/pit_time.c', 'platform/pc98/pit_time.h', 'kernel/time/time_math.h', 'tools/tests/time_math_host.c', 'include/types.h', 'kernel/time/ktime.h', 'include/platform_irq.h', 'platform/pc98/pic.c', 'include/platform_timer.h', 'platform/pc98/clock.c', 'platform/pc98/pit.c', 'platform/pc98/timer.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.0)

from kernel_includes import include_flags
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/time_math_host.c"
SRC = ROOT / "kernel/time/time_math.c"

# カーネルと同じ i386-elf で通す実ソース。ktime.c まで入れるのは、
# スナップショットの採り方が time_decide / time_us_from64 を**実際に**呼ぶ形を
# 固定するため (64bit 除算は lib/divdi3.c の自前 helper で解く)。
TARGET_SRCS = [
    ("kernel/time/time_math.c", []),
    ("platform/pc98/pit_time.c", []),
    ("platform/pc98/clock_source.c", []),
    ("kernel/time/ktime.c", []),
]

CASES = ["decide_table", "us_math", "no_u32_overflow", "monotonic",
         "clamp"]

FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-D__cdecl="]
INCLUDES = include_flags(ROOT)

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
MUTATIONS = [
    (r"        out->tick  = t \+ 1;",
     "        out->tick  = t;",
     "p1 = 1 で tick を足さない (新しい周期の count を古い tick と組ませ、"
     "時刻が 1 周期ぶん戻る = 往復 2 の反例 1)"),
    (r"        return TIME_DECIDE_RETRY;",
     "        out->tick = t;\n        out->count = count;\n"
     "        return TIME_DECIDE_OK;",
     "p1 = 0 / p2 = 1 を**やり直さずに採用する** (境界のどちら側か分からない "
     "count を採るので最大 1 周期ずれる)"),
    (r"    unsigned long long cycles = raw \* reload \+ elapsed;",
     "    unsigned long long cycles = (unsigned int)raw * reload + elapsed;",
     "raw と reload の積を u32 に潰す"),
    (r"    unsigned int elapsed = \(count <= reload\) \? \(reload - count\) : 0;",
     "    unsigned int elapsed = count;",
     "経過ぶんを reload − count ではなく count そのものにする (時計が逆走する)"),
    (r"    if \(p1\) \{",
     "    if (p1 && p2) {",
     "p1 だけでは tick を足さない (p2 も 1 のときだけ足す。境界の直前で "
     "読んで p2 が 0 に落ち着いた場合に 1 周期ぶん戻る)"),
    (r"             \(cycles % clk_hz\) \* 1000000ULL / clk_hz;",
     "             (cycles % clk_hz) / clk_hz * 1000000ULL;",
     "剰余を先に割り端数を失う"),
    (r"    unsigned long long raw = \(\(unsigned long long\)raw_hi << BITS_PER_U32\) \| raw_lo;",
     "    unsigned long long raw = raw_lo; (void)raw_hi;",
     "raw の上位32bitを捨てる"),
    (r"        us = \(cycles / clk_hz\) \* 1000000ULL \+\n"
     r"             \(cycles % clk_hz\) \* 1000000ULL / clk_hz;",
     "        us = cycles * 1000000ULL / clk_hz;",
     "商・剰余に分けず中間積を64bitで溢れさせる"),
    (r"    if \(us < last\) \{\n        if \(out\) \*out = last;",
     "    if (us < last) {\n        if (out) *out = us;",
     "巻き戻りを数えるだけで**押さえない** (NP21/W は 8254 の再ロードと 8259 の "
     "IRR を原子的に模擬しないので、判定表を正しく通しても 1 万回読みが逆行する)"),
    (r"    if \(us < last\) \{",
     "    if (us <= last) {",
     "同じ µs を 2 回読んだだけでクランプに数える (回数が「模擬の粗さ」の"
     "指標にならなくなる)"),
]


def host_build(tmp, source_text=None, mutation_source=None):
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "time-math-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        for rel in ("kernel/time/time_math.c", "kernel/time/time_math.h",
                    "platform/pc98/pit_time.c", "platform/pc98/pit_time.h"):
            dest = src_dir / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(source_text if rel == mutation_source else (ROOT / rel).read_text())
        shim = src_dir / "harness.c"
        shim.write_text(HARNESS.read_text().replace('"../../', '"'))
        cmd = ["gcc", *FLAGS, "-I" + str(src_dir), *include_flags(ROOT), str(shim), "-o", str(exe)]
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

    bad = 0
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        rel = "platform/pc98/pit_time.c" if i <= 8 else "kernel/time/time_math.c"
        original = (ROOT / rel).read_text()
        mutated, n = re.subn(pattern, repl, original)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, mutated, rel)
        except subprocess.CalledProcessError:
            print(f"MUTATION {i} INVALID (compile): {why}", flush=True)
            bad += 1
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
    with tempfile.TemporaryDirectory(prefix="os32-time-math-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU11 -Werror compile PASS (real kernel/time/time_math.c)",
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
    argv = ['python3', '-B', 'tools/tests/test_time_math.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
