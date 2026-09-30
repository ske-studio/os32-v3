"""動的 IRQ の判断 (kernel/irq_math.c) のホスト試験。

記録: tools/tests/irq_math_tdd.md
票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-1 (割り込みの動的登録)

実物の kernel/irq_math.c を 1 行も写さずに #include して回す。PIC も IDT も
tick_count も触らないので、模型は「筋書きどおりの値を返す偽装置」だけ。

**ここで見る分岐は NP21/W でも実機でも狙って作れない** — 「A と B が同時に
要因を持つ」「1 巡目で受けて 2 巡目が空」は時間の重なりで、エミュレータでも
実機でも再現を待つしかない。W3 (NP21/W) は `int 0x23` と `/api/pic` で
経路が通ることを見る側で、分岐の網羅はここが持つ。

  python3 -B tools/tests/test_irq_math.py            # ホストで全ケース
  python3 -B tools/tests/test_irq_math.py --target   # + i386-elf で irq.c も通す
  python3 -B tools/tests/test_irq_math.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/irq_math_host.c"
SRC = ROOT / "kernel/irq_math.c"

# カーネルと同じ i386-elf で通す実ソース。irq.c まで入れるのは、
# 表・PIC のマスク・EOI が irq_math の決め事を**実際に**呼ぶ形を固定するため。
TARGET_SRCS = [
    ("kernel/irq_math.c", []),
    ("kernel/irq.c", []),
]

CASES = ["dispatch_two_pass", "register_rules", "storm", "eoi_plan"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "kernel")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (パターン, 置換, 説明)
MUTATIONS = [
    (r"            round \|= rc;\n",
     "            round |= rc;\n            if (rc == IRQ_HANDLED) break;\n",
     "最初の HANDLED で走査を打ち切る (同時に要因を持つ相方を取りこぼす "
     "= 共有線が上がったまま次のエッジが来ない)"),
    (r"        any \|= round;\n",
     "        any = round;\n",
     "handled_any を 2 巡目で上書きする (受けたのに「誰も受けなかった」に化け、"
     "診断とストーム勘定が狂う)"),
    (r"    if \(\(int\)ln->count >= IRQ_MAX_HANDLERS\) return IRQ_ERR_FULL;",
     "    if ((int)ln->count > IRQ_MAX_HANDLERS) return IRQ_ERR_FULL;",
     "5 件目を受けてしまう (表の外へ書く)"),
    (r"        for \(i = 0; i < \(int\)ln->count; i\+\+\) \{\n"
     r"            if \(!\(ln->slot\[i\]\.flags & \(unsigned int\)IRQ_F_SHARED\)\) \{\n"
     r"                return IRQ_ERR_SHARE;\n            \}\n        \}\n",
     "",
     "新規の flags だけ見て既存を見ない (排他のつもりの driver が相席させられる)"),
    (r"    if \(ln->tick_hits > \(unsigned int\)IRQ_STORM_LIMIT && !handled_any\) \{",
     "    if (ln->tick_hits >= (unsigned int)IRQ_STORM_LIMIT && !handled_any) {",
     "閾値が 1 つ手前 (200 回ちょうどでマスクが入る)"),
    (r"        return slave_isr_bit7 \? IRQ_EOI_SLAVE_MASTER : IRQ_EOI_MASTER;",
     "        return (slave_isr_bit7 >= 0) ? IRQ_EOI_SLAVE_MASTER\n"
     "                                      : IRQ_EOI_MASTER;",
     "IRQ15 のスプリアスでもスレーブに EOI を送る (本物の IR7 を潰す)"),
    (r"    if \(ln->quarantined\) return IRQ_ERR_BUSY;\n",
     "",
     "隔離済みの線に登録を許す (打ち切ったはずの線が再び動き出す)"),
    (r"    if \(irq >= \(unsigned int\)IRQ_LINE_MAX\) return IRQ_ERR_INVAL;\n"
     r"    if \(irq_dyn_index\(irq\) < 0 \|\| ln == 0\) return IRQ_ERR_NOTSUP;\n\n"
     r"    if \(fn == 0\) return IRQ_ERR_INVAL;",
     "    if (irq_dyn_index(irq) < 0 || ln == 0) return IRQ_ERR_NOTSUP;\n\n"
     "    if (fn == 0) return IRQ_ERR_INVAL;",
     "範囲外 (>= 16 / PCI の未割り当て 0xFF) を「固定 IRQ」と同じ扱いにする "
     "(呼び手が番号の壊れと線種の違いを区別できなくなる)"),
]


def host_build(tmp, source_text=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。"""
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "irq-math-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        mut = src_dir / "kernel"
        mut.mkdir(exist_ok=True)
        (mut / "irq_math.c").write_text(source_text, encoding="utf-8")
        (mut / "irq_math.h").write_text(
            (ROOT / "kernel/irq_math.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../kernel/irq_math.c"', '"kernel/irq_math.c"'),
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
    """カーネルと同じ i386-elf で kernel/irq.c ごと通す。"""
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
    with tempfile.TemporaryDirectory(prefix="os32-irq-math-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real kernel/irq_math.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
