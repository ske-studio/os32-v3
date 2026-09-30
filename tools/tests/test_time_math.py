"""µs 時計の判定と算数 (kernel/time_math.c) のホスト試験。

記録: tools/tests/time_math_tdd.md
票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-5 (µs 時計 sys_time_now)

実物の kernel/time_math.c を 1 行も写さずに #include して回す。PIT も PIC も
触らないので模型は 1 つも要らない。

**ここで見る分岐は実機でも NP21/W でも撃ち分けられない**:
  - p1/p2 の 3 分岐 — 呼び出しから p1 読みまでに周期境界を越える機械があり、
    位相を待っても 0/0 と 0/1 を作り分けられない (往復 8 の中継 1)。
    kselftest 側は入力列を注入する hook で同じ表を踏む。
  - 71 分の桁あふれ — 起動から 71 分は NP21/W でも実機でも 1 度も回していない。

  python3 -B tools/tests/test_time_math.py            # ホストで全ケース
  python3 -B tools/tests/test_time_math.py --target   # + i386-elf で ktime.c も通す
  python3 -B tools/tests/test_time_math.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/time_math_host.c"
SRC = ROOT / "kernel/time_math.c"

# カーネルと同じ i386-elf で通す実ソース。ktime.c まで入れるのは、
# スナップショットの採り方が time_decide / time_us_from を**実際に**呼ぶ形を
# 固定するため (u64 が libgcc 経由で解けることもここで分かる)。
TARGET_SRCS = [
    ("kernel/time_math.c", []),
    ("kernel/ktime.c", []),
]

CASES = ["decide_table", "us_math", "no_u32_overflow", "monotonic",
         "clamp"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "kernel")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
MUTATIONS = [
    (r"        out->tick  = t \+ 1;",
     "        out->tick  = t;",
     "p1 = 1 で tick を足さない (新しい周期の count を古い tick と組ませ、"
     "時刻が 1 周期ぶん戻る = 往復 2 の反例 1)"),
    (r"        time_branch_hits\[TIME_BR_RETRY\]\+\+;\n        return TIME_DECIDE_RETRY;",
     "        time_branch_hits[TIME_BR_RETRY]++;\n"
     "        out->tick = t;\n        out->count = count;\n"
     "        return TIME_DECIDE_OK;",
     "p1 = 0 / p2 = 1 を**やり直さずに採用する** (境界のどちら側か分からない "
     "count を採るので最大 1 周期ずれる)"),
    (r"    us = \(unsigned long long\)tick \* \(unsigned long long\)period_us \+\n"
     r"         \(unsigned long long\)frac;",
     "    us = (unsigned long long)(tick * period_us + frac);",
     "tick との積を u32 で組む (**71 分で時刻が 0 に戻る**。起動から 71 分は "
     "エミュレータでも実機でも回していない)"),
    (r"        unsigned int elapsed = \(count <= reload\) \? \(reload - count\) : 0;",
     "        unsigned int elapsed = count;",
     "経過ぶんを reload − count ではなく count そのものにする (時計が逆走する)"),
    (r"    if \(p1\) \{",
     "    if (p1 && p2) {",
     "p1 だけでは tick を足さない (p2 も 1 のときだけ足す。境界の直前で "
     "読んで p2 が 0 に落ち着いた場合に 1 周期ぶん戻る)"),
    (r"        frac = \(unsigned int\)\(\(\(unsigned long long\)elapsed \*\n"
     r"                               \(unsigned long long\)period_us\) / reload\);",
     "        frac = (unsigned int)(elapsed / (reload / period_us));",
     "端数の割り算を先に約分する (reload / period_us = 1 か 2 に潰れ、"
     "補間が µs ではなく 5〜10ms 刻みになる)"),
    (r"    if \(us < last\) \{\n        if \(out\) \*out = last;",
     "    if (us < last) {\n        if (out) *out = us;",
     "巻き戻りを数えるだけで**押さえない** (NP21/W は 8254 の再ロードと 8259 の "
     "IRR を原子的に模擬しないので、判定表を正しく通しても 1 万回読みが逆行する)"),
    (r"    if \(us < last\) \{",
     "    if (us <= last) {",
     "同じ µs を 2 回読んだだけでクランプに数える (回数が「模擬の粗さ」の"
     "指標にならなくなる)"),
]


def host_build(tmp, source_text=None):
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "time-math-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        mut = src_dir / "kernel"
        mut.mkdir(exist_ok=True)
        (mut / "time_math.c").write_text(source_text, encoding="utf-8")
        (mut / "time_math.h").write_text(
            (ROOT / "kernel/time_math.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../kernel/time_math.c"', '"kernel/time_math.c"'),
            encoding="utf-8")
        cmd = ["gcc", *FLAGS, "-I" + str(ROOT / "include"), "-I" + str(src_dir),
               "-I" + str(mut), str(shim), "-o", str(exe)]
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
        cmd = ["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror", "-Wdeclaration-after-statement",
               "-D__KERNEL_BUILD__",
               "-I" + str(ROOT), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "kernel"),
               "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "lib"),
               *extra, "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU89 -Werror PASS", flush=True)


def mutate(tmp):
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        mutated, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, mutated)
        except subprocess.CalledProcessError:
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
    with tempfile.TemporaryDirectory(prefix="os32-time-math-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real kernel/time_math.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
