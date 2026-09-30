"""キーボード 8251 のステータス判定 (drivers/kbd_status.c) と仮想 8251
(kernel/v86_kbd.c) のホスト試験。

記録: tools/tests/kbd_status_tdd.md
経緯: docs/POLICY_DEBUG.md §4-57 (実機 PC-9821Ra266 で打鍵が届かない、
      その実装レビューで出た V86 への偽 ESC と OE の扱い)

実物の drivers/kbd_status.c と kernel/v86_kbd.c を 1 行も写さずに #include
して回す。I/O を持たないので模型は要らない。

**ここで見る分岐のうち EMPTY / ERROR / OVERRUN は NP21/W では (ほぼ) 踏めない**
— NP21/W の keyboard_i43 は IRQ1 の前に必ず RxRDY を立て、エラービットは
自前のバッファが溢れたときの OE しか出さない。

  python3 -B tools/tests/test_kbd_status.py            # ホストで全ケース
  python3 -B tools/tests/test_kbd_status.py --target   # + i386-elf で kbd.c も通す
  python3 -B tools/tests/test_kbd_status.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/kbd_status_host.c"
# 変異の対象。キーは MUTATIONS の 1 列目。
SOURCES = {
    "c": "drivers/kbd_status.c",
    "h": "drivers/kbd_status.h",
    "v": "kernel/v86_kbd.c",
}

# カーネルと同じ i386-elf で通す実ソース。kbd.c まで入れるのは、
# IRQ1 ハンドラが判定を**実際に**呼ぶ形と KbdDiag の大きさ (STATIC_ASSERT)
# を固定するため。
TARGET_SRCS = [
    "drivers/kbd_status.c",
    "drivers/kbd.c",
    "kernel/v86_kbd.c",
]

CASES = ["empty", "error", "overrun", "data", "reflect", "v86empty"]

# -Wno-attributes: v86_kbd.c が引く os32_kapi_generated.h の __cdecl は
# x86-64 のホストでは無視される (警告だけ。判定には関係しない)。
FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror", "-Wno-attributes",
         "-Wdeclaration-after-statement"]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (対象ファイル, パターン, 置換, 説明)
MUTATIONS = [
    ("c", r"    if \(!\(st & KBD_STAT_RXRDY\)\) \{\n        return KBD_ST_EMPTY;\n    \}\n",
     "",
     "RxRDY を見ない (空 IRQ でも 0041h を読んで前回のバイトを打鍵にする)"),
    ("c", r"    if \(st & KBD_STAT_BADBYTE\) \{\n        return KBD_ST_ERROR;\n    \}\n",
     "",
     "PE / FE を見ない (化けたバイトを打鍵として配る)"),
    ("c", r"    if \(!\(st & KBD_STAT_RXRDY\)\) \{\n        return KBD_ST_EMPTY;\n    \}\n"
          r"    if \(st & KBD_STAT_BADBYTE\) \{\n        return KBD_ST_ERROR;\n    \}\n",
     "    if (st & KBD_STAT_BADBYTE) {\n        return KBD_ST_ERROR;\n    }\n"
     "    if (!(st & KBD_STAT_RXRDY)) {\n        return KBD_ST_EMPTY;\n    }\n",
     "エラーを RxRDY より先に見る (受信データが無いのに 0041h を読み捨てる)"),
    ("c", r"    if \(st & KBD_STAT_OE\) \{\n        return KBD_ST_OVERRUN;\n    \}\n",
     "",
     "OE を見ない (ER で解除せず、overrun_count も数えない)"),
    ("c", r"    if \(st & KBD_STAT_OE\) \{\n        return KBD_ST_OVERRUN;\n    \}\n",
     "    if (st & KBD_STAT_OE) {\n        return KBD_ST_ERROR;\n    }\n",
     "OE を ERROR にする (正しいバイトを読み捨てる — 修正前の挙動)"),
    ("c", r"    return \(kind == KBD_ST_DATA \|\| kind == KBD_ST_OVERRUN\);",
     "    (void)kind;\n    return 1;",
     "いつでも反射する (空 IRQ・エラーでゲストに偽の打鍵 — 修正前の挙動)"),
    ("c", r"    return \(kind == KBD_ST_DATA \|\| kind == KBD_ST_OVERRUN\);",
     "    return (kind == KBD_ST_DATA);",
     "OVERRUN で反射しない (使ったバイトがゲストに届かない)"),
    ("h", r"#define KBD_STAT_RXRDY   0x02",
     "#define KBD_STAT_RXRDY   0x04",
     "RxRDY を bit2 (TxEMP) と取り違える (NP21/W では常に立つので全部 DATA に化ける)"),
    ("h", r"#define KBD_STAT_BADBYTE \(KBD_STAT_PE \| KBD_STAT_FE\)",
     "#define KBD_STAT_BADBYTE (KBD_STAT_PE | KBD_STAT_OE | KBD_STAT_FE)",
     "OE を化けたバイトに数える (正しいバイトを読み捨てる)"),
    ("h", r"#define KBD_STAT_BADBYTE \(KBD_STAT_PE \| KBD_STAT_FE\)",
     "#define KBD_STAT_BADBYTE (KBD_STAT_PE)",
     "FE を落とす (フレーミングエラーのバイトを使う)"),
    ("h", r"#define KBD_STAT_BADBYTE \(KBD_STAT_PE \| KBD_STAT_FE\)",
     "#define KBD_STAT_BADBYTE (KBD_STAT_PE | KBD_STAT_FE | 0x80)",
     "DSR (bit7) をエラーに数える (NP21/W の `| 0x85` で打鍵が全部落ちる)"),
    ("v", r"            return last_data;   /\* 空読み。実機と同じく前回のバイト \*/",
     "            return 0x00;",
     "空読みで 0x00 を返す (= ESC のメイク — 修正前の挙動)"),
    ("v", r"        last_data = v;\n",
     "",
     "読んだバイトを覚えない (空読みが初期値のまま)"),
    ("v", r"    last_data = V86_KBD_IDLE;\n",
     "",
     "セッション開始で前回のバイトを捨てない (前のセッションの打鍵が出る)"),
]


def host_build(tmp, mutated=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。

    SOURCES の 3 本を tmp へ写し (mutated = {キー: 本文} は差し替え)、
    -I tmp を先頭に置いてハーネスの #include をそちらへ向ける。"""
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "kbd-status-host"
    for key, rel in SOURCES.items():
        dst = src_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        text = (mutated or {}).get(key)
        if text is None:
            text = (ROOT / rel).read_text(encoding="utf-8")
        dst.write_text(text, encoding="utf-8")
    cmd = ["gcc", *FLAGS, "-I" + str(src_dir),
           "-I" + str(ROOT / "include"), "-I" + str(ROOT / "sdk/include/os32"),
           "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "kernel"),
           str(HARNESS), "-o", str(exe)]
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
    """カーネルと同じ i386-elf で drivers/kbd.c ごと通す。"""
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
    """実装を 1 か所ずつ壊して、どれも RED になることを見る。"""
    orig = {k: (ROOT / rel).read_text(encoding="utf-8")
            for k, rel in SOURCES.items()}
    bad = 0
    for i, (which, pattern, repl, why) in enumerate(MUTATIONS, 1):
        mutated, n = re.subn(pattern, repl, orig[which], count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, {which: mutated})
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
    with tempfile.TemporaryDirectory(prefix="os32-kbd-status-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real drivers/kbd_status.c + kernel/v86_kbd.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
