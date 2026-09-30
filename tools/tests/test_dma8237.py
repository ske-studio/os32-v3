"""8237 DMA 共通部の算数 (drivers/dma8237_math.c) のホスト試験。

記録: tools/tests/dma8237_tdd.md
票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-2 (8237 DMA の共通部)

実物の drivers/dma8237_math.c を 1 行も写さずに #include して回す。I/O を
出さないので模型は 1 つも要らない。

**NP21/W では踏めない分岐がここの主目的**: エミュレータは 8237 の 64KB
折り返しも 16MB の壁も模擬しないので、またいだ転送が「たまたま読めて」
しまう (§4-51 と同じ型)。断る規則そのものをホストで固定する。

  python3 -B tools/tests/test_dma8237.py            # ホストで全ケース
  python3 -B tools/tests/test_dma8237.py --target   # + i386-elf で実物を通す
  python3 -B tools/tests/test_dma8237.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/dma8237_host.c"
SRC = ROOT / "drivers/dma8237_math.c"
TARGET_SRCS = ["drivers/dma8237_math.c", "drivers/dma8237.c"]

CASES = ["port_table", "split_addr", "crosses", "count_bytes",
         "accept_pair", "check_args", "mode_bytes"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "drivers", "sdk/include/os32")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
MUTATIONS = [
    (r"    if \(bytes == 0\) return 1;\n", "",
     "0 バイトを「またがない」と答える (積むと 65536 バイト転送になる)"),
    (r"    if \(bytes > \(u32\)\(DMA_BANK_SIZE - off\)\) return 1;",
     "    if (bytes > (u32)(DMA_BANK_SIZE - off) + 1) return 1;",
     "またぎ判定が 1 バイトぶん緩い (末尾 1 バイトが別バンクへ折り返す)"),
    (r"    if \(c1 > limit \|\| c2 > limit\) return 0;\n", "",
     "設定長を超えた読みを採用する (TC 後の FFFFh を残量と読む)"),
    (r"    if \(c1 < c2\) return 0;",
     "    if (c1 < c2) return 1;",
     "増えたカウントを採用する (再ロードをまたいだ合成を残量と読む)"),
    (r"\{ DMA8237_CH0_ADDR, DMA8237_CH0_COUNT, DMA8237_CH0_BANK \}",
     "{ DMA8237_CH0_ADDR, DMA8237_CH0_COUNT, DMA8237_CH1_BANK }",
     "ch0 のバンクを等差数列で出す (0027h ではなく 0021h = ch1 を壊す)"),
    # 16MB の壁は 2 本 (先頭と終端) で見ていて、**互いに相手を覆う**ので
    # 片方ずつ消しても挙動が変わらない。2 本まとめて消して RED にする。
    (r"    if \(phys >= DMA_PHYS_LIMIT\) return DMA_ERR_ARG;\n"
     r"    if \(bytes > DMA_PHYS_LIMIT - phys\) return DMA_ERR_ARG;\n", "",
     "16MB の壁を見ない (バンクレジスタ 8bit では届かない番地へ積む)"),
    (r"    v \|= \(dir == DMA_DIR_FROM_MEM\) \? DMA8237_MODE_TR_READ\n"
     r"                                   : DMA8237_MODE_TR_WRITE;",
     "    v |= (dir == DMA_DIR_FROM_MEM) ? DMA8237_MODE_TR_WRITE\n"
     "                                   : DMA8237_MODE_TR_READ;",
     "転送の向きを取り違える (読んだつもりで書き、ディスクを潰す)"),
    (r"    if \(dma_crosses_64k\(phys, bytes\)\) return DMA_ERR_ARG;\n", "",
     "64KB またぎを検査しない ([HW2] そのもの)"),
]


def host_build(tmp, source_text=None):
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "dma8237-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        mut = src_dir / "drivers"
        mut.mkdir(exist_ok=True)
        (mut / "dma8237_math.c").write_text(source_text, encoding="utf-8")
        (mut / "dma8237.h").write_text(
            (ROOT / "drivers/dma8237.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../drivers/dma8237_math.c"', '"drivers/dma8237_math.c"'),
            encoding="utf-8")
        cmd = ["gcc", *FLAGS, "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(src_dir),
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
    """カーネルと同じ i386-elf で I/O を出す側 (dma8237.c) ごと通す。"""
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
    with tempfile.TemporaryDirectory(prefix="os32-dma8237-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real drivers/dma8237_math.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
