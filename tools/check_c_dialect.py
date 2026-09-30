#!/usr/bin/env python3
"""言語モードの検査 ([C1]、票 docs/tasks/v3/TASK_C11_MIGRATION.md §6 段 4)。

check_constraints.py の ID 検査とは別に、**実際の旗とコンパイル結果**で次を見る。

  (a) 翻訳単位ごとに実際に効いている言語モード。`make -n -B all` のコンパイル行を
      読み、旗の組ごとにコンパイラへ `__STDC_VERSION__` / `__STRICT_ANSI__` を聞く
      (旗の文字列では判定しない — `-std` を 2 つ並べれば後ろが効く)。
      本体・ブート・userland・SDK の実装は gnu11、SQLite 系は gnu89。
  (b) gnu11 の翻訳単位の旗の組それぞれで、暗黙の関数宣言・暗黙 int・VLA・偽の
      STATIC_ASSERT (include/types.h の実物のマクロ) が拒否され、真の STATIC_ASSERT は
      通ること (拒否は診断の文言まで確かめる — 別の理由の失敗を「拒否」と数えない)。
  (c) 公開 SDK ヘッダ (sdk/include/os32/*.h。os32_kapi_shared.h もここ) を
      gnu89 (C90 との差を警告・エラーにする — 行コメントもここで落ちる) と gnu11 の
      両方で取り込めること、前処理後に C99/C11 の語・ヘッダが無いこと。SDK が配る library ヘッダ
      (build/sdk.mk の SDK_LIB_HEADER_DIRS・rt・lib/utf8.h) は gnu89 と gnu11 で
      取り込めること。in-tree の gnu89 の例 (sdk/example/hello) が gnu89 のまま
      in-tree の SDK ヘッダでコンパイルできること (apps/game の代わり)。
  (d) 内部実装に T0 で新規導入しないもの (_Atomic・TLS・restrict・<threads.h>・
      <stdatomic.h>) が無いこと。(a) の翻訳単位を実際の旗で前処理 (-E) し、行標識で
      ファイルと行を得る。行継続・コメント・VT/FF・#if・#include の解釈はコンパイラに
      任せ、自前は文字列・文字定数を除いて識別子を数えるだけ。vendor は除く。

  python3 tools/check_c_dialect.py [--root <木>]

終了コード: 0 = 合格、1 = 問題あり、2 = 実行できない (コンパイラ・make が無い)。
試験は tools/tests/test_c_dialect.py (記録 tools/tests/c_dialect_tdd.md)。
"""
import concurrent.futures
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
CC = os.environ.get("OS32_CC", "i386-elf-gcc")
MAKE_TARGETS = ["all"]

# ---- 期待する言語モード ----------------------------------------------------
STD_MAIN = "gnu11"
STD_SQLITE = "gnu89"
SQLITE_SRCS = (
    "lib/sqlite3/sqlite3.c",
    "lib/sqlite3/os32_sqlite_vfs.c",
    "lib/sqlite3/os32_sqlite_test.c",
)
SQLITE_DIRS = ("userland/tests/sqlite_standalone/",)
# make -n に必ず現れるはずの翻訳単位 (消えたら検査が空振りしている)
REQUIRED_SRCS = SQLITE_SRCS + (
    "userland/tests/sqlite_standalone/sqlite_standalone.c",
    "userland/tests/sqlite_standalone/sqlite_user_vfs.c",
    "kernel/kernel.c",
    "boot/boot_main.c",
)

# ---- 内部実装の走査 --------------------------------------------------------
VENDOR_DIRS = ("lib/sqlite3", "lib/zlib", "lib/microtar", "lib/fatfs", "lib/os32_lz4",
               "fs/fatfs", "userland/rust")
INTERNAL_FORBIDDEN = ("_Atomic", "_Thread_local", "__thread", "restrict",
                      "threads.h", "stdatomic.h")

# ---- 公開 SDK ヘッダ -------------------------------------------------------
SDK_HDR_DIR = "sdk/include/os32"
SDK_EXTRA_HDRS = ()  # 公開 SDK ヘッダは sdk/include/os32/ の下だけ (os32_kapi_shared.h もここ)
SDK_FORBIDDEN = ("_Bool", "_Static_assert", "_Alignas", "_Alignof", "_Atomic", "_Generic",
                 "_Noreturn", "_Thread_local", "restrict",
                 "stdbool.h", "stdatomic.h", "stdalign.h", "stdnoreturn.h", "threads.h")
