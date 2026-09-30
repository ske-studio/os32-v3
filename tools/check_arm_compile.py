#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_arm_compile.py — カーネル C ソースの ARM 移植度を測る (合否ではなく計測)

将来の ARM 移植に向けた準備作業の「順序 1」。カーネル側の C ソースを 1 本ずつ
`arm-none-eabi-gcc` に通し、**何本が素通りするか**と、通らなかったものの
**最初のエラー 1 行**を出す。以後の作業 (io.h 経由への統一、arch/ の導入、
LE アクセサ化) の効果を同じ物差しで見るための計測器。

これは `make check` の列には入れない。ARM で通らないのは現時点では当たり前で、
落ちても OS32 のビルドは壊れていない。合否の門ではなく、動かしたい数字を出す
ものなので、`make check-arm-compile` として独立させてある。

## 何をどう測るか

  * 対象は `build/kernel.mk` の `C_KERNEL` — カーネルに実際にリンクされる
    C ソースの正典。ディレクトリを glob すると、カーネルに入らない
    vendored ライブラリ (下記) まで数に混ざる。
  * インクルードパスとフラグは `build/config.mk` の `INC_*` /
    `CFLAGS_COMMON` を**読んで**使う。ここに書き写すと config.mk と二重管理に
    なり、片方だけ直ったときに黙ってずれる。
  * x86 専用のフラグ (`-m32` `-march=i386` `-mno-red-zone`) だけを外す。
    残りはカーネル本番ビルドと同じ素性で測る。
  * `-fsyntax-only` ではなく `-c` (コード生成まで) を使う。`-fsyntax-only` は
    インライン asm の中身を見ないので、x86 命令がそのまま素通りしてしまい、
    「通った本数」が実態より大きく出る。

## 対象から外しているもの (理由つき)

  * `lib/sqlite3/`  — vendored SQLite アマルガメーション。移植性は上流が見て
    いて ANSI C で書かれており、こちらで直す対象ではない。25 万行あるので
    計測時間だけが延びる。
  * `lib/zlib/` `lib/microtar/` `lib/lz4.c` — vendored だが**カーネルには
    リンクされない** (ユーザーランド用。`build/libs.mk` /
    `build/programs.mk` が `PROGRAM_FLAGS` で別に組む)。カーネルの移植度の
    数字に混ぜる意味がない。

対象集合が `C_KERNEL` から導かれる以上、この一覧は「glob には出るが数えない
もの」の説明でしかない。glob に**未知の** `*.c` が増えたときは警告を出して
気づけるようにしてある (DRIFT)。

## 使い方

    python3 tools/check_arm_compile.py           # 人が読む表
    python3 tools/check_arm_compile.py --json     # 機械可読
    ARM_CC=/path/to/gcc python3 tools/check_arm_compile.py

`arm-none-eabi-gcc` が無い環境では SKIP と出して終了コード 0。計測器なので、
道具が無いことは失敗ではない。
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

from concurrent.futures import ThreadPoolExecutor

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CONFIG_MK = os.path.join(PROJ_DIR, "build", "config.mk")
KERNEL_MK = os.path.join(PROJ_DIR, "build", "kernel.mk")

# 既定のコンパイラ。ARM_CC で上書きできる。
DEFAULT_ARM_CC = "arm-none-eabi-gcc"

# glob 対象のディレクトリ (DRIFT 検出用。計測の対象集合ではない)
SCAN_DIRS = ["kernel", "drivers", "fs", "exec", "kapi", "lib", "net", "gfx"]

# C_KERNEL に無くても DRIFT として騒がないもの = 上の docstring で
# 理由を書いた除外。接頭辞かちょうど一致で判定する。
KNOWN_NON_KERNEL = [
    "lib/sqlite3/",
    "lib/zlib/",
    "lib/microtar/",
    "lib/lz4.c",
]

# x86 専用で ARM に持っていけないフラグ。CFLAGS_COMMON からこれだけ落とす。
X86_ONLY_FLAGS = ["-m32", "-march=i386", "-mno-red-zone"]

# ヘッダ依存の .d を吐くフラグ。出力先が /dev/null なので外す。
DEPFLAGS_DROP = ["-MMD", "-MP"]

# CFLAGS_COMMON に足してカーネル本番ビルド (KERNEL_CFLAGS) に揃えるもの。
# -Wall は付けない — 測るのはエラーであって警告ではなく、出力が膨らむだけ。
KERNEL_EXTRA_FLAGS = ["-O2", "-D__KERNEL_BUILD__"]

