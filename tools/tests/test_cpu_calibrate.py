"""CPU 校正の止め方と丸め (kernel/cpu_calibrate_math.c) のホスト試験。

記録: tools/tests/cpu_calibrate_tdd.md
票  : docs/archive/realhw_v21/TASK_SERIAL_VFAST.md (往復 3 — 1 バイト 2ms の固定費)

実物の kernel/cpu_calibrate_math.c を 1 行も写さずに #include して回す。
判定は tick_count も I/O も触らないので模型は要らない。

**ここはエミュレータでは踏めない。** NP21/W は十分に遅いので校正ループ 1 周で
5 tick を超え、丸めが起きない。実機 PC-9821Ra266 (266MHz) でだけ 1 周が 1 tick に
満たず、`elapsed = 0 → 1` の丸めで loops_per_tick が実際の 1/7〜1/13 になり、
`cpu_delay_us(5)` が 0.5µs しか待たず、シリアルが 1 バイトごとに `_halt()` へ
落ちて 9600 でも 38400 でも約 2ms/バイトになっていた。

  python3 -B tools/tests/test_cpu_calibrate.py            # ホストで全ケース
  python3 -B tools/tests/test_cpu_calibrate.py --target   # + i386-elf で実物も通す
  python3 -B tools/tests/test_cpu_calibrate.py --mutate   # 否定側 (変異が RED か)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/cpu_calibrate_host.c"
SRC = ROOT / "kernel/cpu_calibrate_math.c"
# カーネルと同じ i386-elf で通す実ソース。cpu_calibrate.c は判定を使う側。
TARGET_SRCS = [
    ("kernel/cpu_calibrate_math.c", []),
    ("kernel/cpu_calibrate.c", []),
]

CASES = ["fast_cpu", "slow_cpu", "give_up", "delay_scale"]

FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "kernel")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (パターン, 置換, 説明)
MUTATIONS = [
    (r"if \(ticks >= \(u32\)CALIBRATE_MIN_TICKS\) \{",
     "if (ticks >= (u32)CALIBRATE_MIN_TICKS || rounds >= 1) {",
     "1 周で打ち切る (= 直す前の姿。266MHz で loops_per_tick が 1/10 になる)"),
    (r"if \(ticks >= \(u32\)CALIBRATE_MIN_TICKS\) \{",
     "if (ticks >= 1) {",
     "1 tick で確定してよいことにする (±100% の誤差を許す)"),
    (r"if \(rounds >= \(u32\)CALIBRATE_MAX_ROUNDS\) \{",
     "if (rounds >= (u32)CALIBRATE_MAX_ROUNDS * 10UL) {",
     "周回数の打ち切りを 10 倍遠くへ (PIT が死んだとき起動が戻るまでが長すぎる)"),
    (r"    if \(ticks == 0\) \{\n        return \(u32\)CALIBRATE_FALLBACK_LPT;\n    \}\n",
     "    if (ticks == 0) {\n        ticks = 1;\n    }\n",
     "測れなかったときに合計を 1 tick で割る (4000 万 loops/tick が入り "
     "cpu_delay_us が返らなくなる)"),
    (r"    if \(lpt < \(u32\)CALIBRATE_MIN_LPT\) \{\n"
     r"        return \(u32\)CALIBRATE_FALLBACK_LPT;\n    \}\n",
     "",
     "極端に小さい結果をそのまま採る (安全装置を外す)"),
]


def host_build(tmp, source_text=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。"""
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "cpu-calibrate-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        # 変異させた写しを一時の kernel/ に置いて、そちらを先に引かせる
        # (実物のソースは書き換えない = make check-par で並列に回せる)。
        mut = src_dir / "kernel"
        mut.mkdir(exist_ok=True)
        (mut / "cpu_calibrate_math.c").write_text(source_text, encoding="utf-8")
        (mut / "cpu_calibrate_math.h").write_text(
            (ROOT / "kernel/cpu_calibrate_math.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../kernel/cpu_calibrate_math.c"',
                '"kernel/cpu_calibrate_math.c"'),
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
    """カーネルと同じ i386-elf で kernel/cpu_calibrate.c ごと通す。"""
    for rel, extra in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror", 
               "-D__KERNEL_BUILD__",
               "-I" + str(ROOT), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "kernel"),
               "-I" + str(ROOT / "lib"),
               *extra, "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU11 -Werror PASS", flush=True)


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
    with tempfile.TemporaryDirectory(prefix="os32-cpu-calibrate-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU11 -Werror compile PASS "
              "(real kernel/cpu_calibrate_math.c)", flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