SDK_BASE = ["-fsyntax-only", "-m32", "-march=i386", "-ffreestanding", "-D__OS32_USERLAND__"]
SDK_MODES = (
    ("gnu89", ["-std=gnu89", "-Wall", "-Werror", "-Wc90-c99-compat", "-Wc99-c11-compat",
               "-Wdeclaration-after-statement", "-Wlong-long"]),
    ("gnu11", ["-std=gnu11", "-Wall", "-Werror"]),
)
LIB_MODES = (
    ("gnu89", ["-std=gnu89", "-Wall", "-Werror"]),
    ("gnu11", ["-std=gnu11", "-Wall", "-Werror"]),
)
SAMPLE_DIR = "sdk/example/hello"
SAMPLE_STD = "gnu89"

# ---- 拒否の探り -------------------------------------------------------------
# (名前, 本文, 期待 "reject"/"accept", 拒否と認める診断の正規表現)
PROBES = (
    ("vla",
     "void t0_vla(int n);\nvoid t0_vla(int n) { char a[n]; a[0] = 0; (void)a; }\n",
     "reject", r"-Werror=vla"),
    ("implicit_decl",
     "int t0_f(void);\nint t0_f(void) { return t0_undeclared_fn(1); }\n",
     "reject", r"implicit-function-declaration"),
    ("implicit_int",
     "static t0_x = 1;\nint t0_g(void);\nint t0_g(void) { return t0_x; }\n",
     "reject", r"implicit-int"),
    ("static_assert_false",
     '#include "types.h"\nSTATIC_ASSERT(1 == 0, t0_false);\nint t0_dummy_f;\n',
     "reject", r"static assertion failed|size of array .* is negative"),
    ("static_assert_true",
     '#include "types.h"\nSTATIC_ASSERT(1 == 1, t0_true);\nint t0_dummy_t;\n',
     "accept", None),
)


# ==========================================================================
#  前処理後の出力を読む (字句はコンパイラに任せる)
# ==========================================================================
# 行継続 (バックスラッシュと改行の間の空白も含む)・コメント・VT/FF・#if・#include の解釈は
# 本物の前処理器 (`-E`) に任せ、ここでは前処理後の出力から文字列・文字定数を除いて
# 識別子を数えるだけにする。ファイルと行は行標識 (`# N "file" flags`) から取り、
# 取り込んだヘッダは行標識の「入った」(flag 1) と「戻った」(flag 2) から
# 「どのファイルの何行目の #include か」を得る (戻り先の行番号 - 1 が #include の行)。

LITERAL_RE = re.compile(r'(?:u8|[LuU])?"(?:\\.|[^"\\\n])*"|[LuU]?\'(?:\\.|[^\'\\\n])*\'')
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
MARKER_RE = re.compile(r'^#\s*(\d+)\s+"((?:\\.|[^"\\])*)"((?:\s+\d+)*)\s*$')


def _unescape_marker(s):
    return re.sub(r"\\(.)", r"\1", s)


def scan_preprocessed(out, words, headers):
    """`-E` の出力から [(ファイル, 行, 語またはヘッダ名)] を返す。
    words は禁止する識別子、headers は禁止するヘッダの basename。"""
    hits = []
    stack = []
    pending = []  # (取り込んだファイル, ヘッダ名, 取り込んだ側の深さ)
    cur = None
    line = 0
    for raw in out.split("\n"):
        m = MARKER_RE.match(raw)
        if m:
            n = int(m.group(1))
            f = _unescape_marker(m.group(2))
            flags = m.group(3).split()
            if "1" in flags:
                if cur is not None and os.path.basename(f) in headers:
                    pending.append((cur, os.path.basename(f), len(stack)))
                stack.append(cur)
            elif "2" in flags:
                if stack:
                    stack.pop()
                keep = []
                for inc, h, d in pending:
                    if inc == f and d == len(stack):
                        hits.append((inc, n - 1, h))
                    else:
                        keep.append((inc, h, d))
                pending = keep
            cur = f
            line = n
            continue
        if cur is not None and words and raw:
            for w in IDENT_RE.findall(LITERAL_RE.sub(" ", raw)):
                if w in words:
                    hits.append((cur, line, w))
        line += 1
    for inc, h, _ in pending:  # 戻りの標識が無いまま終わった (出力の打ち切りなど)
        hits.append((inc, 0, h))
    return hits


