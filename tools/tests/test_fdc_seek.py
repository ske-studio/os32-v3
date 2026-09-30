"""FDC の純粋な判定 (drivers/fdc_decide.c) のホスト試験。

記録: tools/tests/fdc_seek_tdd.md
票  : 実機 PC-9821Ra266 の FD 起動が MOUNT... の root panic になる件

実物の drivers/fdc_decide.c を 1 行も写さずに #include して回す。
判定は I/O もタイマも触らないので模型は要らない。

  python3 -B tools/tests/test_fdc_seek.py            # ホストで全ケース
  python3 -B tools/tests/test_fdc_seek.py --target   # + i386-elf で fdc.c も通す
  python3 -B tools/tests/test_fdc_seek.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/fdc_seek_host.c"
SRC = ROOT / "drivers/fdc_decide.c"
# カーネルと同じ i386-elf で通す実ソース。
# fdc.c だけ -Wno-unused-function を足す: fdc_motor_off() が **元から**
# 未使用の static で (票の指示どおり触っていない)、カーネル本体のビルドは
# -Werror ではないので今まで表に出ていなかった。他の警告は落とす。
TARGET_SRCS = [
    ("drivers/fdc_decide.c", []),
    ("drivers/fdc.c", ["-Wno-unused-function"]),
]

CASES = ["sis_len", "seek_ok", "seek_ec", "seek_pending", "seek_fail",
         "seek_not_ready", "real_hw_story", "other_drive"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p) for p in ("include", "drivers")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (パターン, 置換, 説明)
MUTATIONS = [
    (r"return FDC_SIS_LEN_INVALID;", "return FDC_SIS_LEN_NORMAL;",
     "pending 無しでも PCN を読みに行く (= 直す前の姿)"),
    (r"if \(\(st0 & FDC_ST0_EC\) != 0\) \{\n        return FDC_SEEK_RETRY_EC;\n    \}", "",
     "EC を失敗として扱う (80 シリンダ媒体で RECALIBRATE が通らない)"),
    (r"return FDC_SEEK_PENDING;", "return FDC_SEEK_FAIL;",
     "未完了 (ST0=80h) を失敗と区別しない"),
    (r"if \(\(st0 & FDC_ST0_NR\) != 0\) \{\n        return FDC_SEEK_NOT_READY;\n    \}", "",
     "Not Ready を見ない (ディスク無しを完了にする)"),
    (r"return FDC_SEEK_NOT_READY;", "return FDC_SEEK_FAIL;",
     "NR を普通の失敗と混ぜる (空ドライブで回復を 3 回踏む)"),
]


def host_build(tmp, source_text=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。"""
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "fdc-seek-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None:
        # 変異させた fdc_decide.c を一時の drivers/ に置いて、そちらを先に引かせる。
        mut = src_dir / "drivers"
        mut.mkdir(exist_ok=True)
        (mut / "fdc_decide.c").write_text(source_text, encoding="utf-8")
        (mut / "fdc_decide.h").write_text(
            (ROOT / "drivers/fdc_decide.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../drivers/fdc_decide.c"', '"drivers/fdc_decide.c"'),
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
    """カーネルと同じ i386-elf で drivers/fdc.c ごと通す。"""
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
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        mutated, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        exe = host_build(tmp, mutated)
        hits = sum(subprocess.run([str(exe), c], cwd=ROOT,
                                  stderr=subprocess.DEVNULL).returncode != 0
                   for c in CASES)
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-fdc-seek-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real drivers/fdc_decide.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
