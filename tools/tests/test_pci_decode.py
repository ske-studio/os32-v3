"""PCI コンフィギュレーションの純粋な復号 (drivers/pci_decode.c) のホスト試験。

記録: tools/tests/pci_decode_tdd.md
票  : docs/tasks/realhw/TASK_LAN_82557.md §2 L-A (実機の内蔵 LAN 82557 を
      `lspci` で見つける)

**NP21/W は PCI を実装していない** (`0CF8h` が無い) ので、この復号を走らせて
確かめられるのは実機 PC-9821Ra266 だけ。実機の 1 回はシリアル 115200 での
会話で、ビットの読み違いで潰すには高い。だからアドレス語の組み立て・BAR の
復号・ヘッダの切り出しを純粋関数に切り出し、ここで全部潰しておく。

実物の drivers/pci_decode.c を 1 行も写さずに #include して回す。復号は
I/O も静的配列も触らないので模型は要らない。

  python3 -B tools/tests/test_pci_decode.py            # ホストで全ケース
  python3 -B tools/tests/test_pci_decode.py --target   # + i386-elf で pci.c も通す
  python3 -B tools/tests/test_pci_decode.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/pci_decode_host.c"
SRC = ROOT / "drivers/pci_decode.c"
# カーネルと同じ i386-elf で通す実ソース。列挙側 (drivers/pci.c) は I/O を
# 触るのでホストでは回せないが、**コンパイルは通ることを見る** — 実機でしか
# 走らせられないコードこそ、型とプロトタイプのずれを手元で捕まえておく。
TARGET_SRCS = [
    ("drivers/pci_decode.c", []),
    ("drivers/pci.c", []),
    ("userland/shell/pci_verbose.c", []),
]

CASES = ["cfg_addr", "probe_values", "bar_kind", "bar_base", "header_type",
         "extract", "names", "story_82557", "absent",
         "verbose_tuner", "verbose_bars", "verbose_bridge", "verbose_bounds",
         "verbose_parse", "verbose_mem64", "verbose_longest"]
# コマンド層 (userland/shell/cmd_pci.c) は別のハーネスで回す — 引数の検査の
# 順番、config を読む先と回数、引数なしの `lspci` の回帰。
CMD_HARNESS = ROOT / "tools/tests/lspci_cmd_host.c"
CMD_CASES = ["lspci_noarg", "lspci_v_all", "lspci_v_one", "lspci_badargs"]
ALL_CASES = CASES + CMD_CASES

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "drivers")] + ["-I" + str(ROOT)]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# どれも「仕様を素直に読むと書いてしまう形」= RED 段で実際に書いた形。
# (パターン, 置換, 説明)
MUTATIONS = [
    ("drivers/pci_decode.c", r"\(\(bus & PCI_BUS_MASK\) << PCI_CFG_ADDR_BUS_SHIFT\)",
     "(bus << PCI_CFG_ADDR_BUS_SHIFT)",
     "bus/dev/fn をマスクせずに詰める (範囲外が隣の欄へ溢れ、別のデバイスを読む)"),
    ("drivers/pci_decode.c", r"if \(raw == 0\) return PCI_BAR_NONE;",
     "if (raw == 0 || raw == PCI_BAR_SPACE_IO) return PCI_BAR_NONE;",
     "番地未割り当ての I/O BAR (生値 1) を「無い」に畳む (票 R1 が消える)"),
    ("drivers/pci_decode.c", r"return raw & PCI_BAR_IO_ADDR_MASK;",
     "return raw & PCI_BAR_MEM_ADDR_MASK;",
     "I/O BAR の番地を ~0xF で切る (0xE808 が 0xE800 に化ける)"),
    ("drivers/pci_decode.c", r"if \(\(raw & PCI_BAR_SPACE_IO\) != 0\) return 0;",
     "",
     "I/O BAR でも bit3 を prefetchable と読む (番地の一部を属性と取り違える)"),
    ("drivers/pci_decode.c", r"return \(int\)\(header_type & PCI_HDR_LAYOUT_MASK\);",
     "return (int)header_type;",
     "Header Type の bit7 を落とさない (マルチファンクションのブリッヂを見落とす)"),
    ("drivers/pci_decode.c", r"return \(u16\)\(dword >> \(\(reg & 2\) \* 8\)\);",
     "return (u16)(dword >> ((reg & 3) * 8));",
     "16 ビットの切り出しをバイト境界で行う (奇数オフセットで値がずれる)"),
    ("drivers/pci_decode.c", r'if \(sub == PCI_SUB_BRIDGE_ISA\) return "Bridge/ISA";',
     "",
     "ブリッヂのサブクラスを見ない (Host / ISA / PCI が全部同じ名前になる)"),
    # --- `lspci -v` (userland/shell/pci_verbose.c) ---
    ("userland/shell/pci_verbose.c",
     r"if \(layout == PCI_HDR_LAYOUT_BRIDGE\) return PCI_BRIDGE_BAR_COUNT;",
     "if (layout == PCI_HDR_LAYOUT_BRIDGE) return PCI_CFG_BAR_COUNT;",
     "ブリッヂにも BAR を 6 本読む (0x18 のバス番号を bar2 と偽って出す)"),
    ("userland/shell/pci_verbose.c",
     r"if \(pv_bar_is_mem64_hi\(bar, count, n\)\) \{",
     "if (0 && pv_bar_is_mem64_hi(bar, count, n)) {",
     "64 ビット BAR の上位半分を見分けない (上位の 0 が none に化ける)"),
    ("userland/shell/pci_verbose.c",
     r"if \(layout == PCI_HDR_LAYOUT_DEVICE\) \{\n            u32 sw",
     "if (layout != PCI_HDR_LAYOUT_CARDBUS) {\n            u32 sw",
     "ブリッヂでも 0x2C を Subsystem と読む (プリフェッチ窓の上位を ID と偽る)"),
    ("userland/shell/pci_verbose.c",
     r"if \(pci_bar_base\(raw\) == 0 && hi == 0\)",
     "if (pci_bar_base(raw) == 0)",
     "mem64 の未割り当てを下位だけで決める (4G 超の窓を未割り当てと偽る、Codex P2)"),
    # --- コマンド層 (userland/shell/cmd_pci.c) ---
    ("userland/shell/cmd_pci.c",
     r"    /\* \*\*引数を先に検査する。\*\*",
     "    if (g_api->pci_count() <= 0) {\n"
     "        g_api->kprintf(ATTR_CYAN, \"%s\", \"lspci: no PCI\");\n"
     "        return 0;\n"
     "    }\n"
     "    /* **引数を先に検査する。**",
     "PCI の有無を引数の検査より先に見る (NP21/W で打ち間違いが成功になる、Codex P3)"),
    ("userland/shell/cmd_pci.c",
     r"for \(k = 0; k < PCI_VERBOSE_CFG_DWORDS; k\+\+\)\n        cfg\[k\]",
     "for (k = 0; k < PCI_VERBOSE_CFG_DWORDS - 1; k++)\n        cfg[k]",
     "config を 0x3C まで読まない (Interrupt Line / Pin が 0 に化ける)"),
]


# ハーネスが #include する実物。変異のときはこれを一時の木へ写し、1 本だけ
# 差し替える (ハーネスの "../../drivers/..." がそのまま一時の木を指す)。
MIRROR = ["tools/tests/pci_decode_host.c", "tools/tests/lspci_cmd_host.c",
          "drivers/pci_decode.c", "drivers/pci_decode.h",
          "userland/shell/pci_verbose.c", "userland/shell/pci_verbose.h",
          "userland/shell/cmd_pci.c", "userland/shell/shell.h"]


def host_build(tmp, mutated=None):
    """2 本のハーネスをコンパイルして {"decode": exe, "cmd": exe} を返す。
    mutated = (相対パス, 本文) なら、そのファイルだけ差し替えた木で通す。"""
    src_dir = pathlib.Path(tmp)
    root, inc = ROOT, INCLUDES
    if mutated is not None:
        tree = src_dir / "tree"
        for rel in MIRROR:
            dst = tree / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            text = (ROOT / rel).read_text(encoding="utf-8")
            if rel == mutated[0]:
                text = mutated[1]
            dst.write_text(text, encoding="utf-8")
        root = tree
        inc = ["-I" + str(ROOT / "include"), "-I" + str(tree / "drivers"),
               "-I" + str(tree)]
    exes = {}
    for key, harness in (("decode", HARNESS), ("cmd", CMD_HARNESS)):
        exe = src_dir / f"pci-{key}-host"
        src = root / harness.relative_to(ROOT)
        subprocess.run(["gcc", *FLAGS, *inc, str(src), "-o", str(exe)],
                       cwd=ROOT, check=True)
        exes[key] = exe
    return exes


def exe_for(exes, case):
    return exes["cmd"] if case in CMD_CASES else exes["decode"]


def run_cases(exes, cases):
    failed = 0
    for case in cases:
        rc = subprocess.run([str(exe_for(exes, case)), case], cwd=ROOT).returncode
        print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def build_target(tmp):
    """カーネルと同じ i386-elf で drivers/pci.c ごと通す。"""
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


def mutate(tmp):
    """実装を 1 か所ずつ壊して、どれも RED になることを見る。"""
    bad = 0
    for i, (rel, pattern, repl, why) in enumerate(MUTATIONS, 1):
        original = (ROOT / rel).read_text(encoding="utf-8")
        mutated, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        exes = host_build(tmp, (rel, mutated))
        hits = sum(subprocess.run([str(exe_for(exes, c)), c], cwd=ROOT,
                                  stderr=subprocess.DEVNULL).returncode != 0
                   for c in ALL_CASES)
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-pci-decode-") as tmp:
        exes = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real drivers/pci_decode.c, "
              "userland/shell/pci_verbose.c, userland/shell/cmd_pci.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exes, [a for a in args if not a.startswith("--")] or ALL_CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