def preprocess(cc, args, root):
    """(rc, 出力, 標準エラー)。警告は -w で抑える (-Werror でも前処理を止めない)。"""
    r = subprocess.run([cc] + list(args) + ["-E", "-w"], cwd=str(root),
                       capture_output=True, text=True, errors="replace")
    return r.returncode, r.stdout, r.stderr


def scan_c_text(text, root, words, headers, flags=("-std=gnu11", "-ffreestanding"), cc=None):
    """試験用: text を 1 翻訳単位として前処理し、そのファイル自身の [(行, 語)]。"""
    with tempfile.TemporaryDirectory(prefix="c_dialect_scan_") as tmp:
        src = pathlib.Path(tmp) / "t0_scan.c"
        src.write_bytes(text.encode("utf-8"))
        rc, out, err = preprocess(cc or CC, list(flags) + [str(src)], root)
        if rc != 0:
            raise RuntimeError(_first_error(err))
        return sorted({(ln, w) for f, ln, w in scan_preprocessed(out, set(words), set(headers))
                       if os.path.basename(f) == "t0_scan.c"})


# ==========================================================================
#  コンパイル行
# ==========================================================================

ARG_DROP = {"-o", "-MF", "-MT", "-MQ", "-I", "-isystem", "-iquote", "-idirafter"}
FLAG_DROP = {"-c", "-MMD", "-MD", "-MP", "-M", "-MM"}
ARG_KEEP = {"-include", "-imacros", "-x"}
SEPARATORS = {"&&", "||", ";", "|", "then", "do", "else"}


def _segments(line):
    try:
        toks = shlex.split(line, comments=False)
    except ValueError:
        toks = line.split()
    seg = []
    for t in toks:
        if t in SEPARATORS:
            if seg:
                yield seg
            seg = []
            continue
        if t.endswith(";") and len(t) > 1:
            seg.append(t[:-1])
            yield seg
            seg = []
            continue
        seg.append(t)
    if seg:
        yield seg


def _is_cross_cc(tok):
    b = os.path.basename(tok)
    return b.startswith("i386-elf-") and b.endswith("gcc")


def parse_compile_lines(text):
    """`make -n` の出力から i386-elf-gcc の -c の行を拾い、
    [{"src": 元の .c, "sig": 旗の組 (tuple), "cc": コンパイラ, "argv": 引数}] で返す。
    旗の組からは -I・依存生成・-c・-o と元のソースを外す (言語に効かないもの)。
    argv は依存生成・-c・-o だけを外した実際の引数 (前処理に使う)。"""
    units = []
    for line in text.splitlines():
        if "gcc" not in line:
            continue
        for seg in _segments(line):
            if not seg or not _is_cross_cc(seg[0]) or "-c" not in seg[1:]:
                continue
            sig = []
            srcs = []
            argv = []
            it = iter(seg[1:])
            for a in it:
                if a in ("-o", "-MF", "-MT", "-MQ"):
                    next(it, None)
                    continue
                if a not in FLAG_DROP:
                    argv.append(a)
                if a in ARG_DROP:
                    argv.append(next(it, ""))
                    continue
                if a in ARG_KEEP:
                    nx = next(it, "")
                    argv.append(nx)
                    sig.append(a)
                    sig.append(nx)
                    continue
                if a in FLAG_DROP or (a.startswith("-I") and len(a) > 2):
                    continue
                if not a.startswith("-") and a.endswith(".c"):
                    srcs.append(os.path.normpath(a))
                    continue
                sig.append(a)
            if len(srcs) != 1:
                continue
            units.append({"src": srcs[0], "sig": tuple(sig), "cc": seg[0], "argv": argv})
    return units


def expected_std(src):
    s = os.path.normpath(src).replace(os.sep, "/")
    if s in SQLITE_SRCS or any(s.startswith(d) for d in SQLITE_DIRS):
        return STD_SQLITE
    return STD_MAIN