# --- ファイル -> インクルード変数の対応 (build/kernel.mk のルールを写したもの) ---
# drivers/ の 5 本だけは kernel.mk が個別ルールで INC_KERNEL を渡している
# (idt.h / vfs.h / cpu_calibrate.h を見るため)。
DRIVERS_INC_KERNEL = [
    "drivers/mouse.c",
    "drivers/loop_dev.c",
    "drivers/ne2000.c",
    "drivers/atapi.c",
    "drivers/lgy98.c",
]

# 先に一致したものを採る。fs/fatfs/ は fs/ より先でなければならない。
INC_RULES = [
    ("fs/fatfs/", "INC_FATFS"),
    ("kernel/", "INC_KERNEL"),
    ("net/", "INC_KERNEL"),
    ("drivers/", "INC_DRIVERS"),
    ("gfx/", "INC_GFX"),
    ("fs/", "INC_FS"),
    ("exec/", "INC_EXEC"),
    ("kapi/", "INC_KAPI"),
    ("lib/", "INC_LIB"),
]

# --- 失敗の分類 ---
# 最初のエラー 1 行だけを見て機械的に決める。**上から順に当てて最初の一致を
# 採る**。io.h は asm エラーでもあるので、asm より先に見ないと全部 (a) に
# 吸われて「io.h を分離すれば何本通るか」が見えなくなる。
#
# 順序 3 で io.h は契約 (include/io.h) と実装 (arch/<arch>/arch_io.h,
# platform/<platform>/platform_io.h) に分かれた。エラーが出るのは実装側の
# 2 ファイルなので、両方をこの分類に含める。含めないと 33 本がまるごと
# (a) へ移ってしまい、順序 1 の基準値と比べられなくなる。
#
# 順序 5 で cr3 / CR0 / GDT / TSS / リング降下の asm も arch/x86/ へ移した。
# 分類 (a) の意味は「差し替え先がまだ決まっていない、その場の x86 asm」で、
# arch/ や platform/ の下に入ったものはもう差し替えの境界に載っている。
# だから **arch/ か platform/ のファイルを指すエラーは全部この分類**に入れる
# (基底名の一覧では arch_cpu.h / x86_desc.h を取りこぼす)。
IO_BASENAMES = ("io.h", "arch_io.h", "platform_io.h")
IMPL_DIRS = ("arch/", "platform/")

CAT_IO = "io"
CAT_ASM = "asm"
CAT_X86HDR = "x86hdr"
CAT_OTHER = "other"

CAT_LABEL = {
    CAT_IO: "(b) arch/ platform/ の実装経由",
    CAT_ASM: "(a) インライン asm (x86 命令・レジスタ)",
    CAT_X86HDR: "(c) x86 固有ヘッダ・型",
    CAT_OTHER: "(d) その他",
}

CAT_ORDER = [CAT_IO, CAT_ASM, CAT_X86HDR, CAT_OTHER]

# エラー行の形: "path:line:col: error: msg" / "path:line: Error: msg" (アセンブラ)
ERROR_LINE_RE = re.compile(r"^(?P<path>[^:]+):\d+(?::\d+)?:\s+(?:error|fatal error|Error):")

ASM_RE = re.compile(
    # コンパイル段で落ちるもの (制約が ARM に無い、レジスタ名が x86)
    r"impossible constraint in 'asm'"
    r"|unknown register name"
    r"|inconsistent operand constraints"
    r"|invalid 'asm'"
    r"|'asm' operand"
    r"|operand number out of range"
    r"|undefined named operand"
    # アセンブル段で落ちるもの (制約は通ったが命令が x86)
    r"|no such instruction"
    r"|bad register name"
    r"|unknown mnemonic"
    r"|bad instruction"
    r"|ARM register expected"
    r"|garbage following instruction"
    r"|selected processor does not support"
)

