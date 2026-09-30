"""PIT の分周 (kernel/pit_math.c) のホスト試験。

記録: tools/tests/pit_clock_tdd.md
票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-0 (PIT の分周をクロック判定に合わせる)

実物の kernel/pit_math.c を 1 行も写さずに #include して回す。算数だけなので
I/O もタイマも触らず、模型は 1 つも要らない。

**NP21/W は 1.9968MHz 固定なので、2.4576MHz 系の分岐はエミュレータでは
一度も通らない** — 実機 PC-9821Ra266 で tick が 8.125ms だった件がこれ。

  python3 -B tools/tests/test_pit_clock.py            # ホストで全ケース
  python3 -B tools/tests/test_pit_clock.py --target   # + i386-elf で idt.c も通す
  python3 -B tools/tests/test_pit_clock.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/pit_clock_host.c"
SRC = ROOT / "kernel/pit_math.c"
# カーネルと同じ i386-elf で通す実ソース。idt.c まで入れるのは、
# pit_init が pit_compute と sysclk_hz を**実際に**呼ぶ形を固定するため。
# STATIC_ASSERT (中間積が 32bit に収まるか) が効くのもこちら側。
TARGET_SRCS = [
    ("kernel/pit_math.c", []),
    ("kernel/sysclk.c", []),
    ("kernel/idt.c", []),
]

CASES = ["both_clocks", "reject_hz", "reject_clock", "no_overflow",
         "real_hw_story"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "kernel")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (パターン, 置換, 説明)
MUTATIONS = [
    (r"raw = clk_hz / \(unsigned long\)hz;",
     "raw = (unsigned long)SYSCLK_1997 / (unsigned long)hz;",
     "クロックを見ずに 1.9968MHz で割る (= 直す前の姿。実機で tick 8.125ms)"),
    (r"    if \(hz != \(unsigned int\)PIT_HZ\) \{\n        return PIT_ERR_HZ;\n    \}\n",
     "",
     "100Hz 以外を受けてしまう (端数のリロード値が積まれ周期がずれる)"),
    (r"    if \(clk_hz != \(unsigned long\)SYSCLK_1997 &&\n"
     r"        clk_hz != \(unsigned long\)SYSCLK_2458\) \{\n"
     r"        return PIT_ERR_CLOCK;\n    \}\n",
     "",
     "知らないクロックで割る (0 除算・桁違いのリロード値)"),
    (r"period = \(reload \* PIT_PERIOD_SCALE\) / \(unsigned int\)\(clk_hz / PIT_CLOCK_DIV\);",
     "period = (reload * PIT_US_PER_SEC) / (unsigned int)clk_hz;",
     "約分せず 10^6 を掛ける (32bit で溢れて period_us が桁違いになる)"),
    (r"    out->valid = 0;\n",
     "    out->valid = 1;\n",
     "断ったのに valid を立てる (呼び出し側が古い値を本物と読む)"),
]


def host_build(tmp, source_text=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。"""
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "pit-clock-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        # 変異させた pit_math.c を一時の kernel/ に置いて、そちらを先に引かせる。
        mut = src_dir / "kernel"
        mut.mkdir(exist_ok=True)
        (mut / "pit_math.c").write_text(source_text, encoding="utf-8")
        (mut / "pit_math.h").write_text(
            (ROOT / "kernel/pit_math.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../kernel/pit_math.c"', '"kernel/pit_math.c"'),
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
    """カーネルと同じ i386-elf で kernel/idt.c ごと通す。"""
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
    """実装を 1 か所ずつ壊して、どれも RED になることを見る。"""
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
            # コンパイルが通らないのも RED (STATIC_ASSERT で落ちる変異)。
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
        print("HOST GNU89 -Werror compile PASS (real kernel/pit_math.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