# ==========================================================================
#  コンパイラに聞く
# ==========================================================================

_STD_BY_VERSION = {None: "89", "199409L": "94", "199901L": "99", "201112L": "11",
                   "201710L": "17"}


def effective_std(flags, root, cc=None):
    """flags で実際に効く言語モード ("gnu11" / "gnu89" / "c11" / "gnu17" …)。
    コンパイラが通らなければ "error: …"。"""
    r = subprocess.run([cc or CC] + list(flags) + ["-E", "-dM", "-x", "c", os.devnull],
                       cwd=str(root), capture_output=True, text=True)
    if r.returncode != 0:
        return "error: " + (r.stderr.strip().splitlines() or ["?"])[0]
    ver = None
    strict = False
    for ln in r.stdout.splitlines():
        p = ln.split()
        if len(p) >= 3 and p[1] == "__STDC_VERSION__":
            ver = p[2]
        if len(p) >= 2 and p[1] == "__STRICT_ANSI__":
            strict = True
    v = _STD_BY_VERSION.get(ver, "2x" if ver else "89")
    return ("c" if strict else "gnu") + v


def _compile(cc, flags, body, root, tmp, name, incs=()):
    src = pathlib.Path(tmp) / (name + ".c")
    src.write_text(body, encoding="utf-8")
    args = [cc] + list(flags) + ["-fsyntax-only"]
    for d in incs:
        args += ["-I", str(d)]
    args.append(str(src))
    r = subprocess.run(args, cwd=str(root), capture_output=True, text=True)
    return r.returncode, r.stderr


def probe_rejects(flags, root, cc=None):
    """flags で拒否の探りを回し、期待どおりにならなかった探りの名前の並び。
    STATIC_ASSERT は root/include/types.h の実物のマクロを使う。"""
    root = pathlib.Path(root)
    miss = []
    incs = (root / "include", root / SDK_HDR_DIR)
    with tempfile.TemporaryDirectory(prefix="c_dialect_probe_") as tmp:
        for name, body, want, pat in PROBES:
            rc, err = _compile(cc or CC, flags, body, root, tmp, name, incs)
            if want == "accept":
                if rc != 0:
                    miss.append(name)
            elif rc == 0 or not re.search(pat, err):
                miss.append(name)
    return miss


def _first_error(err):
    for ln in err.splitlines():
        if "error" in ln:
            return ln.strip()
    lines = err.strip().splitlines()
    return lines[0].strip() if lines else "(診断なし)"


def _cross_newlib_include(cc):
    p = shutil.which(cc)
    if not p:
        return None
    d = pathlib.Path(p).resolve().parents[1] / "i386-elf" / "include"
    return d if d.is_dir() else None


def _rel(root, p):
    try:
        return str(pathlib.Path(p).relative_to(root))
    except ValueError:
        return str(p)


# ==========================================================================
#  (c) 公開 SDK ヘッダ・配布ライブラリヘッダ・gnu89 の例
# ==========================================================================

def sdk_public_headers(root):
    root = pathlib.Path(root)
    hs = sorted((root / SDK_HDR_DIR).glob("*.h"))
    hs += [root / x for x in SDK_EXTRA_HDRS if (root / x).is_file()]
    return hs