X86HDR_RE = re.compile(
    r"No such file or directory"
    r"|unknown type name"
)


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def parse_make_vars(text):
    """`NAME = value` / `NAME := value` / `NAME ?= value` を拾う。
    行末 `\\` の継続をつなぐ。

    展開はしない (expand_var が必要になった時点で再帰的に行う)。`+=` は
    無視する — 追記の意味を持つので「最初の 1 回だけ採る」この読み方と
    合わない。

    `?=` を `=` と同じに扱うのは、この計測器が config.mk を**単独で**
    読むから。make なら「まだ定義されていなければ」の条件が付くが、ここでは
    先に定義するものが無いので必ず既定値が採られる。順序 3 の
    `ARCH ?= x86` / `PLATFORM ?= pc98` はこれで読める (読めないと
    `-Iarch/$(ARCH)` が `-Iarch/` に潰れ、io.h が実装を見つけられずに
    33 本が「ヘッダが無い」で落ちて計測が無意味になる)。
    """
    text = re.sub(r"\\\n", " ", text)
    out = {}
    for line in text.split("\n"):
        line = line.split("#", 1)[0]
        m = re.match(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?::|\?)?=\s*(.*)$", line)
        if m:
            name, value = m.group(1), m.group(2).strip()
            if name not in out:
                out[name] = value
    return out


def expand_var(varmap, name, depth=0):
    """`$(OTHER)` を再帰的に展開する。未知の変数は空に落とす。"""
    if depth > 16 or name not in varmap:
        return ""
    value = varmap[name]

    def sub(m):
        inner = m.group(1)
        if inner.startswith("shell ") or "(" in inner:
            return ""
        return expand_var(varmap, inner, depth + 1)

    return re.sub(r"\$\(([^()]*)\)", sub, value)


def parse_kernel_vars(text):
    """build/kernel.mk の変数。ARCH の分岐で 2 回代入されるものは
    **空でない方**を採る。

    順序 4-b で `KSTRING_C_SRC` が入った — ARCH が x86 なら空 (kstring は
    lib/kstring_asm.asm のアセンブリ)、それ以外なら lib/kstring_c.c。この
    計測器が測るのは **ARM のビルド**なので、x86 側の空の代入ではなく
    非 x86 側を採るのが正しい。ifeq を本気で評価するのは大げさなので、
    「同じ名前に空と非空があったら非空」という単純な規則で足りる。
    """
    text = re.sub(r"\\\n", " ", text)
    out = {}
    for line in text.split("\n"):
        line = line.split("#", 1)[0]
        m = re.match(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?::|\?)?=\s*(.*)$", line)
        if not m:
            continue
        name, value = m.group(1), m.group(2).strip()
        if name not in out or (value and not out[name]):
            out[name] = value
    return out


def parse_c_kernel(text):
    """build/kernel.mk の C_KERNEL (カーネルにリンクされる C ソースの正典)。

    `$(VAR)` の形で入っているものは build/kernel.mk 自身の代入から解決する
    (順序 4-b の `$(KSTRING_C_SRC)`)。"""
    varmap = parse_kernel_vars(text)
    joined = re.sub(r"\\\n", " ", text)
    m = re.search(r"^\s*C_KERNEL\s*:?=\s*(.*)$", joined, re.M)
    if not m:
        return []
    files = []
    for tok in m.group(1).split():
        ref = re.match(r"^\$\(([A-Za-z_][A-Za-z0-9_]*)\)$", tok)
        if ref:
            files.extend(varmap.get(ref.group(1), "").split())
        else:
            files.append(tok)
    return sorted(set(files))


def scan_glob():
    """SCAN_DIRS 下の *.c を全部。DRIFT 検出にだけ使う。"""
    found = []
    for d in SCAN_DIRS:
        root_dir = os.path.join(PROJ_DIR, d)
        if not os.path.isdir(root_dir):
            continue
        for root, _dirs, files in os.walk(root_dir):
            for f in files:
                if f.endswith(".c"):
                    rel = os.path.relpath(os.path.join(root, f), PROJ_DIR)
                    found.append(rel.replace(os.sep, "/"))
    return sorted(found)


def is_known_non_kernel(path):
    for pref in KNOWN_NON_KERNEL:
        if path == pref or path.startswith(pref):
            return True
    return False


def inc_var_for(path):
    if path in DRIVERS_INC_KERNEL:
        return "INC_KERNEL"
    for prefix, var in INC_RULES:
        if path.startswith(prefix):
            return var
    return "INC_KERNEL"


def build_flags(varmap):
    """CFLAGS_COMMON から x86 専用と依存生成を落とし、カーネル素性を足す。"""
    common = expand_var(varmap, "CFLAGS_COMMON").split()
    kept = [f for f in common
            if f not in X86_ONLY_FLAGS and f not in DEPFLAGS_DROP]
    dropped = [f for f in common if f in X86_ONLY_FLAGS]
    return kept + KERNEL_EXTRA_FLAGS, dropped


