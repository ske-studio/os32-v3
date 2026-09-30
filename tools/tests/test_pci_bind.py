"""PCI の結線表 (drivers/pci_bind_match.c) のホスト試験。

記録: tools/tests/pci_bind_tdd.md
票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-4 (PCI デバイスの結線表)

実物の drivers/pci_bind_match.c を 1 行も写さずに #include して回す。
probe は呼び手が渡す関数ポインタなので、偽 driver を並べれば
DECLINE → 次へ / QUARANTINE → 打ち切り の遷移は全部踏める。

**NP21/W には PCI が無い** — `pci_init()` は「mech#1 absent」で終わるので、
この層はエミュレータでは 1 行も走らない。実機の日に初めて通る経路にしない
ために、規則をここで固定する (§4-49・§4-51 と同じ型の「実機でしか出ない」)。

  python3 -B tools/tests/test_pci_bind.py            # ホストで全ケース
  python3 -B tools/tests/test_pci_bind.py --target   # + i386-elf で実物を通す
  python3 -B tools/tests/test_pci_bind.py --mutate   # 否定側
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/pci_bind_host.c"
SRC = ROOT / "drivers/pci_bind_match.c"
TARGET_SRCS = ["drivers/pci_bind_match.c", "drivers/pci_bind.c"]

CASES = ["match_rules", "next_order", "decline_chain", "quarantine_stops",
         "reason_reset", "line_state", "multi_dev", "info_get"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "drivers", "sdk/include/os32")]

MUTATIONS = [
    (r"        if \(rc == PCI_PROBE_QUARANTINE\) \{", "        if (0) {",
     "QUARANTINE で打ち切らない (状態不明の装置を次の driver に渡す)"),
    (r"            return rc;\n        \}\n        /\* DECLINE",
     "            rc = PCI_PROBE_DECLINE;\n        }\n        /* DECLINE",
     "QUARANTINE を記録だけして探索を続ける"),
    (r"        pci_bind_reason_reset\(\);\n", "",
     "候補ごとに理由を初期化しない (前の driver の理由が後に残る)"),
    (r"    if \(drv->class != PCI_MATCH_ANY8 && drv->class != dev->class\) return 0;\n",
     "", "class 欄を見ない (別の種類の装置に当たる)"),
    (r"    if \(drv->subclass != PCI_MATCH_ANY8 && drv->subclass != dev->subclass\)\n        return 0;\n",
     "", "subclass 欄を見ない"),
    (r"    if \(drv->device != PCI_MATCH_ANY16 && drv->device != dev->device\) return 0;\n",
     "", "device 欄を見ない (同じベンダの別チップに当たる)"),
    (r"    if \(bits & PCI_LINE_BIT_QUARANTINED\)  info->line_state = PCI_LINE_QUARANTINED;\n    else if \(bits & PCI_LINE_BIT_STORM\)   info->line_state = PCI_LINE_STORM_MASKED;",
     "    if (bits & PCI_LINE_BIT_STORM)   info->line_state = PCI_LINE_STORM_MASKED;\n    else if (bits & PCI_LINE_BIT_QUARANTINED)  info->line_state = PCI_LINE_QUARANTINED;",
     "隔離とストームで**弱いほう**を名乗る (隔離を復旧済みに見せる)"),
    (r"    if \(info->irq >= PCI_BIND_IRQ_MAX\) return;\n", "",
     "16 以上の irq_line で hook を呼ぶ (線ではない値をシフトする)"),
    # 0xFF は 16 以上でもあるので、2 本の範囲検査は**互いに相手を覆う**。
    # 片方ずつでは挙動が変わらないので、2 本まとめて消して RED にする。
    (r"    if \(info->irq == PCI_IRQ_UNASSIGNED\) return;\n"
     r"    if \(info->irq >= PCI_BIND_IRQ_MAX\) return;\n", "",
     "未割り当て (0xFF) と 16 以上で hook を呼ぶ (線ではない値をシフトする)"),
    (r"            info->reason = \(s_reason != PCI_BIND_OK\) \? s_reason\n"
     r"                                          : PCI_BIND_DECLINED_UNSPECIFIED;",
     "            info->reason = s_reason;",
     "理由を書かなかった DECLINE を「問題なし」と記録する"),
]


def host_build(tmp, source_text=None):
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "pci-bind-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        mut = src_dir / "drivers"
        mut.mkdir(exist_ok=True)
        (mut / "pci_bind_match.c").write_text(source_text, encoding="utf-8")
        shim = src_dir / "harness.c"
        # 変異させるのは pci_bind_match.c だけ。pci_bind.c (記録の取得口) は
        # **実物のまま**絶対パスで引く — 相対のままだと tmp から辿れない。
        shim.write_text(
            HARNESS.read_text(encoding="utf-8")
            .replace('"../../drivers/pci_bind.c"',
                     '"%s/drivers/pci_bind.c"' % ROOT)
            .replace('"../../drivers/pci_bind_match.c"',
                     '"drivers/pci_bind_match.c"'),
            encoding="utf-8")
        cmd = ["gcc", *FLAGS, *INCLUDES, "-I" + str(src_dir), str(shim),
               "-o", str(exe)]
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
    for rel in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror", "-Wdeclaration-after-statement",
               "-D__KERNEL_BUILD__",
               "-I" + str(ROOT), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "kernel"),
               "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "lib"),
               "-c", str(ROOT / rel),
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
    with tempfile.TemporaryDirectory(prefix="os32-pci-bind-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real drivers/pci_bind_match.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
