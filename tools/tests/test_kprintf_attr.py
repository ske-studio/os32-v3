"""kprintf の属性変換 (lib/kprintf_attr.c) のホスト試験。

記録: tools/tests/kprintf_attr_tdd.md
票  : 実機 PC-9821Ra266 の FD 起動が root panic になり、しかも kprintf の
      診断行 ([fdc] / [ide]) が画面に 1 行も出なかった件

実物の lib/kprintf_attr.c を 1 行も写さずに #include して回す。
変換は I/O も VRAM も触らないので模型は要らない。

**エミュレータでは踏めない**。NP21/W の /api/tvram は文字コードだけを返し、
属性 VRAM (0xA2000) を見せないので、属性が壊れていても「出ている」ように
読めてしまう。実機の画面だけが真を言う — だからここで数値として固定する。

  python3 -B tools/tests/test_kprintf_attr.py            # ホストで全ケース
  python3 -B tools/tests/test_kprintf_attr.py --target   # + i386-elf で通す
  python3 -B tools/tests/test_kprintf_attr.py --mutate   # 否定側 (変異が RED か)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/kprintf_attr_host.c"
SRC = ROOT / "lib/kprintf_attr.c"

# カーネルと同じ i386-elf で通す実ソース。
TARGET_SRCS = [
    ("lib/kprintf_attr.c", []),
    ("lib/kprintf.c", []),
]

CASES = ["real_callers", "pc98_passthrough", "invariants", "cga_bits"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "lib")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (パターン, 置換, 説明)
MUTATIONS = [
    (r"return \(u8\)\(color \| TATTR_VISIBLE\);", "return color;",
     "表示ビットを立てない (色だけ指定 = シークレットのまま消える)"),
    (r"        color = \(u8\)\(TATTR_WHITE & KPRINTF_PC98_COLOR_MASK\);\n", "",
     "色が黒のときに白へ倒さない (kprintf(0, …) が不可視のまま)"),
    (r"return \(u8\)\(attr \| TATTR_VISIBLE\);", "return attr;",
     "PC-98 流の属性の表示ビットを補わない"),
    (r"    if \(\(attr & KPRINTF_PC98_COLOR_MASK\) != 0\) \{\n"
     r"        return \(u8\)\(attr \| TATTR_VISIBLE\);\n    \}\n", "",
     "PC-98 流の属性まで CGA として読み替える (0xC1 が赤に化ける)"),
    (r"if \(\(attr & KPRINTF_CGA_FG_RED\) != 0\)   color \|= TATTR_B_RED;",
     "if ((attr & KPRINTF_CGA_FG_RED) != 0)   color |= TATTR_B_BLUE;",
     "CGA の赤を PC-98 の青へ写す (色の対応が入れ替わる)"),
    (r"#define KPRINTF_PC98_COLOR_MASK   \(TATTR_B_GREEN \| TATTR_B_RED \| TATTR_B_BLUE\)",
     "#define KPRINTF_PC98_COLOR_MASK   (TATTR_B_GREEN | TATTR_B_RED)",
     "素通し判定から青を落とす (0x21 が緑に化ける)"),
]


def host_build(tmp, source_text=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。"""
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "kprintf-attr-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        # 変異させた写しを一時の lib/ に置いて、そちらを引かせる。
        mut = src_dir / "lib"
        mut.mkdir(exist_ok=True)
        (mut / "kprintf_attr.c").write_text(source_text, encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../lib/kprintf_attr.c"', '"lib/kprintf_attr.c"'),
            encoding="utf-8")
        cmd = ["gcc", *FLAGS, "-I" + str(ROOT / "include"), "-I" + str(src_dir),
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
    """カーネルと同じ i386-elf で lib/kprintf.c ごと通す。"""
    for rel, extra in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror", "-Wdeclaration-after-statement",
               "-D__KERNEL_BUILD__",
               "-I" + str(ROOT), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "drivers"),
               "-I" + str(ROOT / "lib"),
               *extra, "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU89 -Werror PASS", flush=True)


def check_entry():
    """kprintf() の入口で 1 回だけ通していることを静的に見る。

    変換そのものが正しくても、呼び出し元が通っていなければ画面は黒いまま。
    下流 (shell_print / console_write) は全部 kprintf の attr を使うので、
    入口の 1 行があることを見れば足りる。
    """
    text = (ROOT / "lib/kprintf.c").read_text(encoding="utf-8")
    n = text.count("attr = kprintf_attr_to_pc98(attr);")
    print(f"ENTRY kprintf applies conversion x{n}", flush=True)
    return 0 if n == 1 else 1


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
            # コンパイルが通らない変異も「見つけた」に数える。
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
    with tempfile.TemporaryDirectory(prefix="os32-kprintf-attr-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real lib/kprintf_attr.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        rc += check_entry()
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