def check_sdk_headers(root, cc=None):
    """公開 SDK ヘッダの問題の並び (空なら合格)。"""
    root = pathlib.Path(root)
    cc = cc or CC
    probs = []
    hs = sdk_public_headers(root)
    if not hs:
        return ["%s: 公開 SDK ヘッダが 1 本も無い (空の検査で通さない)" % SDK_HDR_DIR]
    shared = root / SDK_HDR_DIR / "os32_kapi_shared.h"
    incs = [root / SDK_HDR_DIR, root / "sdk/include"]
    with tempfile.TemporaryDirectory(prefix="c_dialect_sdk_") as tmp:
        for h in hs:
            rel = _rel(root, h)
            pre = ""
            if shared.is_file() and h.resolve() != shared.resolve():
                pre = '#include "os32_kapi_shared.h"\n'
            body = pre + '#include "%s"\nint os32_sdk_probe_tu;\n' % h.resolve()
            # 語とヘッダは前処理後の出力で (行コメント・宣言位置・_Bool 等は下の gnu89 の
            # -Wc90-c99-compat -Werror がコンパイラの判定で拒否する)
            src = pathlib.Path(tmp) / "sdk_scan.c"
            src.write_text(body, encoding="utf-8")
            args = SDK_BASE[1:] + SDK_MODES[0][1][:1]
            for d in incs:
                args += ["-I", str(d)]
            rc, out, err = preprocess(cc, args + [str(src)], root)
            if rc != 0:
                probs.append("%s: 前処理できない — %s" % (rel, _first_error(err)))
            words = {w for w in SDK_FORBIDDEN if not w.endswith(".h")}
            heads = {w for w in SDK_FORBIDDEN if w.endswith(".h")}
            for f, ln, w in sorted(set(scan_preprocessed(out, words, heads))):
                if pathlib.Path(root, f).resolve().parent == (root / SDK_HDR_DIR).resolve():
                    probs.append("%s:%d: 公開 SDK ヘッダに C99/C11 の %s"
                                 % (_rel(root, pathlib.Path(root, f).resolve()), ln, w))
            for mode, fl in SDK_MODES:
                rc, err = _compile(cc, SDK_BASE + fl, body, root, tmp, "sdk", incs)
                if rc != 0:
                    probs.append("%s: %s で取り込めない — %s" % (rel, mode, _first_error(err)))
    return probs


def sdk_lib_headers(root):
    """SDK が配るライブラリヘッダ (build/sdk.mk の sdk: と同じ選び方)。"""
    root = pathlib.Path(root)
    mk = (root / "build/sdk.mk").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^SDK_LIB_HEADER_DIRS\s*[:?]?=\s*(.*)$", mk, re.M)
    dirs = m.group(1).split() if m else []
    hs = []
    for d in dirs:
        hs += [h for h in sorted((root / "userland/lib" / d).glob("*.h"))
               if not h.name.endswith("_internal.h")]
    hs += sorted((root / "userland/lib/rt").glob("*.h"))
    if (root / "lib/utf8.h").is_file():
        hs.append(root / "lib/utf8.h")
    return dirs, hs


def _lib_incs(root, cc, dirs):
    incs = [root / SDK_HDR_DIR, root / "sdk/include"]
    incs += [root / "userland/lib" / d for d in dirs]
    incs += [root / "userland/lib", root / "lib"]
    nl = _cross_newlib_include(cc)
    if nl:
        incs.append(nl)
    return incs


def check_sdk_lib_headers(root, cc=None):
    root = pathlib.Path(root)
    cc = cc or CC
    dirs, hs = sdk_lib_headers(root)
    if not hs:
        return ["build/sdk.mk: SDK が配るライブラリヘッダが 1 本も見つからない"], 0
    probs = []
    incs = _lib_incs(root, cc, dirs)
    with tempfile.TemporaryDirectory(prefix="c_dialect_lib_") as tmp:
        for h in hs:
            body = '#include "os32api.h"\n#include "%s"\nint os32_lib_probe_tu;\n' % h.resolve()
            for mode, fl in LIB_MODES:
                rc, err = _compile(cc, SDK_BASE + fl, body, root, tmp, "lib", incs)
                if rc != 0:
                    probs.append("%s: %s で取り込めない — %s"
                                 % (_rel(root, h), mode, _first_error(err)))
    return probs, len(hs)


def check_sdk_sample(root, cc=None):
    """sdk/example/hello が gnu89 のまま、in-tree の SDK ヘッダでコンパイルできるか。"""
    root = pathlib.Path(root)
    cc = cc or CC
    mk = root / SAMPLE_DIR / "Makefile"
    if not mk.is_file():
        return ["%s: 無い (gnu89 の例が消えた)" % _rel(root, mk)]
    stds = re.findall(r"-std=(\S+)", mk.read_text(encoding="utf-8", errors="replace"))
    if stds != [SAMPLE_STD]:
        return ["%s: -std が %r (gnu89 の例として残す、票 §1-2)" % (_rel(root, mk), stds)]
    dirs, _ = sdk_lib_headers(root)
    probs = []
    for src in sorted((root / SAMPLE_DIR).glob("*.c")):
        args = [cc, "-std=" + SAMPLE_STD] + SDK_BASE + ["-Wall"]
        for d in _lib_incs(root, cc, dirs):
            args += ["-I", str(d)]
        r = subprocess.run(args + [str(src)], cwd=str(root), capture_output=True, text=True)
        if r.returncode != 0:
            probs.append("%s: gnu89 でコンパイルできない — %s"
                         % (_rel(root, src), _first_error(r.stderr)))
    return probs


