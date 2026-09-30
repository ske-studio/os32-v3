"""tools/check_c_dialect.py (言語モードの検査、[C1]) のホスト試験。

記録: tools/tests/c_dialect_tdd.md
票:   docs/tasks/v3/TASK_C11_MIGRATION.md §6 段 4・段 5

検査器の部品 (字句の読み分け・コンパイル行の読み取り・実際に効いている言語モード・
拒否の探り・公開 SDK ヘッダの検査) を小さな入力で固定し、実物の木が通ることも見る。

--mutate は否定側: 実物の木の写し (tools/tests/mutpar.py の写しの木) に変異を当て、
写しに対して検査器を回して **落ちる** (RED) ことを見る。C11 で許す書き方 (`//`、
ブロック途中の宣言) を内部実装に足す変異は **対照** で、通る (GREEN) のが期待。

  python3 -B tools/tests/test_c_dialect.py            # 全ケース
  python3 -B tools/tests/test_c_dialect.py --mutate   # 否定側も
"""
import importlib.util
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "tools/check_c_dialect.py"
sys.path.insert(0, str(ROOT / "tools/tests"))
import mutpar  # noqa: E402

FAILED = []
N = [0]


def check(cond, what):
    N[0] += 1
    print("  %s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILED.append(what)


def load():
    spec = importlib.util.spec_from_file_location("check_c_dialect_under_test", str(SRC))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# --------------------------------------------------------------------------
#  1. 字句: コメント・文字列を区別する
# --------------------------------------------------------------------------

def case_lex(cd):
    print("== 1: 字句 (コメント・文字列・文字定数の読み分け) ==", flush=True)
    code, lc = cd.strip_c('int a; // real\n')
    check(lc == [1], "行コメント // を 1 行目に見つける")
    check("real" not in code, "行コメントの中身はコードから消える")
    code, lc = cd.strip_c('const char *s = "a // b";\n')
    check(lc == [], "文字列の中の // は行コメントでない")
    code, lc = cd.strip_c('/* x // y */ int b;\n')
    check(lc == [], "ブロックコメントの中の // は行コメントでない")
    code, lc = cd.strip_c("char c = '\"'; // after\n")
    check(lc == [1], "文字定数の '\"' で文字列に入ったことにしない")
    code, lc = cd.strip_c('const char *s = "\\" // still string";\n')
    check(lc == [], "文字列の \\\" で文字列を抜けたことにしない")
    code, lc = cd.strip_c('#define X a \\\n  // c\n')
    check(lc == [2], "行継続の先の // も行番号どおりに見つける")
    toks = cd.find_tokens('/* restrict */ int f(int *restrict p);\n', {"restrict"})
    check(toks == [(1, "restrict")], "コメントの中の restrict は数えず、コードのものだけ数える")
    toks = cd.find_tokens('const char *s = "_Atomic";\nint __restrict q;\n',
                          {"_Atomic", "restrict"})
    check(toks == [], "文字列の中の語と、語の一部 (__restrict) は数えない")
    toks = cd.find_tokens('#include <stdbool.h>\n#include "threads.h"\n',
                          {"<stdbool.h>", "threads.h"})
    check(toks == [(1, "<stdbool.h>"), (2, "threads.h")],
          "#include のヘッダ名 (<...> と \"...\") を見つける")
    # 翻訳段階の順: 行継続を先に除き、コメントを空白にしてから判定する (Codex P2-2)
    toks = cd.find_tokens('int a;\n#include /* C11 */ <stdatomic.h>\natomic_int x;\n',
                          {"stdatomic.h"})
    check(toks == [(2, "stdatomic.h")], "#include とヘッダ名の間のコメントを空白として読む")
    toks = cd.find_tokens('#include /* a\n b */ <threads.h>\n', {"threads.h"})
    check(toks == [(1, "threads.h")], "複数行のコメントを挟んだ #include も 1 行として読む")
    toks = cd.find_tokens('int a;\nvoid t0_r(char *re\\\nstrict p);\n', {"restrict"})
    check(toks == [(2, "restrict")], "行継続で割った restrict を見つけ、元の行番号で返す")
    toks = cd.find_tokens('#inc\\\nlude <stdatomic.h>\n', {"stdatomic.h"})
    check(toks == [(1, "stdatomic.h")], "行継続で割った #include も読む")
    code, lc = cd.strip_c('int a; /\\\n/ c\nint b;\n')
    check(lc == [1], "行継続で割った // も行コメント")


# --------------------------------------------------------------------------
#  2. コンパイル行の読み取りと期待する言語モード
# --------------------------------------------------------------------------

DRY = """\
nasm -f elf32 -o kernel/entry.o kernel/entry.asm
i386-elf-gcc -std=gnu11 -Werror=vla -m32 -MMD -MP -O2 -I. -Iinclude -c kernel/kernel.c -o kernel/kernel.o
i386-elf-gcc -std=gnu89 -m32 -Os -include lib/sqlite3/os32_sqlite_config.h -Ilib/sqlite3 -c lib/sqlite3/sqlite3.c -o lib/sqlite3/sqlite3.o
i386-elf-gcc -std=gnu11 -m32 -Iboot -c -o boot/boot_main.o boot/boot_main.c
i386-elf-ld -m elf_i386 -o build/kernel.elf kernel/kernel.o
"""


def case_parse(cd):
    print("== 2: コンパイル行の読み取り・期待する言語モード ==", flush=True)
    units = cd.parse_compile_lines(DRY)
    srcs = [u["src"] for u in units]
    check(srcs == ["kernel/kernel.c", "lib/sqlite3/sqlite3.c", "boot/boot_main.c"],
          "i386-elf-gcc の -c の行だけを拾い、元のソースを読む (-o が先でも)")
    k = units[0]["sig"]
    check("-Iinclude" not in k and "-MMD" not in k and "-c" not in k and "-o" not in k,
          "旗の組から -I・依存生成・-c・-o を外す")
    check("-std=gnu11" in k and "-Werror=vla" in k and "-O2" in k, "言語・警告・最適化の旗は残す")
    s = units[1]["sig"]
    check("-include" in s and "lib/sqlite3/os32_sqlite_config.h" in s, "-include とその引数は残す")
    check(cd.expected_std("lib/sqlite3/sqlite3.c") == "gnu89", "SQLite 本体は gnu89")
    check(cd.expected_std("lib/sqlite3/os32_sqlite_vfs.c") == "gnu89", "SQLite の VFS は gnu89")
    check(cd.expected_std("lib/sqlite3/os32_sqlite_test.c") == "gnu89", "SQLite の試験は gnu89")
    check(cd.expected_std("userland/tests/sqlite_standalone/sqlite_user_vfs.c") == "gnu89",
          "userland の SQLite 単体は gnu89")
    check(cd.expected_std("kernel/kernel.c") == "gnu11", "本体は gnu11")
    check(cd.expected_std("boot/boot_main.c") == "gnu11", "ブートは gnu11")
    check(cd.expected_std("userland/lib/gfx/gfx.c") == "gnu11", "userland は gnu11")


# --------------------------------------------------------------------------
#  3. 実際に効いている言語モード (コンパイラに聞く)
# --------------------------------------------------------------------------

def case_effective(cd):
    print("== 3: 実際に効いている言語モード (__STDC_VERSION__ / __STRICT_ANSI__) ==", flush=True)
    check(cd.effective_std(["-std=gnu11"], ROOT) == "gnu11", "-std=gnu11 → gnu11")
    check(cd.effective_std(["-std=gnu89"], ROOT) == "gnu89", "-std=gnu89 → gnu89")
    check(cd.effective_std(["-std=gnu89", "-std=gnu11"], ROOT) == "gnu11",
          "-std を 2 つ並べると後ろが効く (文字列でなくコンパイラで判定する)")
    check(cd.effective_std([], ROOT) not in ("gnu11", "gnu89"),
          "-std 無しはコンパイラの既定 (gnu11 でも gnu89 でもない)")
    check(cd.effective_std(["-std=c11"], ROOT) == "c11", "-std=c11 は GNU 拡張なし (c11) と読む")
    ov = cd.make_overrides(" -j4 --jobserver-auth=fifo:/tmp/GMfifo1 -- B:=1 A=b\\ c C_STD=-std=gnu89")
    check(ov == ["C_STD=-std=gnu89", "A=b c", "B:=1"],
          "親の MAKEFLAGS から変数指定だけを元の順で取り出し、-j / jobserver は捨てる (%r)" % (ov,))
    check(cd.make_overrides("-- X=1") == ["X=1"], "旗なしの MAKEFLAGS (-- で始まる) も読む")
    check(cd.make_overrides(" -j4 --jobserver-auth=fifo:/tmp/x") == [], "変数指定が無ければ空")


# --------------------------------------------------------------------------
#  4. 拒否の探り (暗黙宣言・暗黙 int・VLA・偽の STATIC_ASSERT・非定数式)
# --------------------------------------------------------------------------

GOOD = ["-std=gnu11", "-Werror=implicit-function-declaration", "-Werror=implicit-int",
        "-Werror=vla", "-m32", "-ffreestanding"]


def case_probe(cd):
    print("== 4: 拒否の探り ==", flush=True)
    miss = cd.probe_rejects(GOOD, ROOT)
    check(miss == [], "全部の旗があれば探りは全部拒否される (miss=%r)" % (miss,))
    for flag, name in (("-Werror=vla", "vla"),
                       ("-Werror=implicit-function-declaration", "implicit_decl"),
                       ("-Werror=implicit-int", "implicit_int")):
        miss = cd.probe_rejects([f for f in GOOD if f != flag], ROOT)
        check(name in miss, "%s が無ければ %s の探りが通ってしまうと報告する" % (flag, name))
    # 別の理由 (壊れた -include) で落ちた探りを「拒否された」と数えない
    with tempfile.TemporaryDirectory(prefix="c_dialect_brk_") as td:
        brk = pathlib.Path(td) / "broken.h"
        brk.write_text("this is not C;\n", encoding="utf-8")
        miss = cd.probe_rejects([f for f in GOOD if f != "-Werror=vla"] + ["-include", str(brk)],
                                ROOT)
        check("vla" in miss, "VLA の探りが別の理由で落ちても拒否と数えない (診断の文言まで見る)")
    # 偽の STATIC_ASSERT (条件を捨てるマクロ) を持つ写しの include/types.h
    with tempfile.TemporaryDirectory(prefix="c_dialect_sa_") as td:
        t = pathlib.Path(td)
        (t / "include").mkdir()
        (t / "include/types.h").write_text(
            "#define STATIC_ASSERT(cond, name) _Static_assert(1, #name)\n", encoding="utf-8")
        miss = cd.probe_rejects(GOOD, t)
        check("static_assert_false" in miss, "条件を捨てる STATIC_ASSERT を見逃さない")
        (t / "include/types.h").write_text(
            "#define STATIC_ASSERT(cond, name) typedef char sa_##name[(cond) ? 1 : -1]\n",
            encoding="utf-8")
        miss = cd.probe_rejects(GOOD, t)
        check("static_assert_false" not in miss, "負サイズ配列の STATIC_ASSERT でも偽は拒否される")
        (t / "include/types.h").write_text("/* STATIC_ASSERT が無い */\n", encoding="utf-8")
        miss = cd.probe_rejects(GOOD, t)
        check("static_assert_true" in miss, "STATIC_ASSERT(1) が通らない (マクロが無い) ことも報告する")


# --------------------------------------------------------------------------
#  5. 公開 SDK ヘッダ (gnu89 と gnu11 の両方、C99 以降の構文の混入)
# --------------------------------------------------------------------------

BASE_H = "#ifndef OS32_KAPI_SHARED_H\n#define OS32_KAPI_SHARED_H\ntypedef unsigned long u32;\n#endif\n"


def sdk_tree(td, extra):
    t = pathlib.Path(td)
    d = t / "sdk/include/os32"
    d.mkdir(parents=True)
    (d / "os32_kapi_shared.h").write_text(BASE_H, encoding="utf-8")
    for name, body in extra.items():
        (d / name).write_text(body, encoding="utf-8")
    return t


def case_sdk(cd):
    print("== 5: 公開 SDK ヘッダ ==", flush=True)
    good = "/* ok */\n#define OS32_X \"a//b\"\nstatic __inline__ int os32_f(void) { int a = 1; return a; }\n"
    with tempfile.TemporaryDirectory(prefix="c_dialect_sdk_") as td:
        t = sdk_tree(td, {"x.h": good})
        probs = cd.check_sdk_headers(t)
        check(probs == [], "C89 の書き方だけのヘッダは通る (problems=%r)" % (probs,))
    bad_cases = [
        ("行コメント", "int os32_a; // x\n"),
        ("ブロック途中の宣言", "static __inline__ int os32_f(void) { int a = 1; a++; int b = a; return b; }\n"),
        ("for の中の宣言", "static __inline__ void os32_f(void) { for (int i = 0; i < 1; i++) { } }\n"),
        ("_Bool", "extern _Bool os32_b;\n"),
        ("stdbool", "#include <stdbool.h>\nextern bool os32_b;\n"),
        ("指示付き初期化子", "struct os32_s { int a; };\nstatic const struct os32_s os32_v = { .a = 1 };\n"),
        ("_Static_assert", "_Static_assert(1, \"x\");\n"),
        ("restrict", "void os32_f(char *restrict p);\n"),
        ("long long", "extern long long os32_ll;\n"),
    ]
    for what, body in bad_cases:
        with tempfile.TemporaryDirectory(prefix="c_dialect_sdk_") as td:
            t = sdk_tree(td, {"x.h": body})
            probs = cd.check_sdk_headers(t)
            check(any("x.h" in p for p in probs), "%s を混ぜたヘッダを拒否する" % what)
    with tempfile.TemporaryDirectory(prefix="c_dialect_sdk_") as td:
        t = pathlib.Path(td)
        (t / "sdk/include/os32").mkdir(parents=True)
        probs = cd.check_sdk_headers(t)
        check(probs != [], "公開ヘッダが 1 本も無ければ落ちる (空の検査で通さない)")


# --------------------------------------------------------------------------
#  6. 内部実装の禁止トークン (T0 で新規導入しないもの) と vendor の除外
# --------------------------------------------------------------------------

def case_internal(cd):
    print("== 6: 内部実装の禁止トークン ==", flush=True)
    with tempfile.TemporaryDirectory(prefix="c_dialect_int_") as td:
        t = pathlib.Path(td)
        (t / "kernel").mkdir()
        (t / "lib/sqlite3").mkdir(parents=True)
        (t / "kernel/a.c").write_text(
            "// C11 で許す行コメント\nint f(void) { int a = 0; a++; int b = a; return b; }\n"
            "/* _Atomic はコメントなら可 */\nconst char *s = \"_Thread_local\";\n",
            encoding="utf-8")
        (t / "lib/sqlite3/v.c").write_text("int g(int *restrict p);\n", encoding="utf-8")
        probs = cd.check_internal_tokens(t)
        check(probs == [], "C11 で許す書き方・コメントと文字列の中・vendor は数えない (%r)" % (probs,))
        for tok in ("_Atomic int x;", "_Thread_local int y;", "int h(int *restrict p);",
                    "#include <threads.h>", "#include <stdatomic.h>",
                    "#include /* C11 */ <stdatomic.h>\natomic_int t0_atomic;",
                    "void t0_r(char *re\\\nstrict p) { (void)p; }"):
            (t / "kernel/b.c").write_text(tok + "\n", encoding="utf-8")
            probs = cd.check_internal_tokens(t)
            check(any("kernel/b.c:1" in p for p in probs), "内部実装の %s を拒否する" % tok)


# --------------------------------------------------------------------------
#  7. 実物の木
# --------------------------------------------------------------------------

def run_checker(root):
    return subprocess.run([sys.executable, "-B", str(root / "tools/check_c_dialect.py"),
                           "--root", str(root)], capture_output=True, text=True)


def case_real():
    print("== 7: 実物の木 ==", flush=True)
    r = run_checker(ROOT)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stdout.write(r.stderr)
    check(r.returncode == 0, "実物の木で check_c_dialect.py が rc=0")
    check("gnu89" in r.stdout and "gnu11" in r.stdout, "要約に gnu11 と gnu89 の翻訳単位の数を出す")
    # 親の make のコマンドライン変数が子の make -n に伝わる (Codex P2-1)
    env = dict(os.environ)
    for k in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES"):
        env.pop(k, None)
    r = subprocess.run(["make", "--no-print-directory", "-C", str(ROOT), "check-c-dialect",
                        "C_STD=-std=gnu89"], capture_output=True, text=True, env=env)
    check(r.returncode != 0 and "gnu11 でなく gnu89" in r.stdout,
          "make check-c-dialect C_STD=-std=gnu89 は落ちる (親の変数指定を子の make に渡す)")
    r = subprocess.run(["make", "--no-print-directory", "-j4", "-C", str(ROOT), "check-c-dialect"],
                       capture_output=True, text=True, env=env)
    check(r.returncode == 0, "make -j4 check-c-dialect は通る (ジョブサーバを引き継がない)")


# --------------------------------------------------------------------------
#  否定側 (写しの木に変異を当てて検査器を回す)
# --------------------------------------------------------------------------

def rep(rel, old, new):
    """rel の old (ちょうど 1 か所) を new に替えた中身。当たらなければ None。"""
    text = (ROOT / rel).read_text(encoding="utf-8")
    if text.count(old) != 1:
        return None
    return {rel: text.replace(old, new)}


def append(rel, extra):
    text = (ROOT / rel).read_text(encoding="utf-8")
    return {rel: text + extra}


CFG = "build/config.mk"
DIALECT = ("C_DIALECT_ERRORS = -Werror=implicit-function-declaration -Werror=implicit-int "
           "-Werror=vla")

# (説明, 期待 "RED"/"GREEN", 変異を作る関数)
MUTANTS = [
    ("本体から -Werror=vla を外す (VLA が通る)", "RED",
     lambda: rep(CFG, DIALECT, DIALECT.replace(" -Werror=vla", ""))),
    ("本体から -Werror=implicit-function-declaration を外す (暗黙宣言が通る)", "RED",
     lambda: rep(CFG, DIALECT, DIALECT.replace(" -Werror=implicit-function-declaration", ""))),
    ("本体から -Werror=implicit-int を外す", "RED",
     lambda: rep(CFG, DIALECT, DIALECT.replace(" -Werror=implicit-int", ""))),
    ("偽の STATIC_ASSERT (_Static_assert(1, …) で条件を消す)", "RED",
     lambda: rep("include/types.h", "_Static_assert(cond, #name)", "_Static_assert(1, #name)")),
    ("本体を gnu89 に戻す", "RED",
     lambda: rep(CFG, "C_STD            = -std=gnu11", "C_STD            = -std=gnu89")),
    ("SQLite を gnu11 にする", "RED",
     lambda: rep(CFG, "C_STD_SQLITE     = -std=gnu89", "C_STD_SQLITE     = -std=gnu11")),
    ("SQLite の旗が本体の共通旗を継ぐ (§2 F2 の形に戻す)", "RED",
     lambda: rep(CFG, "CFLAGS_SQLITE = $(C_STD_SQLITE) $(CFLAGS_MACHINE) -Os",
                 "CFLAGS_SQLITE = $(CFLAGS_COMMON) -Os")),
    ("os32_sqlite_test.o を本体の言語指定で組む", "RED",
     lambda: rep("build/kernel.mk", "$(CC) $(C_STD_SQLITE) -m32", "$(CC) $(C_STD) -m32")),
    ("userland の SQLite 単体を gnu11 で組む", "RED",
     lambda: rep("build/programs.mk", "SQLITE_SA_CFLAGS = $(C_STD_SQLITE)",
                 "SQLITE_SA_CFLAGS = $(C_STD)")),
    ("ブートの旗から言語指定を落とす (コンパイラの既定になる)", "RED",
     lambda: rep("build/boot.mk", "CFLAGS_BOOT = $(C_STD) $(C_DIALECT_ERRORS)",
                 "CFLAGS_BOOT = $(C_DIALECT_ERRORS)")),
    ("ブートの旗から拒否の旗を落とす", "RED",
     lambda: rep("build/boot.mk", "CFLAGS_BOOT = $(C_STD) $(C_DIALECT_ERRORS)",
                 "CFLAGS_BOOT = $(C_STD)")),
    ("公開 SDK ヘッダに行コメント", "RED",
     lambda: append("sdk/include/os32/os32api.h", "extern int os32_t0_probe; // C11 only\n")),
    ("公開 SDK ヘッダにブロック途中の宣言", "RED",
     lambda: append("sdk/include/os32/os32api.h",
                    "static __inline__ int os32_t0_f(void) { int a = 1; a++; int b = a; return b; }\n")),
    ("公開 SDK ヘッダに stdbool", "RED",
     lambda: append("sdk/include/os32/os32_gui_shared.h",
                    "#include <stdbool.h>\nextern bool os32_t0_b;\n")),
    ("公開 SDK ヘッダに指示付き初期化子", "RED",
     lambda: append("sdk/include/os32/os32api.h",
                    "struct os32_t0_s { int a; };\n"
                    "static const struct os32_t0_s os32_t0_v = { .a = 1 };\n")),
    ("SDK が配るライブラリヘッダに for の中の宣言 (gnu89 のアプリで通らない)", "RED",
     lambda: append("userland/lib/gfx/libos32gfx.h",
                    "static __inline__ void os32_t0_l(void) { for (int i = 0; i < 1; i++) { } }\n")),
    ("gnu89 の例 (sdk/example/hello) を gnu11 にする", "RED",
     lambda: rep("sdk/example/hello/Makefile", "CFLAGS = -std=gnu89", "CFLAGS = -std=gnu11")),
    ("内部実装に _Atomic", "RED",
     lambda: append("kernel/sysclk.c", "static _Atomic int t0_atomic;\n")),
    ("内部実装に restrict", "RED",
     lambda: append("kernel/sysclk.c", "void t0_r(char *restrict p);\n")),
    ("内部実装に #include /* C11 */ <stdatomic.h> と atomic_int (Codex P2-2 反例 a)", "RED",
     lambda: append("kernel/sysclk.c",
                    "#include /* C11 */ <stdatomic.h>\natomic_int t0_atomic;\n")),
    ("内部実装に行継続で割った restrict (Codex P2-2 反例 b)", "RED",
     lambda: append("kernel/sysclk.c", "void t0_r(char *re\\\nstrict p) { (void)p; }\n")),
    # --- 対照 (C11 で許す書き方。落ちたら検査器が厳しすぎる) ---
    ("対照: 内部実装に // とブロック途中の宣言", "GREEN",
     lambda: append("kernel/sysclk.c",
                    "// C11 で許す行コメント\n"
                    "int t0_c11_ok(void) { int a = 0; a++; int b = a; return b; }\n")),
    ("対照: 公開 SDK ヘッダのコメントと文字列に // と restrict", "GREEN",
     lambda: append("sdk/include/os32/os32api.h",
                    "/* restrict // _Bool */\n#define OS32_T0_STR \"a//b restrict\"\n")),
    ("対照: 恒等 (何も変えない)", "GREEN",
     lambda: append("sdk/include/os32/os32api.h", "")),
]


def mutate_one(item):
    i, (desc, want, make) = item
    edits = make()
    if edits is None:
        return (i, desc, want, "NOT_APPLIED", "")
    with tempfile.TemporaryDirectory(prefix="c_dialect_mut_") as td:
        real = set(edits) | {"tools/check_c_dialect.py"}
        tree = mutpar.mutant_tree(ROOT, pathlib.Path(td) / "tree", edits, real=real)
        r = run_checker(tree)
        got = "GREEN" if r.returncode == 0 else "RED"
        tail = (r.stdout + r.stderr).strip().splitlines()[-1:] if got == "RED" else []
        return (i, desc, want, got, tail[0] if tail else "")


def run_mutations():
    print("== 否定側 (写しの木に変異を当てる) ==", flush=True)
    bad = 0
    red = green_ctl = 0
    for i, desc, want, got, tail in mutpar.run_ordered(mutate_one, list(enumerate(MUTANTS, 1))):
        ok = got == want
        if not ok:
            bad += 1
        if want == "RED" and ok:
            red += 1
        if want == "GREEN" and ok:
            green_ctl += 1
        label = "MUTATION %d %s" % (i, got) if want == "RED" else "MUTATION CONTROL %d %s" % (i, got)
        print("%s%s: %s%s" % ("" if ok else "UNEXPECTED ", label, desc,
                                (" -- " + tail) if tail else ""), flush=True)
    n_red = sum(1 for m in MUTANTS if m[1] == "RED")
    n_ctl = len(MUTANTS) - n_red
    print("MUTATIONS %d/%d RED; CONTROLS %d/%d GREEN" % (red, n_red, green_ctl, n_ctl), flush=True)
    return bad == 0


def main(argv):
    cd = load()
    case_lex(cd)
    case_parse(cd)
    case_effective(cd)
    case_probe(cd)
    case_sdk(cd)
    case_internal(cd)
    case_real()
    ok = not FAILED
    print("%d checks, %d failed" % (N[0], len(FAILED)), flush=True)
    if "--mutate" in argv:
        ok = run_mutations() and ok
    print("PASS" if ok else "FAIL", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
