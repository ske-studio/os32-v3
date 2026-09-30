"""試験プログラムの「合否の出し方」を固定する (票 docs/archive/test/TASK_TEST_RESULT.md §2 / §11)。

記録: tools/tests/result_conv_tdd.md

ゲストの試験の合否を人が画面を読まずに判定できるようにするには、

  * 終了コードが 0 (全部合格) / 1 (1 件以上不合格) / 2 (実行しなかった) のどれかで、
    シェルの予約値 126 / 127 / 130 / 139 を**返さない**こと、
  * 最終行に `<名前>: PASS <n>/<m>` / `FAIL <n>/<m>` / `SKIP <理由>` が 1 行出ること、
  * **その 2 つが必ず一致する**こと (片方だけ直すとランナーが 2 つの答えを持つ)、
  * その集計行が **fd 1 に出る**こと (票 §11。`api->kprintf` は画面へ直に書くので
    リダイレクトを素通りし、ランナーは 16 本中 14 本を取りこぼした)

の 4 つが要る。この試験はそれを 2 段で見る。

  (A) 実行時 — tools/tests/test_result_conv_host.c が実物の
      userland/lib/rt/testresult.h と、userland/tests/ の**実物のプログラム 5 本**
      (stat_t / restest / test2 / klibc_test / font_load_test) を贋物の KernelAPI で
      走らせ、**どの fd に何が書かれたか**と `main` の返り値の両方を観測する。
      贋物は行き先ごとに別のバッファを持つので、「画面に出た」と「fd 1 に出た」を
      区別できる — 同じ 1 本のバッファへ流す贋物では票 §11 の穴はまた見逃される。
      合格側・不合格側・SKIP 側の 3 通りと、argv[0] を変えても集計行が動かないこと、
      短い書き込みを「書けた」ことにしないことを踏む。
      **grep では一致は確かめられない** — 「PASS と出して 1 を返す」版も
      grep はどちらの行も通してしまう。

  (B) 静的 — 第 1 陣 (票 §4) と、`rt/testresult.h` を取り込んだ試験すべてについて
      `void main` が残っていないこと、集計行の名前が固定文字列であること
      (argv[0] 由来にしない)、`main` の return が必ず集計の答えを返していること、
      予約値をそのまま返す経路が無いこと、**ヘッダの内部 (`os32_test__*`) を
      直接呼んでいないこと** (呼べると行と終了コードを別々に作れてしまう) を見る。
      ヘッダ自身についても、出し口が 1 つ (fd 1) だけであることを見る。

      Rust の alloc_demo は C のヘッダを使えないので、書式と終了コードが
      testresult.h と同じであることをここで突き合わせる。

使い方:

    python3 -B tools/tests/test_result_conv.py [--target] [--mutate]

--target を付けると、適合させた試験が実機と同じ i386-elf クロスコンパイラでも
-Werror で通ることを確かめる ([C1] GNU11)。

--mutate は**否定側**。集計行を kprintf に戻す (fd 1 に出ない) / 短い書き込みを
「書けた」ことにする / 行と終了コードを別々の呼び出しで作る / 集計行と終了コードを
食い違わせる / 予約値を返す / `void main` に戻す / 集計行の名前を argv[0] 由来に
する / 約束事の本体を壊す、をそれぞれ作り、この試験がちゃんと RED になることを
見る。**どれもコンパイルは通る** — コンパイルエラーで落ちるだけなら試験の目が
働いたことにならない。

make・エミュレータ・実配備には一切触れない。
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]

HARNESS = ROOT / "tools/tests/test_result_conv_host.c"

# 実物のプログラムを取り込む翻訳単位。**列挙で拾わず名前で持つ** — ここが
# docs/TESTS.md の「対象ソース」の出どころにもなる (tools/gen_tests_inventory.py
# が SHIMS の中身を読んで #include 先を辿る)。
SHIMS = [
    "tools/tests/result_conv/run_stat_t.c",
    "tools/tests/result_conv/run_restest.c",
    "tools/tests/result_conv/run_test2.c",
    "tools/tests/result_conv/run_klibc_test.c",
    "tools/tests/result_conv/run_font_load_test.c",
]
CONV_HEADER = ROOT / "userland/lib/rt/testresult.h"
RUST_DEMO = ROOT / "userland/rust/alloc_demo/src/lib.rs"
TESTS_DIR = ROOT / "userland/tests"

# 票 §4 の第 1 陣。**ここが唯一の管理元** — 増やすのはこの表だけ。
# これ以外にも rt/testresult.h を取り込んだ試験は自動で検査の対象になる。
WAVE1 = [
    "klibc_test", "db_test", "math_test", "mgx_test", "save_test", "restest",
    "stat_t", "asset_test", "gui_call_test", "test2", "e2test", "ecs_test",
    "db_v50_test", "host_test", "input_test", "font_load_test",
]

HOST_FLAGS = ["-std=gnu11", "-Wall", "-Wextra",
              "-D__cdecl=", "-D__OS32_USERLAND__"]
HOST_INC = ["-I" + str(ROOT / p) for p in
            (".", "include", "sdk/include", "sdk/include/os32", "userland/lib")]

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt

# -Wno-shift-negative-value: gnu11 では -Wextra が負値の左シフトを警告する (gnu89 では
# 出ない)。userland/lib/math/libos32math.h の FIX16_FROM_INT(負値) が当たる。GCC は
# 2 の補数として定義しており、本番の旗 (USER_CFLAGS、-Wextra なし) では出ない。
# マクロの書き換えは T0 の範囲外 (票 TASK_C11_MIGRATION §6 段 3 の結果)。
TARGET_FLAGS = ["-std=gnu11", "-Wno-shift-negative-value", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-fsigned-char",
                "-fno-short-enums", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
                "-Isdk/include/os32", "-Iuserland/lib", "-Iuserland/lib/math",
                "-Iuserland/lib/ecs", "-Iuserland/lib/input",
                "-Iuserland/lib/save", "-Iuserland/lib/db",
                "-Iuserland/lib/asset", "-Iuserland/lib/mgx", "-Ilib/zlib",
                "-I" + str(CROSS_DIR / "i386-elf/include")]


# --------------------------------------------------------------------------
#  ビルド
# --------------------------------------------------------------------------

def build_host(tmp, name):
    """ハーネス + 取り込み用の翻訳単位を 1 本の実行ファイルにする。

    取り込み側 (run_*.c) は**実機 32 ビット向けのソースをホストの 64 ビットで
    コンパイルする**ので警告を止める (ポインタを u32 へ落とす箇所など)。
    実機と同じ -Werror は --target が i386-elf-gcc で見る。"""
    objs = []
    exe = tmp / name
    subprocess.run(["gcc", *HOST_FLAGS, "-Werror", *HOST_INC,
                    "-c", str(HARNESS), "-o", str(tmp / (name + "-h.o"))],
                   cwd=ROOT, check=True)
    objs.append(str(tmp / (name + "-h.o")))
    for rel in SHIMS:
        src = ROOT / rel
        obj = tmp / (name + "-" + src.stem + ".o")
        subprocess.run(["gcc", *HOST_FLAGS, "-w", *HOST_INC,
                        "-c", str(src), "-o", str(obj)], cwd=ROOT, check=True)
        objs.append(str(obj))
    subprocess.run(["gcc", *objs, "-o", str(exe)], cwd=ROOT, check=True)
    return exe


# --------------------------------------------------------------------------
#  静的検査 — ソースを読んで約束事に照らす
# --------------------------------------------------------------------------

def strip_c(text):
    """コメントと文字列の中身を空白に潰す。波括弧の対応を取るため。

    もとの長さと改行の位置は保つ (行番号を壊さない)。"""
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == '\\':
                    j += 1
                j += 1
            j = min(j + 1, n)
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        else:
            i += 1
    return "".join(out)


MAIN_RE = re.compile(
    r"^(?P<ret>void|int)\s+(?:__cdecl\s+)?main\s*\((?P<args>[^)]*)\)\s*$",
    re.M)


def main_span(text):
    """`main` の宣言と本体の範囲を返す。見つからなければ None。"""
    blank = strip_c(text)
    m = MAIN_RE.search(blank)
    if not m:
        # `int main(...) {` のように同じ行に `{` が来る書き方も拾う。
        m = re.search(r"^(?P<ret>void|int)\s+(?:__cdecl\s+)?main\s*"
                      r"\((?P<args>[^)]*)\)\s*\{", blank, re.M)
        if not m:
            return None
    start = blank.find("{", m.end() - 1)
    if start < 0:
        return None
    depth = 0
    i = start
    while i < len(blank):
        if blank[i] == '{':
            depth += 1
        elif blank[i] == '}':
            depth -= 1
            if depth == 0:
                return m.group("ret"), m.group("args"), start, i + 1
        i += 1
    return None


# 第 1 引数は KernelAPI、第 2 引数が名前 (票 §11 でヘッダが出し口まで持つように
# なり、呼び手は行のバッファを渡さなくなった)。
NAME_ARG = re.compile(r"os32_test_summary(?:_skip)?\s*\(\s*[^,]+,\s*"
                      r"(?P<name>[^,]+),")
ASSIGN = re.compile(r"(?P<var>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*"
                    r"os32_test_summary(?:_skip)?\s*\(")
CALL = re.compile(r"\bos32_test_summary(?:_skip)?\s*\(")
RETURN = re.compile(r"\breturn\s+(?P<what>[^;]*);")
RESERVED = (126, 127, 130, 139)

# ヘッダの内部。試験プログラムが直接触ると、行と終了コードを別々の呼び出しで
# 作れてしまい、rt/testresult.h の目的 (片方だけ直せないこと) が消える。
INTERNAL = re.compile(r"\bos32_test__[A-Za-z0-9_]*")


def scan_source(path, report):
    """試験 1 本を約束事に照らす。違反を report(msg) へ流す。"""
    stem = path.stem
    text = path.read_text(encoding="utf-8")
    span = main_span(text)
    if span is None:
        report("%s: main() の宣言が見つからない" % stem)
        return
    ret, args, body_start, body_end = span

    # (1) `void main` は約束違反 (票 §2-1)。crt0_c.c は int として呼ぶので、
    #     終了コードが eax の残骸になり再現性が無い。
    if ret != "int":
        report("%s: `%s main` — 終了コードが eax の残骸になる (票 §2-1)"
               % (stem, ret))

    # (2) 署名は int main(int, char **, KernelAPI *)。
    if "KernelAPI" not in args:
        report("%s: main が KernelAPI * を受け取っていない (票 §2-1): (%s)"
               % (stem, " ".join(args.split())))

    # (3) 約束事の管理元を取り込んでいる。
    if 'rt/testresult.h' not in text:
        report("%s: rt/testresult.h を取り込んでいない "
               "(集計行と終了コードを手書きしない)" % stem)
        return

    body = text[body_start:body_end]
    blank_body = strip_c(text)[body_start:body_end]

    # (4) 集計行の名前は固定文字列で、プログラム名と同じ。
    names = NAME_ARG.findall(text)
    for raw in names:
        arg = raw.strip()
        if not (arg.startswith('"') and arg.endswith('"')):
            report("%s: 集計行の名前が固定文字列でない (%s) — argv[0] 由来だと"
                   "リダイレクトや呼び方で行が変わる" % (stem, arg))
        elif arg.strip('"') != stem:
            report("%s: 集計行の名前が \"%s\" でプログラム名と違う"
                   % (stem, arg.strip('"')))

    # (4b) ヘッダの内部を直接呼んでいない (票 §11)。
    #      os32_test__build + 自前の出力 = 行と終了コードを別々に作る形で、
    #      これを許すとヘッダが出し口を持っている意味が無くなる。
    for m in INTERNAL.finditer(strip_c(text)):
        report("%s: ヘッダの内部 %s を直接呼んでいる — 行と終了コードは "
               "os32_test_summary / _skip の 1 回の呼び出しで作る (票 §11)"
               % (stem, m.group(0)))

    # (5) main の return は必ず集計の答えを返す。
    #     「FAIL を出しながら 0 を返す」「PASS を出しながら 1 を返す」を止める。
    #     受け方は 2 つ — 変数に受けて返すか、そのまま return するか。
    vars_ = set(m.group("var") for m in ASSIGN.finditer(body))
    if not CALL.search(blank_body):
        report("%s: main の中で os32_test_summary / _skip を呼んでいない" % stem)
    for m in RETURN.finditer(blank_body):
        what = body[m.start("what"):m.end("what")].strip()
        if what in vars_:
            continue
        if " ".join(what.split()).startswith(("os32_test_summary(",
                                              "os32_test_summary_skip(")):
            continue
        if what.isdigit() or (what.startswith("-") and what[1:].isdigit()):
            n = int(what)
            if n in RESERVED:
                report("%s: main が予約値 %d をそのまま返している (票 §2-1)"
                       % (stem, n))
            else:
                report("%s: main が `return %s;` — 集計行と食い違いうる "
                       "(答えは os32_test_summary から受ける)" % (stem, what))
        else:
            report("%s: main が `return %s;` — 集計の答えではない"
                   % (stem, what))

    # (6) 予約値を書いている経路が無い。
    for n in RESERVED:
        if re.search(r"\breturn\s+%d\s*;" % n, blank_body):
            report("%s: 予約値 %d を返す経路がある" % (stem, n))


def check_header():
    """rt/testresult.h の**出し口が 1 つだけ**であることを見る (票 §11)。

    コメントは潰してから見る — このヘッダは「なぜ kprintf ではないか」を
    本文で説明しているので、素の部分一致だと自分の説明で落ちる。"""
    bad = []
    text = CONV_HEADER.read_text(encoding="utf-8")
    code = strip_c(text)

    if header_int("OS32_TEST_FD_STDOUT") != 1:
        bad.append("rt/testresult.h: 集計行の出し先が fd 1 でない (票 §11)")
    if "sys_write(OS32_TEST_FD_STDOUT" not in " ".join(code.split()).replace(
            "sys_write (", "sys_write("):
        bad.append("rt/testresult.h: 集計行を sys_write(OS32_TEST_FD_STDOUT, …) "
                   "で出していない")
    if "kprintf" in code:
        bad.append("rt/testresult.h: kprintf を使っている — 画面へ直に書くので "
                   "リダイレクトを通らない (票 §11)")
    if "printf" in code.replace("kprintf", ""):
        bad.append("rt/testresult.h: printf を使っている — newlib が要るので "
                   "-nostdlib の試験で使えない (票 §11)")

    for msg in bad:
        print("  FAIL %s" % msg, flush=True)
    print("HEADER rt/testresult.h: %d violations" % len(bad), flush=True)
    return len(bad)


def conforming_sources():
    """検査の対象 — 第 1 陣 + rt/testresult.h を取り込んだ試験。"""
    out = []
    for name in WAVE1:
        p = TESTS_DIR / (name + ".c")
        if not p.exists():
            raise SystemExit("第 1 陣の %s が無い" % p)
        out.append(p)
    for p in sorted(TESTS_DIR.glob("*.c")):
        if p in out:
            continue
        if 'rt/testresult.h' in p.read_text(encoding="utf-8"):
            out.append(p)
    return out


def check_sources():
    bad = []
    srcs = conforming_sources()
    for p in srcs:
        scan_source(p, bad.append)
    for msg in bad:
        print("  FAIL %s" % msg, flush=True)
    print("STATIC %d sources, %d violations" % (len(srcs), len(bad)),
          flush=True)
    return len(bad)


# --------------------------------------------------------------------------
#  Rust 側 (alloc_demo) を C の管理元と突き合わせる
# --------------------------------------------------------------------------

def header_int(name):
    text = CONV_HEADER.read_text(encoding="utf-8")
    m = re.search(r"#define\s+%s\s+(-?\d+)" % re.escape(name), text)
    if not m:
        raise SystemExit("%s が rt/testresult.h に無い" % name)
    return int(m.group(1))


def check_rust():
    """alloc_demo は no_std Rust で C のヘッダを使えない。書式と終了コードが
    testresult.h と同じであることをここで押さえる (票 §5-1)。"""
    bad = []
    text = RUST_DEMO.read_text(encoding="utf-8")

    if '"{}: {} {}/{}\\n' not in text:
        bad.append("alloc_demo: 集計行の書式が `<名前>: <動詞> <n>/<m>` でない")
    for verb in ('"PASS"', '"FAIL"'):
        if verb not in text:
            bad.append("alloc_demo: %s を出さない" % verb)
    for const, want in (("EXIT_PASS", header_int("OS32_TEST_EXIT_PASS")),
                        ("EXIT_FAIL", header_int("OS32_TEST_EXIT_FAIL"))):
        m = re.search(r"const\s+%s\s*:\s*i32\s*=\s*(-?\d+)\s*;" % const, text)
        if not m:
            bad.append("alloc_demo: %s を定義していない" % const)
        elif int(m.group(1)) != want:
            bad.append("alloc_demo: %s が %s (rt/testresult.h は %d)"
                       % (const, m.group(1), want))
    if not re.search(r'const\s+TEST_NAME\s*:\s*&str\s*=\s*"alloc_demo"', text):
        bad.append("alloc_demo: 名前が固定文字列 \"alloc_demo\" でない")
    # 「無条件に合格と印字して 0 を返す」旧版に戻っていないこと。
    # 見るのは**印字する側の字面** (b"...") だけ。経緯を書いた冒頭のコメントに
    # 同じ語が出るので、素の部分一致だと自分のコメントで落ちる。
    if 'b"All tests passed!' in text:
        bad.append("alloc_demo: 無条件の \"All tests passed!\" を印字している")
    if "sum == 285" not in text:
        bad.append("alloc_demo: sum=285 を比較していない (コメントだけになっている)")
    if not re.search(r"\bcheck\(", text):
        bad.append("alloc_demo: 検査を 1 つもしていない")

    for msg in bad:
        print("  FAIL %s" % msg, flush=True)
    print("RUST alloc_demo: %d violations" % len(bad), flush=True)
    return len(bad)


# --------------------------------------------------------------------------
#  否定側
# --------------------------------------------------------------------------

# (相対パス, 目印, 置き換え) — どれも**コンパイルは通る**。
MUTATIONS = [
    # --- 票 §11 (出し口) の否定側。どれも**実行時に**落ちること -----------
    # 変異 1: 集計行を kprintf に戻す。画面には出るので人間は気づかないが、
    #         fd 1 に出ないのでランナーは「集計行が無い」と言う — 2026-09-17 に
    #         16 本中 14 本で起きたのがこれ。
    ("userland/lib/rt/testresult.h",
     "        n = api->sys_write(OS32_TEST_FD_STDOUT, line + done,\n"
     "                           (u32)(len - done));",
     "        api->kprintf(0, \"%s\", line + done);\n"
     "        n = (int)(len - done);"),
    # 変異 2: 短い書き込みを「書けた」ことにする。1 回の返り値をそのまま成功と
    #         読むと、集計行が途中で切れたまま 0 を返す。
    ("userland/lib/rt/testresult.h",
     "        if (n <= 0) return 0;\n"
     "        done += (unsigned int)n;",
     "        if (n <= 0) return 0;\n"
     "        done = len;"),
    # 変異 3: 行と終了コードを**別々の呼び出しで**作る。ヘッダの内部を呼んで
    #         自前で出すと、出し口がまた呼び手任せに戻る。
    ("userland/tests/stat_t.c",
     "    return os32_test_summary(api, \"stat_t\", g_passed, g_total);",
     "    {\n"
     "        char line[OS32_TEST_LINE_MAX];\n"
     "        int  code;\n"
     "\n"
     "        code = os32_test__build(line, sizeof(line), \"stat_t\",\n"
     "                                g_passed, g_total);\n"
     "        api->kprintf(ATTR_GREEN, \"%s\", line);\n"
     "        return code;\n"
     "    }"),
    # 変異 4: 出せなかったのに元の終了コードを返す (「$?=0 なのに集計行が
    #         無い」を作り直す)。
    ("userland/lib/rt/testresult.h",
     "    if (!os32_test__emit(api, line)) return OS32_TEST_EXIT_FAIL;",
     "    if (!os32_test__emit(api, line)) return code;"),

    # --- 票 §2 (終了コードと集計行の一致) の否定側 ------------------------
    # 変異 5: 予約値 (127 = 実行ファイルが見つからない) を返す。
    ("userland/tests/font_load_test.c",
     "        return os32_test_summary_skip(api, \"font_load_test\",\n"
     "                                      \"font file not found\");",
     "        os32_test_summary_skip(api, \"font_load_test\",\n"
     "                               \"font file not found\");\n"
     "        return 127;"),
    # 変異 6: `void main` に戻す (終了コードが eax の残骸になる)。
    ("userland/tests/asset_test.c",
     "int main(int argc, char **argv, KernelAPI *sys_api)",
     "void main(int argc, char **argv, KernelAPI *sys_api)"),
    # 変異 7: 集計行の名前を argv[0] 由来にする。
    ("userland/tests/restest.c",
     "    return os32_test_summary(api, \"restest\", g_passed, g_total);",
     "    return os32_test_summary(api, argv[0], g_passed, g_total);"),
    # 変異 8: 約束事の本体を壊す — 総数 0 を合格にする (集計変数が初期値の
    #         まま早期 return した試験が緑になる)。
    ("userland/lib/rt/testresult.h",
     "    ok = (total > 0) && (pass == total);",
     "    ok = (pass == total);"),
    # 変異 9: 約束事の本体を壊す — 終了コードだけ 0 に固定する
    #         (集計行は FAIL のまま = 2 つの答えを持つ)。
    ("userland/lib/rt/testresult.h",
     "    return ok ? OS32_TEST_EXIT_PASS : OS32_TEST_EXIT_FAIL;",
     "    return OS32_TEST_EXIT_PASS;"),
    # 変異 10: SKIP を不合格と同じ 1 にする (前提の欠如と不合格が混ざる)。
    ("userland/lib/rt/testresult.h",
     "    return OS32_TEST_EXIT_SKIP;\n}",
     "    return OS32_TEST_EXIT_FAIL;\n}"),
    # 変異 11: alloc_demo を「何も検査しない」旧版へ戻す。
    ("userland/rust/alloc_demo/src/lib.rs",
     "        check(b\"sum of squares 0..9 == 285\\0\", sum == 285);",
     "        /* sum は 285 のはず (比較しない) */"),
]


def run_once(tmp, tag):
    """今のソースで一巡する。返り値: (ビルドできたか, 落ちた本数)"""
    fails = 0
    try:
        exe = build_host(tmp, tag)
    except subprocess.CalledProcessError:
        return False, 0
    out = subprocess.run([str(exe)], cwd=ROOT, timeout=300,
                         capture_output=True)
    if out.returncode != 0:
        fails += out.stdout.decode("utf-8", "replace").count("FAIL ")
        fails = max(fails, 1)
    fails += check_sources_quiet()
    fails += check_rust_quiet()
    fails += check_header_quiet()
    return True, fails


def _quiet(fn):
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        n = fn()
    return n


def check_sources_quiet():
    return _quiet(check_sources)


def check_rust_quiet():
    return _quiet(check_rust)


def check_header_quiet():
    return _quiet(check_header)


SELF = "tools/tests/test_result_conv.py"


def dep_cmds(tmp):
    """build_host が流す gcc (-c) の並び。写しの木へ実体で複写する依存を
    gcc -MM で拾うのに使う (取り込み側 run_*.c は "../../../userland/…" を
    相対で引くので、依存を実体にしないと写しの外の実物を読む)。"""
    cmds = [["gcc", *HOST_FLAGS, "-Werror", *HOST_INC,
             "-c", str(HARNESS), "-o", str(tmp / "h.o")]]
    for rel in SHIMS:
        cmds.append(["gcc", *HOST_FLAGS, "-w", *HOST_INC,
                     "-c", str(ROOT / rel), "-o", str(tmp / "s.o")])
    return cmds


def one_mutation(item):
    """変異 1 本: 写しの木を壊し、写しの中のこの試験で一巡する (--mut-once)。
    実物は読むだけ。(印字, 見逃し) を返す。"""
    i, (relpath, old, new) = item
    original = (ROOT / relpath).read_text(encoding="utf-8")
    label = "%d %s" % (i, pathlib.Path(relpath).name)
    if old not in original:
        return "MUTATE %-28s SKIP (目印が見つからない)" % label, 1
    with tempfile.TemporaryDirectory(prefix="os32-result-conv-mut-") as td:
        td = pathlib.Path(td)
        real = set()
        for c in dep_cmds(td):
            real |= mutpar.gcc_deps(c, ROOT)
        p = mutpar.run_script_in_tree(
            ROOT, td, {relpath: original.replace(old, new, 1)}, SELF,
            ["--mut-once", "mut%d" % i], real=real,
            capture_output=True, text=True, timeout=600)
    m = re.search(r"^MUT-ONCE built=(\d) fails=(\d+)$", p.stdout, re.M)
    if not m:
        return ("MUTATE %-28s **写しの中の一巡が結果を返さない (rc=%d)**"
                % (label, p.returncode), 1)
    built, fails = m.group(1) == "1", int(m.group(2))
    if not built:
        return ("MUTATE %-28s **コンパイルが通らない = 目が働いていない**"
                % label, 1)
    if fails == 0:
        return ("MUTATE %-28s **GREEN のまま = 試験が規則を見ていない**"
                % label, 1)
    return "MUTATE %-28s RED (期待どおり落ちた: %d 件)" % (label, fails), 0


def run_mutations(tmp):
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    check-par で回せる)。"""
    return mutpar.run_with_control(
        one_mutation, list(enumerate(MUTATIONS, 1)),
        (0, ("userland/lib/rt/testresult.h", "", "")))