# ==========================================================================
#  (d) 内部実装の禁止トークン
# ==========================================================================

def _is_vendor(rel):
    return any(rel == v or rel.startswith(v + "/") for v in VENDOR_DIRS)


def _tree_rel(root, f):
    """前処理の行標識のファイル名を木からの相対パスに。木の外なら None。"""
    try:
        return os.path.relpath(str(pathlib.Path(root, f).resolve()),
                               str(pathlib.Path(root).resolve())).replace(os.sep, "/")
    except ValueError:
        return None


def check_internal_units(units, root, jobs=None):
    """内部実装の翻訳単位 (vendor の元ソースを除く) を実際の旗で前処理し、
    木の中の vendor 以外のファイルに現れる禁止語・禁止ヘッダの #include を返す。
    (問題の並び, 前処理した翻訳単位の数)。"""
    root = pathlib.Path(root)
    words = {w for w in INTERNAL_FORBIDDEN if not w.endswith(".h")}
    heads = {w for w in INTERNAL_FORBIDDEN if w.endswith(".h")}
    todo = {}
    for u in units:
        if _is_vendor(u["src"].replace(os.sep, "/")):
            continue
        if not pathlib.Path(root, u["src"]).is_file():
            continue  # 生成物 (BUILD_OUT の build_id.c など)。手書きの内部実装ではない
        todo.setdefault((u["cc"], tuple(u["argv"])), u["src"])
    probs = set()

    def one(key):
        cc, argv = key
        return key, preprocess(cc, argv, root)

    n = jobs or max(1, os.cpu_count() or 1)
    with concurrent.futures.ThreadPoolExecutor(max_workers=n) as ex:
        for (cc, argv), (rc, out, err) in ex.map(one, sorted(todo)):
            if rc != 0:
                probs.add("%s: 前処理できない — %s" % (todo[(cc, argv)], _first_error(err)))
                continue
            for f, ln, w in scan_preprocessed(out, words, heads):
                rel = _tree_rel(root, f)
                if rel is None or rel.startswith("../") or _is_vendor(rel):
                    continue
                probs.add("%s:%d: 内部実装に T0 で新規導入しない %s ([C1])" % (rel, ln, w))
    return sorted(probs), len(todo)


# ==========================================================================
#  (a)(b) 実際の旗
# ==========================================================================

JOBSERVER_RE = re.compile(r"^(-j\d*|--jobs(=\d+)?|--jobserver-(auth|fds)=.*)$")


def strip_jobserver(makeflags):
    """親の MAKEFLAGS から並列 make の引き継ぎ (-j…、--jobserver-auth=…、--jobserver-fds=…)
    だけを取り除き、残り (単文字旗の束の e など、` -- ` 以降の変数指定) はバイト列のまま返す。
    変数指定のエスケープの解釈は子の make 自身に任せる。"""
    mf = makeflags or ""
    if mf.startswith("-- "):
        return mf
    head, sep, tail = mf.partition(" -- ")
    kept = [w for w in head.split(" ") if w and not JOBSERVER_RE.match(w)]
    out = " ".join(kept)
    if sep:
        out = (out + " -- " if out else "-- ") + tail
    return out


def dry_run(root):
    """`make -n -B all`。親の make の旗と変数指定 (C_STD=…、-e など) は MAKEFLAGS のまま
    子に渡し、ジョブサーバの引き継ぎだけを切る (strip_jobserver)。BUILD_OUT は一時
    ディレクトリに向ける — config.mk の `$(shell mkdir -p $(BUILD_OUT) …)` は -n でも
    走るので、そのままだと実物の木 (写しの木なら symlink の先) に build/out を作り得る
    (コマンドラインの BUILD_OUT が MAKEFLAGS から来た指定より勝つ)。"""
    env = dict(os.environ)
    mf = strip_jobserver(env.get("MAKEFLAGS", ""))
    for k in ("MAKEFLAGS", "MFLAGS", "MAKE_TERMOUT", "MAKE_TERMERR"):
        env.pop(k, None)
    if mf:
        env["MAKEFLAGS"] = mf
    with tempfile.TemporaryDirectory(prefix="c_dialect_out_") as tmp:
        cmd = (["make", "--no-print-directory", "-n", "-B"] + MAKE_TARGETS
               + ["BUILD_OUT=" + os.path.join(tmp, "out")])
        r = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True, env=env)
    return r.returncode, r.stdout, r.stderr


