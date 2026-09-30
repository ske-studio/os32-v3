"""ブート情報域 (0x7E00) の検証関数 (kernel/bootinfo_check.c) のホスト試験。

記録: tools/tests/bootinfo_tdd.md
票  : docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 0 (§1-v3 N1 / N2)

実物の kernel/bootinfo_check.c を 1 行も写さずに #include して回す。
加えて **NASM 側の写し boot/bootinfo.inc** の値が include/bootinfo.h と名前ごとに
一致するかを見る (ローダとカーネルが別のオフセットを読むと静かにずれる)。

  python3 -B tools/tests/test_bootinfo.py            # ホストで全ケース
  python3 -B tools/tests/test_bootinfo.py --target   # + i386-elf で bootinfo.c / ide.c も通す
  python3 -B tools/tests/test_bootinfo.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/bootinfo_host.c"
SRC = ROOT / "kernel/bootinfo_check.c"
INC = ROOT / "boot/bootinfo.inc"
# i386-elf で通す実ソース。bootinfo.c は構造体の並びを STATIC_ASSERT で見る
# (ホストでは u32 が 64bit なので見られない)。
TARGET_SRCS = ["kernel/bootinfo_check.c", "kernel/bootinfo.c", "drivers/ide.c"]

CASES = ["good", "magic", "check_word", "version", "sum", "rules", "format",
         "image", "inc_mirror"]

FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / "include")]

# 否定側。(パターン, 置換, 説明)
MUTATIONS = [
    (r"    if \(rd32\(raw \+ BI_OFF_CHECK\) != \(u32\)BOOTINFO_CHECK\) \{\n"
     r"        out->status = BOOTINFO_ERR_CHECK;\n        return out->status;\n    \}\n",
     "", "反転チェック語を見ない (書きかけの域を受け入れる)"),
    (r"    if \(d->seclen != \(u16\)BOOTINFO_SECLEN\) return 0;\n", "",
     "BX (セクタ長) を見ない (SASI 256B / CD 2048B を 512 として使う)"),
    (r"    if \(d->heads == 0\) return 0;\n", "", "DH = 0 を通す (0 で割る)"),
    (r"    if \(d->spt == 0\) return 0;\n", "", "DL = 0 を通す (0 で割る)"),
    (r"    if \(rd16\(raw \+ BI_OFF_SUM\) != bootinfo_sum\(raw\)\) \{\n"
     r"        out->status = BOOTINFO_ERR_SUM;\n        return out->status;\n    \}\n",
     "", "ドライブ記録の和を見ない (部分的な上書きを通す)"),
    (r"    if \(out->version != \(u16\)BOOTINFO_VERSION\) \{\n"
     r"        out->status = BOOTINFO_ERR_VERSION;\n        return out->status;\n    \}\n",
     "", "版を見ない"),
    (r"    if \(d->cf != 0\) return 0;\n", "", "CF = 1 (取得失敗) を使える扱いにする"),
    (r"d->valid = \(u8\)\(\(d->loader_valid == 1 && bootinfo_drive_usable\(d\)\) \? 1 : 0\);",
     "d->valid = d->loader_valid;", "ローダの valid を鵜呑みにする"),
    (r"d->valid = \(u8\)\(\(d->loader_valid == 1 && bootinfo_drive_usable\(d\)\) \? 1 : 0\);",
     "d->valid = (u8)bootinfo_drive_usable(d);", "ローダが 0 と書いたものを拾い直す"),
    (r"    if \(!d->queried\) return 0;\n", "", "問い合わせていないドライブを使う"),
    # イメージ欄 (v2、票 TASK_SERIAL_HOSTFS A-4)
    (r"rd32\(raw \+ BI_OFF_IMG_CHECK\) == BOOTINFO_IMG_CHECK_OF\(crc, size\)",
     "1", "イメージ欄のチェック語を見ない (書きかけ・化けた CRC を記録ありにする)"),
    (r"        if \(size != 0 &&\n", "        if (",
     "大きさ 0 を記録ありにする"),
    (r"    out->status = BOOTINFO_OK;\n    return out->status;",
     "    out->status = BOOTINFO_OK;\n    out->img_crc ^= 1;\n    return out->status;",
     "記録した CRC を 1 ビット化かして返す"),
]

# NASM 側の写しを 1 つずつ壊す (inc_mirror が RED になること)
INC_MUTATIONS = [
    (r"BI_OFF_CHECK        EQU 0x2C", "BI_OFF_CHECK        EQU 0x28",
     "check の位置を sum に重ねる"),
    (r"BOOTINFO_CHECK      EQU 0xB6ABB0BF", "BOOTINFO_CHECK      EQU 0xB6ABB0BE",
     "反転語の値が 1 ビット違う"),
    (r"BI_DRV_DL           EQU 0x09", "BI_DRV_DL           EQU 0x08",
     "DL を DH と同じ位置に書く"),
    (r"BI_OFF_IMG_CHECK    EQU 0x38", "BI_OFF_IMG_CHECK    EQU 0x34",
     "イメージ欄のチェック語を大きさの位置に書く"),
    (r"BOOTINFO_IMG_KEY    EQU 0x43474D49", "BOOTINFO_IMG_KEY    EQU 0x43474D48",
     "イメージ欄の鍵が 1 ビット違う"),
    (r"BOOTINFO_VERSION    EQU 2", "BOOTINFO_VERSION    EQU 1",
     "ローダが旧版 (v1) を名乗る"),
]


def host_build(tmp, source_text=None):
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "bootinfo-host"
    harness = HARNESS
    if source_text is not None:
        mut = src_dir / "kernel"
        mut.mkdir(exist_ok=True)
        (mut / "bootinfo_check.c").write_text(source_text, encoding="utf-8")
        harness = src_dir / "harness.c"
        harness.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../kernel/bootinfo_check.c"', '"kernel/bootinfo_check.c"'),
            encoding="utf-8")
    cmd = ["gcc", *FLAGS, *INCLUDES, "-I" + str(src_dir), str(harness),
           "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return exe


def parse_inc(text):
    out = {}
    for line in text.splitlines():
        m = re.match(r"^\s*([A-Z_][A-Z0-9_]*)\s+EQU\s+(\S+)", line)
        if m:
            v = m.group(2)
            out[m.group(1)] = int(v, 16) if v.lower().startswith("0x") else int(v)
    return out


def inc_mirror(exe, inc_text=None):
    """boot/bootinfo.inc の値が include/bootinfo.h と一致するか。"""
    dump = subprocess.run([str(exe), "dump"], cwd=ROOT, check=True,
                          capture_output=True, text=True).stdout
    want = {}
    for line in dump.splitlines():
        k, v = line.split("=")
        want[k] = int(v)
    got = parse_inc(inc_text if inc_text is not None
                    else INC.read_text(encoding="utf-8"))
    bad = 0
    for name, val in got.items():
        if name == "MEM_BOOTINFO_BASE":
            continue  # 番地は tools/gen_memmap.py --check が見る
        if name not in want:
            print(f"  inc_mirror: {name} はヘッダ側に無い (dump に足すこと)",
                  file=sys.stderr)
            bad += 1
        elif want[name] != val:
            print(f"  inc_mirror: {name} inc=0x{val:X} h=0x{want[name]:X}",
                  file=sys.stderr)
            bad += 1
    # ローダが使う名前が inc に全部あること
    for name in ("BOOTINFO_MAGIC", "BOOTINFO_CHECK", "BI_OFF_CHECK", "BI_OFF_SUM",
                 "BI_DRV_BX", "BI_DRV_DH", "BI_DRV_DL", "BI_DRV_QUERIED",
                 "BI_OFF_IMG_CRC", "BI_OFF_IMG_SIZE", "BI_OFF_IMG_CHECK",
                 "BOOTINFO_IMG_KEY"):
        if name not in got:
            print(f"  inc_mirror: {name} が inc に無い", file=sys.stderr)
            bad += 1
    return 1 if bad else 0


def run_cases(exe, cases):
    failed = 0
    for case in cases:
        if case == "inc_mirror":
            rc = inc_mirror(exe)
        else:
            rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
        print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def build_target(tmp):
    for rel in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror", 
               "-D__KERNEL_BUILD__",
               "-I" + str(ROOT), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "kernel"),
               "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "lib"),
               "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        # ide.c には既存の未使用関数 (ide_wait_ready) の警告があるので
        # それだけ黙らせる (この票で増やした警告ではない)。
        if rel == "drivers/ide.c":
            cmd.insert(cmd.index("-Werror") + 1, "-Wno-unused-function")
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU11 -Werror PASS", flush=True)


def mutate(tmp):
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    host_cases = [c for c in CASES if c != "inc_mirror"]
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        mutated, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, mutated)
        except subprocess.CalledProcessError:
            # 組めない変異は何も確かめていない — RED に数えない
            print(f"MUTATION {i} ERROR (compile): {why}", flush=True)
            bad += 1
            continue
        hits = sum(subprocess.run([str(exe), c], cwd=ROOT,
                                  stderr=subprocess.DEVNULL).returncode != 0
                   for c in host_cases)
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    exe = host_build(tmp)
    inc = INC.read_text(encoding="utf-8")
    for j, (old, new, why) in enumerate(INC_MUTATIONS, 1):
        if old not in inc:
            print(f"INC MUTATION {j} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        import contextlib
        import io
        with contextlib.redirect_stderr(io.StringIO()):
            rc = inc_mirror(exe, inc.replace(old, new, 1))
        status = "RED" if rc else "**GREEN (見逃し)**"
        print(f"INC MUTATION {j} {status}: {why}", flush=True)
        bad += not rc
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-bootinfo-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU11 -Werror compile PASS (real kernel/bootinfo_check.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