# --------------------------------------------------------------------------

def run_target(tmp):
    """実機と同じ i386-elf クロスコンパイラで -Werror を通す ([C1])。"""
    for src in conforming_sources():
        rel = src.relative_to(ROOT)
        subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", str(rel),
                        "-o", str(tmp / (src.stem + ".o"))],
                       cwd=ROOT, check=True)
    print("TARGET i386-elf -Werror COMPILE PASS (%d sources)"
          % len(conforming_sources()), flush=True)
    for src in ("userland/tests/ring3_hello.c", "userland/tests/ring3_fault.c",
                "userland/tests/ring3_guard.c"):
        subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", src,
                        "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf -Werror COMPILE PASS (%s)" % src, flush=True)


if __name__ == "__main__":
    if "--mut-once" in sys.argv:
        # 写しの木の中で一巡する (run_mutations の子)。
        with tempfile.TemporaryDirectory(prefix="os32-result-conv-") as tmp:
            built, fails = run_once(pathlib.Path(tmp),
                                    sys.argv[sys.argv.index("--mut-once") + 1])
        print("MUT-ONCE built=%d fails=%d" % (built, fails), flush=True)
        sys.exit(0)
    with tempfile.TemporaryDirectory(prefix="os32-result-conv-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        exe = build_host(tmp, "result-conv")
        print("HOST GNU11 -Werror COMPILE PASS "
              "(real rt/testresult.h + 5 real test programs)", flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=300).returncode
        print("EXIT test_result_conv_host=%d" % rc, flush=True)
        failed += rc != 0

        failed += 1 if check_sources() else 0
        failed += 1 if check_rust() else 0
        failed += 1 if check_header() else 0

        if "--target" in sys.argv:
            run_target(tmp)

        if "--mutate" in sys.argv:
            failed += run_mutations(tmp)

        sys.exit(1 if failed else 0)