def check_build_flags(root):
    """(問題の並び, 要約の辞書)。"""
    root = pathlib.Path(root)
    rc, out, err = dry_run(root)
    if rc != 0:
        return (["make -n -B %s が rc=%d — %s" % (" ".join(MAKE_TARGETS), rc,
                                                   _first_error(err))], {})
    units = parse_compile_lines(out)
    probs = []
    if not units:
        return (["make -n -B の出力に i386-elf-gcc のコンパイル行が無い"], {})
    seen = {u["src"] for u in units}
    for s in REQUIRED_SRCS:
        if s not in seen:
            probs.append("%s: コンパイル行に無い (検査が空振りしている)" % s)
    eff_cache = {}
    counts = {}
    bad = {}
    for u in units:
        key = (u["cc"], u["sig"])
        if key not in eff_cache:
            eff_cache[key] = effective_std(u["sig"], root, u["cc"])
        eff = eff_cache[key]
        exp = expected_std(u["src"])
        counts[eff] = counts.get(eff, 0) + 1
        if eff != exp:
            bad.setdefault((exp, eff), []).append(u["src"])
    for (exp, eff), srcs in sorted(bad.items()):
        probs.append("%d 単位が %s でなく %s: %s%s" % (
            len(srcs), exp, eff, " ".join(srcs[:6]), " …" if len(srcs) > 6 else ""))
    probed = 0
    for (cc, sig) in sorted({(u["cc"], u["sig"]) for u in units
                             if expected_std(u["src"]) == STD_MAIN}):
        probed += 1
        miss = probe_rejects(sig, root, cc)
        if miss:
            srcs = [u["src"] for u in units if u["sig"] == sig and u["cc"] == cc]
            probs.append("旗の組 %d 本目 (%s ほか %d 単位) で探りが期待どおりにならない: %s"
                         % (probed, srcs[0], len(srcs) - 1, ", ".join(miss)))
    return probs, {"units": len(units), "counts": counts, "sigs": len(eff_cache),
                   "probed": probed, "unit_list": units}


def main(argv):
    root = ROOT
    if "--root" in argv:
        root = pathlib.Path(argv[argv.index("--root") + 1]).resolve()
    if shutil.which(CC) is None:
        print("check_c_dialect: コンパイラ %s が無い" % CC, file=sys.stderr)
        return 2
    if shutil.which("make") is None:
        print("check_c_dialect: make が無い", file=sys.stderr)
        return 2
    probs, summ = check_build_flags(root)
    sdk_p = check_sdk_headers(root)
    lib_p, nlib = check_sdk_lib_headers(root)
    sample_p = check_sdk_sample(root)
    int_p, nint = check_internal_units(summ.get("unit_list", []), root)
    probs += sdk_p + lib_p + sample_p + int_p
    c = summ.get("counts", {})
    print("check_c_dialect: 翻訳単位 %d (gnu11 %d、gnu89 %d、他 %d)、旗の組 %d (探り %d)、"
          "公開 SDK ヘッダ %d 本、配布ライブラリヘッダ %d 本、内部実装 %d 翻訳単位 (前処理)"
          % (summ.get("units", 0), c.get(STD_MAIN, 0), c.get(STD_SQLITE, 0),
             sum(v for k, v in c.items() if k not in (STD_MAIN, STD_SQLITE)),
             summ.get("sigs", 0), summ.get("probed", 0), len(sdk_public_headers(root)),
             nlib, nint))
    if probs:
        for p in probs:
            print("  NG " + p)
        print("check_c_dialect: FAIL (%d 件)" % len(probs))
        return 1
    print("check_c_dialect: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
