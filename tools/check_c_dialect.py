#!/usr/bin/env python3
"""[C1] Actual GCC language modes/probes plus clang AST and SDK diagnostics.

make -n command extraction is shared in clang_ast.build. Internal declarations
use clang canonical types and physical preprocessing records, including macros,
TLS, anonymous records and old-style definitions. Public SDK headers also use
clang gnu89/C99/C11 diagnostics and compiler tokens for unexpanded macros and
C99 comments. Parse failures are errors. --root selects an isolated test tree.
"""
import os
import pathlib
import re
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


# Build command extraction is shared by every AST checker.
from clang_ast.build import parse_compile_lines, strip_jobserver, dry_run
import clang_ast as ast
from clang_ast.dialect import findings as ast_findings


def scan_c_text(text, root, words, headers, flags=("-std=gnu11", "-ffreestanding"), cc=None):
    tu = ast.parse('t0_scan.c', flags, root, text=text)
    return sorted({(ln,w) for rel,ln,w in ast_findings(tu,root,set(words),set(headers))
                   if rel == 't0_scan.c'})


def _clang_compile(cc, flags, body, root, tmp, name, incs=()):
    src = pathlib.Path(tmp) / (name + '.c')
    src.write_text(body, encoding='utf-8')
    args = ast.flags(flags,root,cc)
    for d in incs:
        args += ['-I',str(d)]
    args += ['-Werror=c99-extensions','-Werror=c11-extensions']
    r = subprocess.run(['clang'] + args + ['-fsyntax-only',str(src)],
                       cwd=root,capture_output=True,text=True)
    return r.returncode,r.stderr


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
            words = {w for w in SDK_FORBIDDEN if not w.endswith('.h')}
            heads = {w for w in SDK_FORBIDDEN if w.endswith('.h')}
            argv = SDK_BASE[1:] + ['-std=gnu11']
            for d in incs:
                argv += ['-I',str(d)]
            try:
                tu = ast.parse('sdk_scan.c',argv,root,text=body)
                for f,ln,w in ast_findings(tu,root,words,heads,public=True):
                    if f.startswith(SDK_HDR_DIR + '/'):
                        probs.append('%s:%d: 公開 SDK ヘッダに C99/C11 の %s' % (f,ln,w))
            except ast.ParseError as e:
                probs.append('%s: clang parse failure — %s' % (rel,e))
            # Clang accepts GNU // even in gnu89 without an extension diagnostic;
            # use its lexer (including escaped-newline handling) for this C90 rule.
            try:
                lexical = ast.parse('sdk_comments.c',argv,root,text=body)
            except ast.ParseError:
                continue
            for header in hs:
                file = lexical.get_file(str(header.absolute()))
                if not file:
                    continue
                start = ast.cx.SourceLocation.from_offset(lexical,file,0)
                end = ast.cx.SourceLocation.from_offset(lexical,file,header.stat().st_size)
                extent = ast.cx.SourceRange.from_locations(start,end)
                for t in lexical.get_tokens(extent=extent):
                    if t.kind == ast.cx.TokenKind.COMMENT and re.sub(r'\\[ \t]*\n', '', t.spelling).startswith('//'):
                        probs.append('%s:%d: C99 line comment' % (_rel(root,header),t.location.line))
            for mode, fl in SDK_MODES:
                rc, err = _clang_compile(cc, SDK_BASE + fl, body, root, tmp, "sdk", incs)
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
                rc, err = _clang_compile(cc, SDK_BASE + fl, body, root, tmp, "lib", incs)
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


def check_internal_units(units, root, jobs=None):
    root = pathlib.Path(root).absolute()
    probs = set()
    count = 0
    seen = set()
    for u in units:
        vendor = _is_vendor(u['src'])
        src = u['src']
        if not pathlib.Path(root,src).is_file() and src.endswith('/build_id.c'):
            src = 'build/out/build_id.c'
        argv = [a for a in u['argv'] if a != u['src']]
        key = (src,tuple(argv))
        if key in seen:
            continue
        seen.add(key)
        count += not vendor
        try:
            tu = ast.parse(src,argv,root)
            if vendor:
                continue  # Vendor policy is exempt; syntax errors still fail closed.
            words = set(INTERNAL_FORBIDDEN) | {'VLA','anonymous record','old-style definition'}
            for f,ln,w in ast_findings(tu,root,words,{'threads.h','stdatomic.h'}):
                if not _is_vendor(f):
                    probs.add('%s:%d: 内部実装に T0 で新規導入しない %s ([C1])' % (f,ln,w))
        except ast.ParseError as e:
            probs.add('%s: clang parse failure — %s' % (src,e))
    return sorted(probs),count


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
    int_p, nint = ([], 0) if probs or sdk_p or lib_p or sample_p else check_internal_units(summ.get("unit_list", []), root)
    probs += sdk_p + lib_p + sample_p + int_p
    c = summ.get("counts", {})
    print("check_c_dialect: 翻訳単位 %d (gnu11 %d、gnu89 %d、他 %d)、旗の組 %d (探り %d)、"
          "公開 SDK ヘッダ %d 本、配布ライブラリヘッダ %d 本、内部実装 %d 翻訳単位 (clang AST)"
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