def first_error(output):
    for line in output.split("\n"):
        line = line.rstrip()
        if ERROR_LINE_RE.match(line):
            return line.strip()
    # エラー行の形をしていないが失敗した場合 (リンカ・内部エラーなど)
    for line in output.split("\n"):
        if line.strip():
            return line.strip()
    return "(エラー出力なし)"


def classify(err_line):
    m = ERROR_LINE_RE.match(err_line)
    path = m.group("path") if m else ""
    norm = path.replace(os.sep, "/")
    if os.path.basename(path) in IO_BASENAMES or norm.startswith(IMPL_DIRS):
        return CAT_IO
    if ASM_RE.search(err_line):
        return CAT_ASM
    if X86HDR_RE.search(err_line):
        return CAT_X86HDR
    return CAT_OTHER


def _run(cmd):
    return subprocess.run(cmd, cwd=PROJ_DIR, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, universal_newlines=True)


def is_temp_asm(path):
    """gcc が作った中間 .s か。プロジェクト内のファイルなら違う。"""
    return path.endswith(".s") and not os.path.exists(os.path.join(PROJ_DIR, path))


def resolve_asm_origin(cc, flags, inc, path):
    """アセンブル段のエラーを元のソース行に戻す。

    `inb` のように**制約**が ARM に無いものはコンパイル段で落ちるので、
    エラー行が `include/io.h:14` と出て分類できる。ところが `pushfl` /
    `sti` / `mov %cr3` のように制約 (`"=r"`) だけは ARM でも通るものは
    コンパイル段を素通りし、gcc の中間 `.s` を指した
    `/tmp/ccXXXXXX.s:96: Error: bad instruction 'pushfl'` になる。
    これでは (1) どのヘッダ由来か分からず「io.h を分離したら何本通るか」が
    測れない、(2) 一時ファイル名が毎回変わって記録が差分で追えない。

    そこで `-g -S` で .s を自分の場所に出し、それをアセンブルし直して
    エラー行を得てから、直前の `.loc <ファイル番号> <行>` と
    `.file <番号> "パス"` の表で元のソースに戻す。-g はコード生成を
    変えないので、測っている対象は同じ。
    """
    fd, spath = tempfile.mkstemp(suffix=".s", prefix="armgauge_")
    os.close(fd)
    try:
        gen = _run([cc] + flags + inc + ["-g", "-S", "-o", spath, path])
        if gen.returncode != 0:
            return None
        asm = _run([cc, "-c", "-o", os.devnull, spath])
        m = None
        for line in asm.stdout.split("\n"):
            m = re.match(re.escape(spath) + r":(\d+):\s+(?:Error|error):\s*(.*)$",
                         line.strip())
            if m:
                break
        if not m:
            return None
        err_line, msg = int(m.group(1)), m.group(2)

        with open(spath, "r", encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")

        files = {}
        for line in lines:
            fm = re.match(r'^\s*\.file\s+(\d+)\s+"([^"]*)"(?:\s+"([^"]*)")?', line)
            if fm:
                idx, a, b = fm.group(1), fm.group(2), fm.group(3)
                files[idx] = os.path.join(a, b) if b else a

        for i in range(min(err_line, len(lines)) - 1, -1, -1):
            lm = re.match(r"^\s*\.loc\s+(\d+)\s+(\d+)", lines[i])
            if lm:
                origin = files.get(lm.group(1))
                if origin:
                    return "%s:%s: Error: %s" % (origin, lm.group(2), msg)
                break
        return None
    finally:
        if os.path.exists(spath):
            os.remove(spath)


def compile_one(cc, flags, incmap, path):
    inc = incmap[inc_var_for(path)].split()
    proc = _run([cc] + flags + inc + ["-c", "-o", os.devnull, path])
    if proc.returncode == 0:
        return {"file": path, "ok": True}
    err = first_error(proc.stdout)
    m = ERROR_LINE_RE.match(err)
    if m and is_temp_asm(m.group("path")):
        resolved = resolve_asm_origin(cc, flags, inc, path)
        if resolved:
            err = resolved
    return {"file": path, "ok": False, "error": err, "category": classify(err)}


def main():
    ap = argparse.ArgumentParser(description="カーネル C ソースの ARM 移植度を測る")
    ap.add_argument("--json", action="store_true", help="機械可読な JSON を出す")
    ap.add_argument("--jobs", type=int, default=0, help="並列数 (既定: CPU 数)")
    args = ap.parse_args()

    cc = os.environ.get("ARM_CC", DEFAULT_ARM_CC)
    cc_path = shutil.which(cc)
    if not cc_path:
        result = {"skipped": True,
                  "reason": "%s が見つかりません" % cc,
                  "compiler": cc}
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=2))
        else:
            print("SKIP — %s が無いので ARM 計測を飛ばします "
                  "(計測器なので失敗にはしません)" % cc)
        return 0

    varmap = parse_make_vars(read(CONFIG_MK))
    files = parse_c_kernel(read(KERNEL_MK))
    if not files:
        print("Error: build/kernel.mk から C_KERNEL を読めません", file=sys.stderr)
        return 1

    flags, dropped = build_flags(varmap)
    incmap = {}
    for _prefix, var in INC_RULES:
        incmap[var] = expand_var(varmap, var)
    incmap["INC_KERNEL"] = expand_var(varmap, "INC_KERNEL")

    missing_inc = sorted({v for v in incmap if not incmap[v].strip()})
    if missing_inc:
        print("Error: build/config.mk から読めなかったインクルード変数: %s"
              % ", ".join(missing_inc), file=sys.stderr)
        return 1

    # 展開できなかった変数は空文字に落ちるので、INC は空にならず
    # `-Iarch/$(ARCH)` が `-Iarch/` のような**存在しないディレクトリ**に
    # 潰れる。それだけでは上の検査に掛からず、計測は「ヘッダが見つからない」
    # を測るだけになってしまう (順序 3 で実際に踏んだ)。実在を確かめる。
    bad_dirs = set()
    for var in sorted(incmap):
        for tok in incmap[var].split():
            if tok.startswith("-I") and not os.path.isdir(
                    os.path.join(PROJ_DIR, tok[2:])):
                bad_dirs.add("%s: %s" % (var, tok))
    if bad_dirs:
        print("Error: 実在しないインクルードディレクトリ "
              "(build/config.mk の変数が展開できていない可能性):",
              file=sys.stderr)
        for b in sorted(bad_dirs):
            print("  %s" % b, file=sys.stderr)
        return 1

    # DRIFT: glob には出るが C_KERNEL にも既知の除外にも無い *.c
    known = set(files)
    drift = [p for p in scan_glob()
             if p not in known and not is_known_non_kernel(p)]

    jobs = args.jobs if args.jobs > 0 else (os.cpu_count() or 4)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(
            lambda p: compile_one(cc_path, flags, incmap, p), files))

    results.sort(key=lambda r: r["file"])
    passed = [r for r in results if r["ok"]]
    failed = [r for r in results if not r["ok"]]

    by_cat = {}
    for c in CAT_ORDER:
        by_cat[c] = [r for r in failed if r["category"] == c]

    if args.json:
        print(json.dumps({
            "skipped": False,
            "compiler": cc_path,
            "compiler_version": subprocess.run(
                [cc_path, "-dumpversion"], stdout=subprocess.PIPE,
                universal_newlines=True).stdout.strip(),
            "flags": flags,
            "dropped_x86_flags": dropped,
            "total": len(results),
            "passed": len(passed),
            "failed": len(failed),
            "by_category": {c: len(by_cat[c]) for c in CAT_ORDER},
            "category_labels": CAT_LABEL,
            "results": results,
            "drift": drift,
        }, ensure_ascii=False, indent=2))
        return 0

    print("=" * 68)
    print("  ARM コンパイル計測 (合否ではない — 数字を見るためのもの)")
    print("=" * 68)
    print("  コンパイラ : %s" % cc_path)
    print("  フラグ     : %s" % " ".join(flags))
    print("  外した x86 : %s" % (" ".join(dropped) or "(なし)"))
    print("")
    print("  通った本数 : %d / %d" % (len(passed), len(results)))
    print("")
    print("  失敗の分類:")
    for c in CAT_ORDER:
        print("    %-34s %3d 本" % (CAT_LABEL[c], len(by_cat[c])))
    print("")

    for c in CAT_ORDER:
        if not by_cat[c]:
            continue
        print("  --- %s ---" % CAT_LABEL[c])
        for r in by_cat[c]:
            print("    %s" % r["file"])
            print("        %s" % r["error"])
        print("")

    if drift:
        print("  DRIFT — build/kernel.mk の C_KERNEL にも既知の除外にも無い *.c:")
        for p in drift:
            print("    %s" % p)
        print("    (カーネルに足したなら C_KERNEL へ、そうでないなら")
        print("     tools/check_arm_compile.py の KNOWN_NON_KERNEL へ理由つきで)")
        print("")

    print("  記録先: docs/tasks/portability/ARM_GAUGE.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
