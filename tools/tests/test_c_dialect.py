"""tools/check_c_dialect.py (言語モードの検査、[C1]) のホスト試験。

記録: tools/tests/c_dialect_tdd.md
票:   docs/archive/v3/TASK_C11_MIGRATION.md §6 段 4・段 5

検査器の部品 (字句の読み分け・コンパイル行の読み取り・実際に効いている言語モード・
拒否の探り・公開 SDK ヘッダの検査) を小さな入力で固定する。実物の木は check-c-dialect が見る。

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
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
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
#  1. 字句 (本物の前処理器 -E に任せ、出力の行標識で行を得る)
# --------------------------------------------------------------------------

W = {"restrict", "_Atomic", "_Thread_local"}
H = {"stdatomic.h", "stdbool.h", "threads.h"}


def case_lex(cd):
    print("== 1: 字句 (前処理後の出力・行標識) ==", flush=True)

    def sc(text):
        return cd.scan_c_text(text, ROOT, W, H)
    check(sc('/* restrict */ int f(int *restrict p);\n') == [(1, "restrict")],
          "コメントの中の restrict は数えず、コードのものだけ数える")
    check(sc('const char *s = "_Atomic";\nint *__restrict q;\nchar c = \'"\'; int *restrict r;\n')
          == [(2, "restrict"), (3, "restrict")],
          "文字列の語は数えず、__restrict も型として数え、文字定数の '\"' で文字列に入らない")
    check(sc('const char *s = "\\" restrict";\n') == [], "文字列の \\\" で文字列を抜けたことにしない")
    check(sc('#include <stdbool.h>\n#include <stdatomic.h>\n') == [(1, "stdbool.h"), (2, "stdatomic.h")],
          "#include のヘッダを取り込んだ行で見つける")
    check(sc('int a;\n#include /* C11 */ <stdatomic.h>\n') == [(2, "stdatomic.h")],
          "#include とヘッダ名の間のコメント (Codex 1 回目 反例 a)")
    check(sc('int a;\n/* a\n b */ #include <stdatomic.h>\n') == [(3, "stdatomic.h")],
          "複数行コメントの後ろの #include は # のある行で数える (Codex 2 回目 P3)")
    check(sc('int a;\nvoid t0_r(char *re\\\nstrict p);\n') == [(2, "restrict")],
          "行継続で割った restrict を元の行番号で (Codex 1 回目 反例 b)")
    check(sc('int a;\nvoid t0_r(char *re\\ \nstrict p);\n') == [(2, "restrict")],
          "バックスラッシュと改行の間に空白がある行継続 (Codex 2 回目 反例 3)")
    check(sc('int a;\nvoid t0_r(char *re\\\t\nstrict p);\n') == [(2, "restrict")],
          "バックスラッシュと改行の間にタブがある行継続")
    check(sc('#\vinclude <stdatomic.h>\n') == [(1, "stdatomic.h")], "#\\vinclude (VT、Codex 2 回目 反例 4)")
    check(sc('#\finclude <stdbool.h>\n') == [(1, "stdbool.h")], "#\\finclude (FF)")
    check(sc('#inc\\\nlude <stdatomic.h>\n') == [(1, "stdatomic.h")],
          "行継続で割った #include (行は clang の物理開始行)")
    check(sc('#define X _Atomic\nX int y;\n') == [(2, "_Atomic")], "マクロで隠した _Atomic も展開後に数える")
    check(sc('#if 0\n_Atomic int z;\n#endif\nint w;\n') == [],
          "実際の旗で組まれない部分 (#if 0) は数えない (コンパイラの判定に従う)")


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
    check(cd.expected_std("lib/sqlite3/os32_sqlite_test.c") == "gnu11", "core の SQLite 自己試験は gnu11")
    check(cd.expected_std("/tmp/out/sqlite-proto/os32_sqlite_vfs.c") == "gnu89",
          "生成した module VFS も gnu89")
    check(cd.source_identity("/tmp/out/sqlite-proto/os32_sqlite_vfs.c") ==
          "lib/sqlite3/os32_sqlite_vfs.c", "生成した VFS を必須翻訳単位として数える")
    check(cd.expected_std("userland/tests/sqlite_standalone/sqlite_user_vfs.c") == "gnu89",
          "userland の SQLite 単体は gnu89")
    check(cd.expected_std("kernel/kernel.c") == "gnu11", "本体は gnu11")
    check(cd.expected_std("boot/boot_main.c") == "gnu11", "ブートは gnu11")
    check(cd.expected_std("userland/lib/gfx/gfx.c") == "gnu11", "userland は gnu11")
    a = units[0]["argv"]
    check("-Iinclude" in a and "kernel/kernel.c" in a and "-c" not in a and "-MMD" not in a
          and "-o" not in a and "kernel/kernel.o" not in a,
          "前処理用の引数は -I とソースを残し、依存生成・-c・-o を外す")


def case_dry_run_lifetime(cd):
    # Overlap producers, then consume their headers after both have returned.
    # A tiny make graph keeps this independent of the full AST scan.
    from concurrent.futures import ThreadPoolExecutor
    with tempfile.TemporaryDirectory(prefix="c_dialect_lifetime_") as td:
        root = pathlib.Path(td)
        (root / "include").mkdir()
        (root / "include/memmap.h").write_text("#define MEM_TEST 123\n")
        (root / "Makefile").write_text(
            "all:\n\t i386-elf-gcc -include $(BUILD_OUT)/memmap_layout.h "
            "-c probe.c -o probe.o\n")
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(cd.dry_run, [root, root]))
        headers = []
        for rc, out, err in results:
            check(rc == 0, "parallel dry-run succeeds: " + err)
            units = cd.parse_compile_lines(out)
            check(len(units) == 1, "parallel dry-run retains its command")
            argv = units[0]['argv']
            headers.append(pathlib.Path(argv[argv.index('-include') + 1]))
        check(headers[0] != headers[1], "parallel dry-runs own distinct headers")
        check(all(h.is_file() and '#define LAYOUT_MEM_TEST 0x7B' in h.read_text()
                  for h in headers), "headers survive both producers returning")
        headers[0].unlink()
        check(headers[1].is_file(), "removing one header leaves the other intact")


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
    s = cd.strip_jobserver(" -j4 --jobserver-auth=fifo:/tmp/GMfifo1 -- B:=1 D=e\\f A=b\\ c C_STD=-std=gnu89")
    check(s == "-- B:=1 D=e\\f A=b\\ c C_STD=-std=gnu89",
          "MAKEFLAGS から -j / jobserver だけを除き、変数部分はバイト列のまま (%r)" % (s,))
    s = cd.strip_jobserver("e -j8 --jobserver-fds=3,4 --no-print-directory")
    check(s == "e --no-print-directory", "単文字旗の束 (e) と他の旗は残す (%r)" % (s,))
    check(cd.strip_jobserver("-- X=1") == "-- X=1", "旗なしの MAKEFLAGS はそのまま")
    check(cd.strip_jobserver("") == "", "空はそのまま")


def case_cross_free(cd):
    from unittest import mock
    env = {'PATH': '/usr/bin:/bin', 'CROSS_DIR': '/nonexistent',
           'OS32_CLANG_CROSS_FREE': '1'}
    with mock.patch.dict(os.environ, env), \
            mock.patch.object(cd.ast, 'SYSTEM_NEWLIB', pathlib.Path('/nonexistent/newlib')):
        check(cd.ast.cross_compiler() is None, 'cross-free regression has no cross GCC')
        check(cd.effective_std(GOOD, ROOT) == 'gnu11', 'cross-free clang gnu11 mode')
        check(cd.effective_std(['-std=gnu11', '-std=gnu89'], ROOT) == 'gnu89',
              'cross-free clang honors the last -std')
        check(cd.probe_rejects(GOOD, ROOT) == [], 'cross-free clang rejection probes')
        check('vla' in cd.probe_rejects([f for f in GOOD if f != '-Werror=vla'], ROOT),
              'cross-free missing VLA rejection flag is detected')
        check(cd.check_sdk_sample(ROOT) == [], 'cross-free gnu89 SDK example')
        with mock.patch.dict(os.environ, {'OS32_CLANG_CROSS_FREE': '0'}):
            check(cd.main([]) == 2, 'missing cross GCC fails closed without explicit opt-in')
    check(cd.compiler_args(cd.CC, GOOD, ROOT) == [cd.ast.cross_compiler(cd.CC) or cd.CC] + GOOD,
          'normal GCC probes preserve the compiler and every flag')
    cross = cd.ast.cross_compiler(cd.CC)
    if cross:
        with mock.patch.dict(os.environ, {'PATH': '/usr/bin:/bin',
                'CROSS_DIR': str(pathlib.Path(cross).resolve().parents[1]),
                'OS32_CLANG_CROSS_FREE': '0'}):
            check(cd.ast.shutil.which('i386-elf-gcc') is None,
                  'cross GCC regression uses CROSS_DIR without a PATH entry')
            check(cd.compiler_args('i386-elf-gcc', GOOD, ROOT) == [cross] + GOOD,
                  'GCC probes use the resolved cross compiler path and preserve every flag')
            check(cd.effective_std(GOOD, ROOT, 'i386-elf-gcc') == 'gnu11',
                  'GCC probes run with only CROSS_DIR configured')


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
        ("行継続で割った //", "int os32_a; /\\\n/ x\n"),
        ("展開されないマクロの置換列の _Static_assert (Codex 3 回目 (1))",
         "#define OS32_A(x) _Static_assert(x, \"x\")\n"),
        ("#\\f で取り込む stdbool", "#\finclude <stdbool.h>\nextern int os32_b;\n"),
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
    print("== 6: 内部実装の禁止トークン (翻訳単位を実際の旗で前処理) ==", flush=True)

    def unit(src):
        return {"src": src, "cc": cd.CC, "sig": (),
                "argv": ["-std=gnu11", "-ffreestanding", "-Iinclude", src]}
    with tempfile.TemporaryDirectory(prefix="c_dialect_int_") as td:
        t = pathlib.Path(td)
        (t / "kernel").mkdir()
        (t / "lib/sqlite3").mkdir(parents=True)
        (t / "kernel/a.c").write_text(
            '#include "../lib/sqlite3/v.h"\n'
            "// C11 で許す行コメント\nint f(void) { int a = 0; a++; int b = a; return b; }\n"
            "/* _Atomic はコメントなら可 */\nconst char *s = \"_Thread_local\";\n",
            encoding="utf-8")
        (t / "lib/sqlite3/v.h").write_text("int gv(int *restrict p);\n", encoding="utf-8")
        (t / "lib/sqlite3/v.c").write_text("int g(int *restrict p);\n", encoding="utf-8")
        probs, n = cd.check_internal_units([unit("kernel/a.c"), unit("lib/sqlite3/v.c")], t)
        check(probs == [] and n == 1,
              "C11 で許す書き方・コメントと文字列の中・vendor (元ソースもヘッダも) は数えない (%r)" % (probs,))
        cases = [
            ("_Atomic int x;", "kernel/b.c:1"),
            ("_Thread_local int y;", "kernel/b.c:1"),
            ("int h(int *restrict p);", "kernel/b.c:1"),
            ("#include <stdatomic.h>", "kernel/b.c:1"),
            ("#include <threads.h>", "kernel/b.c"),  # freestanding には無い = 前処理で落ちる
            ("#include /* C11 */ <stdatomic.h>\natomic_int t0_atomic;", "kernel/b.c:1"),
            ("void t0_r(char *re\\\nstrict p) { (void)p; }", "kernel/b.c:1"),
            ("void t0_r(char *re\\ \nstrict p) { (void)p; }", "kernel/b.c:1"),
            ("#\vinclude <stdatomic.h>", "kernel/b.c:1"),
            ("#\finclude <stdatomic.h>", "kernel/b.c:1"),
            ("int q;\n/* a\n b */ #include <stdatomic.h>", "kernel/b.c:3"),
            ('#include "b.h"', "kernel/b.h:1"),
        ]
        (t / "kernel/b.h").write_text("_Atomic int t0_hdr;\n", encoding="utf-8")
        for body, where in cases:
            (t / "kernel/b.c").write_text(body + "\n", encoding="utf-8")
            probs, _ = cd.check_internal_units([unit("kernel/b.c")], t)
            ok = (any(p.startswith(where + ":") for p in probs) if ":" in where
                  else any(where in p for p in probs))
            check(ok,
                  "内部実装の %r を %s で拒否する (%r)" % (body, where, probs[:2]))
        (t / 'lib/sqlite3/v.c').write_text('this is not C;\n')
        probs,n = cd.check_internal_units([unit('lib/sqlite3/v.c')],t)
        check(n == 0 and any('clang parse failure' in p for p in probs),
              'vendor は規則免除でも解析失敗を飛ばさない (fail closed、変異REDには数えない)')


def case_suite_cache_owner(cd):
    """The cache is honoured only while its owning suite is alive."""
    try:
        cd.process_start(os.getpid())
    except OSError:
        print("  skip キャッシュの所有者 (/proc が無い — キャッシュは使われず全部を検査する)", flush=True)
        return
    old = os.environ.get('OS32_C_DIALECT_TEST_CACHE')
    try:
        with tempfile.TemporaryDirectory(prefix='c_dialect_owner_') as d:
            os.environ['OS32_C_DIALECT_TEST_CACHE'] = d
            check(cd._suite_cache() is None, "所有者の印の無いキャッシュは使わない")
            me = os.getpid()
            pathlib.Path(d, cd.SUITE_CACHE_OWNER).write_text('%d %s' % (me, cd.process_start(me)))
            check(cd._suite_cache() == d, "生きている suite のキャッシュは使う")
            dead = subprocess.Popen([sys.executable, "-c", "pass"])
            dead.wait()
            pathlib.Path(d, cd.SUITE_CACHE_OWNER).write_text('%d 1' % dead.pid)
            check(cd._suite_cache() is None, "終わった suite のキャッシュ (残骸・手で指定) は使わない")
            pathlib.Path(d, cd.SUITE_CACHE_OWNER).write_text('%d %d' % (me, int(cd.process_start(me)) + 1))
            check(cd._suite_cache() is None, "使い回された pid (開始時刻が違う) のキャッシュは使わない")
            pathlib.Path(d, cd.SUITE_CACHE_OWNER).write_text(str(me))
            check(cd._suite_cache() is None, "開始時刻の無い印は使わない")
    finally:
        if old is None:
            os.environ.pop('OS32_C_DIALECT_TEST_CACHE', None)
        else:
            os.environ['OS32_C_DIALECT_TEST_CACHE'] = old


def case_internal_cache(cd):
    from unittest import mock
    with tempfile.TemporaryDirectory(prefix='c_dialect_cache_test_') as td:
        base = pathlib.Path(td)
        cache = base / 'cache'
        cache.mkdir()
        source = base / 'source'
        (source / 'kernel').mkdir(parents=True)
        (source / 'include').mkdir()
        (source / 'kernel/a.c').write_text('#include "probe.h"\n')
        (source / 'include/probe.h').write_text('extern int value;\n')
        unit = {'src': 'kernel/a.c', 'cc': cd.CC, 'sig': (),
                'argv': ['-std=gnu11', '-ffreestanding', '-Iinclude', 'kernel/a.c']}

        def scan(root, u=unit):
            return cd.check_internal_units([u], root, cache=cache)

        with mock.patch.object(cd.ast, 'parse', wraps=cd.ast.parse) as parse:
            check(scan(source) == ([], 1), 'cache warmup scans the actual TU')
            calls = parse.call_count
            check(scan(source) == ([], 1) and parse.call_count == calls,
                  'unchanged dependencies reuse the successful scan')
            tree = mutpar.mutant_tree(source, base / 'copy', {},
                                      real={'kernel/a.c', 'include/probe.h'})
            check(scan(tree) == ([], 1) and parse.call_count == calls,
                  'identical copied trees reuse the scan')
            generated = []
            for label in ('a', 'z'):
                out = base / ('c_dialect_out_' + label) / 'out'
                out.mkdir(parents=True)
                h = out / 'generated.h'
                h.write_text('extern int generated;\n')
                generated.append(dict(unit, argv=unit['argv'] + ['-include', str(h)]))
            check(scan(source, generated[0]) == ([], 1), 'generated dependency warms cache')
            calls = parse.call_count
            check(scan(tree, generated[1]) == ([], 1) and parse.call_count == calls,
                  'identical generated headers reuse scans across dry-run directories')
            (base / 'c_dialect_out_z/out/generated.h').write_text('this is not C;\n')
            check(any('clang parse failure' in p for p in scan(tree, generated[1])[0]),
                  'changed generated header invalidates the scan')
            header = tree / 'include/probe.h'
            saved = header.stat()
            header.write_text('_Atomic int value;\n')
            os.utime(header, ns=(saved.st_atime_ns, saved.st_mtime_ns))
            check(any('_Atomic' in p for p in scan(tree)[0]) and parse.call_count > calls,
                  'mutated header is reread even with the original timestamp')
            check(any('_Atomic' in p for p in scan(tree)[0]),
                  'a repeated policy failure never becomes a successful cache hit')
            (tree / 'kernel/a.c').write_text('int f(int *restrict p);\n')
            check(any('restrict' in p for p in scan(tree)[0]),
                  'mutated source is reread across copied trees')
            (tree / 'kernel/a.c').write_text('this is not C;\n')
            check(any('clang parse failure' in p for p in scan(tree)[0]),
                  'cache never hides a parse failure')
            (tree / 'kernel/a.c').write_text('#ifdef BAD\n_Atomic int x;\n#endif\n')
            check(scan(tree) == ([], 1), 'conditional source warms the cache')
            changed = dict(unit, argv=unit['argv'] + ['-DBAD'])
            check(any('_Atomic' in p for p in scan(tree, changed)[0]),
                  'changed compiler flags invalidate the scan')
            (tree / 'kernel/a.c').write_text('#include "probe.h"\n')
            header.write_text('extern int value;\n')
            check(scan(tree) == ([], 1), 'restored header reuses the clean scan')
            (tree / 'kernel/probe.h').write_text('_Atomic int shadow;\n')
            check(any('_Atomic' in p for p in scan(tree)[0]),
                  'new shadowing header changes compiler-resolved dependencies')
            (tree / 'kernel/probe.h').unlink()
            header.unlink()
            check(any('clang parse failure' in p for p in scan(tree)[0]),
                  'deleted dependency is not a cache hit')
            (tree / 'kernel/a.c').write_text(
                '#if __has_include("optional.h")\n_Atomic int optional;\n#endif\n')
            check(scan(tree) == ([], 1), '__has_include absence warms the cache')
            (tree / 'kernel/optional.h').write_text('/* not actually included */\n')
            check(any('_Atomic' in p for p in scan(tree)[0]),
                  '__has_include change invalidates even without a new dependency')
            (tree / 'kernel/a.c').write_text(
                'typedef char root_length[(sizeof(__FILE__) == %d) ? 1 : -1];\n'
                % (len(str(tree / 'kernel/a.c')) + 1))
            check(scan(tree) == ([], 1), '__FILE__ dependent source warms cache')
            copy = mutpar.mutant_tree(tree, base / 'longer-copy', {}, real={'kernel/a.c'})
            check(any('clang parse failure' in p for p in scan(copy)[0]),
                  'expanded __FILE__ strings retain their actual root')
            broken = dict(unit, argv=unit['argv'] + ['-std=invalid'])
            check(any('clang parse failure' in p for p in scan(tree, broken)[0]),
                  'invalid compiler flags retain the full-check parse failure verdict')


# --------------------------------------------------------------------------
#  7. make の環境変数・override の変種
# --------------------------------------------------------------------------

def run_checker(root):
    return subprocess.run([sys.executable, "-B", str(root / "tools/check_c_dialect.py"),
                           "--root", str(root)], capture_output=True, text=True)


def case_build_coverage(cd):
    # Exercise the same flag pass used by the full checker, with a tiny dry run.
    from unittest import mock
    lines = "i386-elf-gcc -std=gnu11 -c kernel/kernel.c -o kernel/kernel.o\n"
    for sqlite, want_ok in [(False, False), (True, True)]:
        out = lines + ("i386-elf-gcc -std=gnu89 -c lib/sqlite3/sqlite3.c -o sqlite.o\n" if sqlite else "")
        with mock.patch.object(cd, 'dry_run', return_value=(0, out, '')), \
             mock.patch.object(cd, 'REQUIRED_SRCS', []), \
             mock.patch.object(cd, 'probe_rejects', return_value=[]):
            problems, summary = cd.check_build_flags(ROOT)
        check((not problems) == want_ok, 'gnu89 TU coverage: sqlite=%s' % sqlite)
        check(summary['counts'].get('gnu11', 0) > 0, 'gnu11 TU count retained')


def case_make_variants():
    # 親の make の旗と変数指定は MAKEFLAGS のまま子の make -n に渡す (Codex P2-1、2 回目 1・2)
    base = dict(os.environ)
    for k in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES"):
        base.pop(k, None)
    runs = [
        ("make check-c-dialect C_STD=-std=gnu89", ["check-c-dialect", "C_STD=-std=gnu89"], {}, False),
        ("C_STD=-std=gnu89 make -e check-c-dialect", ["-e", "check-c-dialect"],
         {"C_STD": "-std=gnu89"}, False),
        ("make check-c-dialect 'X=foo\\' C_STD=-std=gnu89",
         ["check-c-dialect", "X=foo\\", "C_STD=-std=gnu89"], {}, False),
        ("make check-c-dialect C_STD=-std=gnu89 'X=foo\\' (MAKEFLAGS では X が C_STD の前)",
         ["check-c-dialect", "C_STD=-std=gnu89", "X=foo\\"], {}, False),
        ("make check-c-dialect 'C_STD=$(MODE)' MODE=-std=gnu11",
         ["check-c-dialect", "C_STD=$(MODE)", "MODE=-std=gnu11"], {}, True),
        ("make check-c-dialect 'C_STD=$(MODE)' MODE=-std=gnu89",
         ["check-c-dialect", "C_STD=$(MODE)", "MODE=-std=gnu89"], {}, False),
        ("make check-c-dialect 'T0_TAB=a<TAB>b'", ["check-c-dialect", "T0_TAB=a\tb"], {}, True),
        ("make check-c-dialect 'C_STD=-std=gnu11<TAB>'", ["check-c-dialect", "C_STD=-std=gnu11\t"], {}, True),
        ("make -j4 check-c-dialect", ["-j4", "check-c-dialect"], {}, True),
    ]

    def go(item):
        desc, args, extra, want_ok = item
        r = subprocess.run(["make", "--no-print-directory", "-C", str(ROOT)] + args,
                           capture_output=True, text=True, env=dict(base, **extra), stdin=subprocess.DEVNULL)
        return desc, want_ok, r.returncode, (r.stdout + r.stderr).strip().splitlines()[-1:]
    for desc, want_ok, rc, tail in mutpar.run_ordered(go, runs):
        if want_ok:
            check(rc == 0, "%s は通る (rc=%d %s)" % (desc, rc, tail))
        else:
            check(rc != 0, "%s は落ちる (rc=%d %s)" % (desc, rc, tail))


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
    ("core の os32_sqlite_test.o を gnu89 に戻す", "RED",
     lambda: rep("build/kernel.mk",
                 "$(KERNEL_OBJ_PREFIX)lib/sqlite3/os32_sqlite_test.o: lib/sqlite3/os32_sqlite_test.c\n"
                 "\t@mkdir -p $(@D)\n\t$(CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@",
                 "$(KERNEL_OBJ_PREFIX)lib/sqlite3/os32_sqlite_test.o: lib/sqlite3/os32_sqlite_test.c\n"
                 "\t@mkdir -p $(@D)\n\t$(CC) $(CFLAGS_BASE) $(C_STD_SQLITE) $(INC_KERNEL) -c $< -o $@")),
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
    ("内部実装に \\ と改行の間に空白がある行継続で割った restrict (Codex 2 回目 反例 3)", "RED",
     lambda: append("kernel/sysclk.c", "void t0_r2(char *re\\ \nstrict p) { (void)p; }\n")),
    ("内部実装に #\\vinclude <stdatomic.h> (Codex 2 回目 反例 4、VT)", "RED",
     lambda: append("kernel/sysclk.c", "#\vinclude <stdatomic.h>\n")),
    ("内部実装に #\\finclude <stdatomic.h> (Codex 2 回目 反例 4、FF)", "RED",
     lambda: append("kernel/sysclk.c", "#\finclude <stdatomic.h>\n")),
    ("公開 SDK ヘッダに展開されないマクロ #define OS32_ASSERT(x) _Static_assert(x, \"x\") (Codex 3 回目 (1))",
     "RED",
     lambda: append("sdk/include/os32/os32api.h", "#define OS32_ASSERT(x) _Static_assert(x, \"x\")\n")),
    ("保存済み lgy98.flags=5 のときだけ組まれる #if CONFIG_LGY98_FLAGS == 5 の中の _Atomic (Codex 3 回目 (5))",
     "RED",
     lambda: dict(append("kernel/sysclk.c",
                         "#if defined(CONFIG_LGY98_FLAGS) && CONFIG_LGY98_FLAGS == 5\n"
                         "static _Atomic int t0_lan;\n#endif\n"),
                  **{"build/out/lgy98.flags": "5\n"})),
    ("内部実装に複数行コメントの後ろの #include <stdatomic.h> (Codex 2 回目 P3)", "RED",
     lambda: append("kernel/sysclk.c", "/* a\n b */ #include <stdatomic.h>\n")),
    # --- 対照 (C11 で許す書き方。落ちたら検査器が厳しすぎる) ---
    ("対照: 内部実装に // とブロック途中の宣言", "GREEN",
     lambda: append("kernel/sysclk.c",
                    "// C11 で許す行コメント\n"
                    "int t0_c11_ok(void) { int a = 0; a++; int b = a; return b; }\n")),
    ("対照: 公開 SDK ヘッダのコメントと文字列に // と restrict", "GREEN",
     lambda: append("sdk/include/os32/os32api.h",
                    "/* restrict // _Bool */\n#define OS32_T0_STR \"a//b restrict\"\n")),
    ("対照: lgy98.flags=0 なら #if CONFIG_LGY98_FLAGS == 5 の中は組まれない", "GREEN",
     lambda: dict(append("kernel/sysclk.c",
                         "#if defined(CONFIG_LGY98_FLAGS) && CONFIG_LGY98_FLAGS == 5\n"
                         "static _Atomic int t0_lan;\n#endif\n"),
                  **{"build/out/lgy98.flags": "0\n"})),
    ("対照: 公開 SDK ヘッダのマクロ置換列の文字列の中の _Static_assert", "GREEN",
     lambda: append("sdk/include/os32/os32api.h", "#define OS32_T0_S \"_Static_assert restrict\"\n")),
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
        if want == 'RED' and ('clang parse failure' in r.stdout or r.returncode != 1):
            got = 'PARSE_FAILURE_NOT_COUNTED'
        tail = ((r.stdout + r.stderr).strip().splitlines()[-1:] if got == "RED"
                else (r.stdout + r.stderr).strip().splitlines() if got != want else [])
        return (i, desc, want, got, "\n".join(tail))


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


def case_sqlite_rule(cd):
    # Check the previously missed recipe even when MUTATE=0; no full AST run.
    edits = MUTANTS[7][2]()
    check(edits is not None, "SQLite test mode edit matches the alternate rule")
    if edits is None:
        return
    with tempfile.TemporaryDirectory(prefix="c_dialect_rule_") as td:
        tree = mutpar.mutant_tree(ROOT, pathlib.Path(td) / 'tree', edits,
                                  real=set(edits))
        rc, out, err = cd.dry_run(tree)
        check(rc == 0, "SQLite recipe dry-run succeeds: " + err)
        units = [u for u in cd.parse_compile_lines(out)
                 if u['src'] == 'lib/sqlite3/os32_sqlite_test.c']
        check(bool(units) and all(cd.effective_std(u['sig'], tree, u['cc']) == 'gnu89'
                                  for u in units),
              "SQLite test mode edit reaches the actual compile command")


def main(argv):
    cd = load()
    case_lex(cd)
    case_parse(cd)
    case_dry_run_lifetime(cd)
    case_sqlite_rule(cd)
    case_effective(cd)
    case_cross_free(cd)
    case_probe(cd)
    case_sdk(cd)
    case_internal(cd)
    case_internal_cache(cd)
    case_suite_cache_owner(cd)
    case_build_coverage(cd)
    sys.path.insert(0, str(ROOT / "tools"))
    import check_select
    if "--mutate" in argv or check_select.dialect_variants_needed():
        warm = run_checker(ROOT)
        check(warm.returncode == 0, 'unmodified tree warms the disposable internal cache: '
              + (warm.stdout + warm.stderr if warm.returncode else 'GREEN'))
        case_make_variants()
    else:
        print("make variants: unchanged build/checker; skipped", flush=True)
    ok = not FAILED
    print("%d checks, %d failed" % (N[0], len(FAILED)), flush=True)
    if "--mutate" in argv:
        ok = run_mutations() and ok
    print("PASS" if ok else "FAIL", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    # Cache only successful internal TU checks, for this one suite invocation.
    # Every checker still reads its real make graph, flags, probes and SDK inputs.
    with tempfile.TemporaryDirectory(prefix='c_dialect_suite_cache_') as cache:
        sys.path.insert(0, str(ROOT / "tools"))
        import check_c_dialect as owner  # noqa: E402
        try:
            pathlib.Path(cache, owner.SUITE_CACHE_OWNER).write_text(
                '%d %s' % (os.getpid(), owner.process_start(os.getpid())))
            os.environ['OS32_C_DIALECT_TEST_CACHE'] = cache
        except OSError:
            pass  # No /proc: run every translation unit without the cache.
        sys.exit(main(sys.argv[1:]))
