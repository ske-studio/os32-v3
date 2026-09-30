"""シリアルの速度判定 (drivers/serial_plan.c) のホスト試験。

記録: tools/tests/serial_vfast_tdd.md
票  : docs/archive/realhw_v21/TASK_SERIAL_VFAST.md (実機を 115200bps まで上げる)

実物の drivers/serial_plan.c を 1 行も写さずに #include して回す。判定は
I/O もタイマも触らないので模型は要らない。

**ここはエミュレータでは踏めない。** NP21/W は通信速度を模擬しないので
(np21w-src/src/io/serial.c)、分周が合っているかは実機でしか分からない。
だからホストで表そのものを押さえる。

  python3 -B tools/tests/test_serial_vfast.py            # ホストで全ケース
  python3 -B tools/tests/test_serial_vfast.py --target   # + i386-elf で serial.c も通す
  python3 -B tools/tests/test_serial_vfast.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/serial_vfast_host.c"
# 変異させる実ソース。どちらも純粋 (I/O も KAPI も触らない)。
SRCS = {
    "drivers/serial_plan.c": ROOT / "drivers/serial_plan.c",
    "userland/shell/serial_watchdog.c":
        ROOT / "userland/shell/serial_watchdog.c",
}
# カーネルと同じ i386-elf で通す実ソース。serial.c は判定を使う側。
TARGET_SRCS = [
    ("drivers/serial_plan.c", []),
    ("drivers/serial.c", []),
]

CASES = ["vfast_table", "compat_exact", "compat_inexact", "mode_choice",
         "tx_budget", "tx_budget_ticks", "status_bits", "fifo_detect",
         "refuse_inexact",
         "watchdog", "watchdog_leave", "real_hw_story"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
# serial_plan.h は共有の契約ヘッダ (os32_kapi_shared.h) から
# SER_MODE_* / SER_INIT_* を引くので sdk/include/os32 も要る。
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "drivers", "sdk/include/os32")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (ソース, パターン, 置換, 説明)
MUTATIONS = [
    ("drivers/serial_plan.c",
     r"\{ 115200UL, SER_VFAST_DIV_115200 \},",
     "{ 115200UL, SER_VFAST_DIV_57600  },",
     "V･FAST の表を 1 行ずらす (115200 のつもりで 57600 が出る)"),
    ("drivers/serial_plan.c",
     r"\{   9600UL, SER_VFAST_DIV_9600   \}",
     "{   4800UL, SER_VFAST_DIV_9600   }",
     "表に無い速度を足す (9600 の戻しが V･FAST に入らなくなる)"),
    ("drivers/serial_plan.c",
     r"return \(u8\)\(\(mode == SER_MODE_VFAST\) \? SER_FSTS_RXRDY : STS_RXRDY\);",
     "return (u8)((mode == SER_MODE_VFAST) ? STS_RXRDY : STS_RXRDY);",
     "FIFO の RxRDY を互換のビット位置 (bit1) で見る"),
    ("drivers/serial_plan.c",
     r"return \(u8\)\(\(mode == SER_MODE_VFAST\) \? SER_FSTS_TXRDY : STS_TXRDY\);",
     "return (u8)((mode == SER_MODE_VFAST) ? SER_FSTS_TXEMP : STS_TXRDY);",
     "FIFO の TxRDY を TxEMP (bit0) と取り違える"),
    ("drivers/serial_plan.c",
     r"if \(us < SER_TX_BUDGET_MIN_US\) \{\n        us = SER_TX_BUDGET_MIN_US;\n    \}",
     "",
     "予算の下限を外す (速い速度で 0µs になり、直す前の hlt 待ちに戻る)"),
    ("drivers/serial_plan.c",
     r"if \(want_vfast && has_fifo\) \{",
     "if (want_vfast || has_fifo) {",
     "FIFO があれば明示指定なしでも V･FAST に入る (起動時の既定 9600 が"
     "互換で上がらなくなる / FIFO 非搭載機でも 013Ah を叩く)"),
    ("drivers/serial_plan.c",
     r"out->exact = \(u8\)\(\(out->actual == baud\) \? 1 : 0\);",
     "out->exact = 1;",
     "割り切れない分周を「ちょうど出る」と答える (38400 → 41600 を見逃す)"),
    ("drivers/serial_plan.c",
     r"    ticks \+= SER_TX_BUDGET_EDGE_TICKS;\n",
     "",
     "tick の境界ずれを足さない (1 tick の予算は実時間 0 になりうる)"),
    ("drivers/serial_plan.c",
     r"    if \(ticks < SER_TX_BUDGET_TICKS_MIN\) \{\n"
     r"        ticks = SER_TX_BUDGET_TICKS_MIN;\n    \}\n",
     "",
     "予算の下限 3 tick を外す (保証 10ms では FTDI の遅延タイマ 16ms を"
     "またげず、結局 hlt に落ちて 1 バイト 2ms に戻る)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(acked\) \{\n        return SER_WD_LINKED;\n    \}\n"
     r"[\s\S]*?    if \(elapsed_ticks >= \(unsigned long\)"
     r"SER_SWITCH_WATCHDOG_TICKS\) \{\n        return SER_WD_REVERT;\n    \}",
     "    if (elapsed_ticks >= (unsigned long)SER_SWITCH_WATCHDOG_TICKS) {\n"
     "        return SER_WD_REVERT;\n    }\n"
     "    if (acked) {\n        return SER_WD_LINKED;\n    }",
     "番犬が期限を ack より先に見る (期限ちょうどに届いた ack を無音と"
     "読み替えて、揃った足並みを自分で壊す)"),
    ("userland/shell/serial_watchdog.c",
     r"elapsed_ticks >= \(unsigned long\)SER_SWITCH_WATCHDOG_TICKS\) \{",
     "elapsed_ticks >= (unsigned long)SER_SWITCH_WATCHDOG_TICKS * 1000) {",
     "番犬の期限を 1000 倍にする (事実上いつまでも戻さない = 会話が死んだまま)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(!w \|\| !w->armed\) return;\n    w->acked = 1;",
     "    if (!w) return;\n    w->acked = 1;",
     "仕掛かっていないときも ack を立てる (前の切替の ack が次で即 LINKED)"),
    ("userland/shell/serial_watchdog.c",
     r"    w->armed = 0;\n    return d;",
     "    return d;",
     "答えを出したあと番犬を下ろさない (REVERT を 2 度返して "
     "serial_init を二重に呼ぶ)"),
    ("userland/shell/serial_watchdog.c",
     r"    w->acked = 0;\n    w->start_tick = tick;",
     "    w->start_tick = tick;",
     "arm が ack の印を 0 に戻さない (前の切替の ack で次が即 LINKED になる)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(w->acked\) \{\n        return SER_WD_WAIT;   /\* 確認済み = そのままでよい \*/\n    \}\n    return SER_WD_REVERT;",
     "    return SER_WD_WAIT;",
     "rshell を抜けるときに未確認でも戻さない (番犬が忘れられて、"
     "確認の取れていない速度のまま会話が死ぬ — 往復 4 B4)"),
    ("userland/shell/serial_watchdog.c",
     r"int serial_watchdog_leave\(struct serial_watchdog \*w\)\n\{\n"
     r"    if \(!w \|\| !w->armed\) return SER_WD_WAIT;",
     "int serial_watchdog_leave(struct serial_watchdog *w)\n{\n"
     "    if (!w) return SER_WD_WAIT;",
     "仕掛けていなくても抜けるときに戻す (ローカル CUI で切り替えた速度が"
     "rshell を抜けた拍子に巻き戻る — 往復 4 B3)"),
    ("userland/shell/serial_watchdog.c",
     r"    if \(elapsed_ticks >= \(unsigned long\)SER_SWITCH_WATCHDOG_TICKS\) \{",
     "    if (elapsed_ticks > (unsigned long)SER_SWITCH_WATCHDOG_TICKS) {",
     "期限を 1 tick 甘くする (期限ちょうどでは戻さない = 境界が仕様とずれる)"),
    ("userland/shell/serial_watchdog.c",
     r"    d = serial_watchdog_decide\(tick - w->start_tick, w->acked\);",
     "    d = serial_watchdog_decide(tick - w->start_tick, 1);",
     "poll がいつでも ack 済みとして聞く (番犬が一度も戻さない)"),
    ("userland/shell/serial_watchdog.c",
     r"    w->armed = 0;\n    if \(w->acked\) \{",
     "    if (w->acked) {",
     "抜けるときに番犬を下ろさない (REVERT を 2 度返して serial_init を"
     "二重に呼ぶ)"),
]


def host_build(tmp, mutated=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。

    mutated = {相対パス: 中身} を渡すと、**実物のソースは 1 バイトも触らずに**
    写しの上で変異させる (= make check-par で並列に回せる)。
    """
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "serial-vfast-host"
    if mutated is None:
        cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
        subprocess.run(cmd, cwd=ROOT, check=True)
        return exe

    # ハーネスが #include している 2 本と、その巻き込むヘッダを写す。
    tree = src_dir / "tree"
    for rel in ("drivers/serial_plan.c", "drivers/serial_plan.h",
                "drivers/serial.h",
                "userland/shell/serial_watchdog.c",
                "userland/shell/serial_watchdog.h"):
        dst = tree / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(mutated.get(rel,
                                   (ROOT / rel).read_text(encoding="utf-8")),
                       encoding="utf-8")
    shim = src_dir / "harness.c"
    shim.write_text(
        HARNESS.read_text(encoding="utf-8")
               .replace('"../../drivers/', '"drivers/')
               .replace('"../../userland/', '"userland/'),
        encoding="utf-8")
    cmd = ["gcc", *FLAGS, "-I" + str(ROOT / "include"),
           "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(tree),
           "-I" + str(tree / "drivers"), "-I" + str(tree / "userland/shell"),
           str(shim), "-o", str(exe)]
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
    """カーネルと同じ i386-elf で drivers/serial.c ごと通す。"""
    for rel, extra in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror", "-Wdeclaration-after-statement",
               "-D__KERNEL_BUILD__",
               "-I" + str(ROOT), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "drivers"),
               "-I" + str(ROOT / "kernel"), "-I" + str(ROOT / "lib"),
               *extra, "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU89 -Werror PASS", flush=True)


def mutate(tmp):
    """実装を 1 か所ずつ壊して、どれも RED になることを見る。"""
    bad = 0
    for i, (rel, pattern, repl, why) in enumerate(MUTATIONS, 1):
        original = SRCS[rel].read_text(encoding="utf-8")
        text, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, {rel: text})
        except subprocess.CalledProcessError:
            # コンパイルが通らないのも RED (見逃しではない)。
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
    with tempfile.TemporaryDirectory(prefix="os32-serial-vfast-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real drivers/serial_plan.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
