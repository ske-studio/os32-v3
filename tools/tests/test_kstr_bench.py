"""kstr_bench の**計測の枠組み**をホストで試験する (票 TASK_KSTRING_BENCH)。

票:   docs/archive/portability/TASK_KSTRING_BENCH.md
記録: tools/tests/kstr_bench_tdd.md

tools/tests/kstr_bench_host.c が実物の userland/tests/kstr_bench.c を 1 行も
写さずそのまま #include し、KernelAPI (get_tick / sys_write / sys_yield) と
測られる側の 13 本だけを差し替えて回す。見るのは**数字そのものではなく、
数字の作り方**:

  1. 出力の書式が固定どおりか (後で集計する側が壊れない)
  2. 繰り返し回数の決め方 — 1 ケース 1MB 以上から始め、30 ティック未満なら
     倍にする。倍は 5 回まで (時計が止まっていても終わる)
  3. 食い違いを注入したら MISMATCH が出て**その関数の計測が飛ぶ**か
  4. 13 本すべてが表に載っているか (lib/kstring_asm.asm の global /
     kstr_bench.c の表 / build/programs.mk の改名表 / 贋物の名前表の 4 者)
  5. [V2] build/app.conf と userland/deploy.yaml に登録されているか

最後に **実物の lib/kstring_asm.asm と lib/kstring_c.c を同居させた版**
(-DKSTRB_REAL) も回す。ゲストと同じ同居のしかたで 13 本が一致すること
(受入 K1 のホスト側) と、出力が書式どおりのことを見る。**時間の数字は
ホストでは意味を持たない** — get_tick は倍化が起きない固定歩幅の贋物。

  python3 -B tools/tests/test_kstr_bench.py [--target] [--mutate]

--target はカーネルと同じ i386-elf クロスコンパイラで kstr_bench.c が
-Werror で通ること、および**外部プログラムのフラグで組んだ lib/kstring_c.c に
未定義参照が 1 本も無い**ことを見る (GCC がループを memcpy / memset の
呼び出しに畳むと、改名の後に自分自身への無限再帰になる)。
--mutate は否定側 (計測の枠組みをわざと壊した版が RED になることを見る)。

make・エミュレータ・実配備には一切触れない。
"""
import host32
import contextlib
import io
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]

PROG_SRC = ROOT / "userland/tests/kstr_bench.c"
HOST_SRC = ROOT / "tools/tests/kstr_bench_host.c"
ASM_SRC = ROOT / "lib/kstring_asm.asm"
C_SRC = ROOT / "lib/kstring_c.c"
MK = ROOT / "build/programs.mk"
APP_CONF = ROOT / "build/app.conf"
DEPLOY = ROOT / "userland/deploy.yaml"

# kstr_bench.c の定数と同じ値。ここが**期待値の管理元**で、ずれたら
# check_constants が止める (票 §2 の「1MB 以上 / 30 ティック未満なら倍」)。
LENS = [4, 16, 64, 256, 1024, 16384, 262144]
ALIGNS = [0, 1]
TARGET_BYTES = 1048576
MIN_TICKS = 30
MAX_DOUBLE = 5
REAL_TICK_STEP = 40

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt

# build/config.mk の CFLAGS_COMMON と同じ素性 (ILP32 必須: u32 = unsigned long)。
# -fno-builtin / -fno-tree-loop-distribute-patterns は **-nostdlib だから**要る
# — GCC がバイトループを memcpy / memset の呼び出しに畳むと未解決になる。
HOST_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
              "-fno-pie", "-fno-stack-protector", "-fcommon",
              "-fsigned-char", "-fno-short-enums",
              "-O2", "-fno-builtin", "-fno-tree-loop-distribute-patterns",
              "-Wall", "-Wextra", "-Werror"]
HOST_LINK = ["-nostdlib", "-static", "-no-pie", "-Wl,-z,noexecstack"]
HOST_INC = ["-I" + str(ROOT / p)
            for p in (".", "include", "sdk/include", "sdk/include/os32")]

# 実物の lib/kstring_c.c をホストで組むときのフラグ (tools/tests/test_kstring_c.py
# と同じ)。kstring.h を引くので -Ilib が要る。
KSTR_C_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-fcommon",
                "-fsigned-char", "-fno-short-enums", "-mno-red-zone", "-O2",
                "-Wall", "-Wextra", "-Werror"]
KSTR_C_INC = ["-I" + str(ROOT / p) for p in ("include", "lib", "sdk/include/os32")]

# 外部プログラム (build/config.mk の PROGRAM_FLAGS と同じ形)
TARGET_USER = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
               "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char",
               "-fno-short-enums", "-O2",
               "-Wall", "-Wextra", "-Werror",
               "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
               "-Isdk/include/os32", "-Iuserland/lib",
               "-I" + str(CROSS_DIR / "i386-elf/include")]

DATA_RE = re.compile(
    r"^KSTR (?P<fn>[a-z0-9_]+) (?P<var>asm|c) (?P<len>\d+) (?P<al>[01])"
    r" (?P<ticks>\d+) (?P<reps>\d+)$")
MISS_RE = re.compile(r"^KSTR MISMATCH (?P<fn>[a-z0-9_]+) (?P<len>\d+) (?P<al>[01])$")


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=str(ROOT), check=True, **kw)


# ---------------------------------------------------------------------------
#  静的: 名前の 4 者一致と登録 ([V2])
# ---------------------------------------------------------------------------
def asm_globals():
    return re.findall(r"^global\s+(\w+)\s*$", ASM_SRC.read_text(encoding="utf-8"),
                      re.M)


def table_names():
    return re.findall(r'kb_add\("(\w+)"', PROG_SRC.read_text(encoding="utf-8"))


def mk_names():
    text = MK.read_text(encoding="utf-8")
    m = re.search(r"^KSTR_BENCH_FUNCS\s*=\s*((?:.*\\\n)*.*)$", text, re.M)
    if not m:
        return []
    return m.group(1).replace("\\\n", " ").split()


def fake_names():
    text = HOST_SRC.read_text(encoding="utf-8")
    m = re.search(r"static const char \*fk_names\[FK_COUNT\] = \{(.*?)\};",
                  text, re.S)
    if not m:
        return []
    return re.findall(r'"(\w+)"', m.group(1))


def check_name_sets():
    """13 本の取りこぼしを 4 者の突き合わせで止める。"""
    src = asm_globals()
    sets = {
        "lib/kstring_asm.asm の global": src,
        "kstr_bench.c の kb_add": table_names(),
        "build/programs.mk の KSTR_BENCH_FUNCS": mk_names(),
        "kstr_bench_host.c の fk_names": fake_names(),
    }
    bad = 0
    if len(src) != 13:
        print("NAMES **lib/kstring_asm.asm の global が %d 本 (13 本のはず)**"
              % len(src), flush=True)
        bad += 1
    for label, names in sets.items():
        if sorted(names) != sorted(src):
            missing = sorted(set(src) - set(names))
            extra = sorted(set(names) - set(src))
            print("NAMES **%s がずれている 足りない=%s 余分=%s**"
                  % (label, " ".join(missing) or "-", " ".join(extra) or "-"),
                  flush=True)
            bad += 1
    if not bad:
        print("NAMES 4 者一致 (%d 本)" % len(src), flush=True)
    return bad


def check_constants():
    """試験が期待値に使う定数が、実物の #define と同じであること。"""
    text = PROG_SRC.read_text(encoding="utf-8")
    want = {
        "KB_TARGET_BYTES": TARGET_BYTES,
        "KB_MIN_TICKS": MIN_TICKS,
        "KB_MAX_DOUBLE": MAX_DOUBLE,
    }
    bad = 0
    for name, value in want.items():
        m = re.search(r"^#define\s+%s\s+(\d+)" % name, text, re.M)
        if not m or int(m.group(1)) != value:
            print("CONST **%s が %s (試験は %d を期待)**"
                  % (name, m.group(1) if m else "見つからない", value), flush=True)
            bad += 1
    m = re.search(r"static const u32 kb_lens\[KB_LEN_COUNT\] = \{(.*?)\};",
                  text, re.S)
    lens = [int(x) for x in re.findall(r"(\d+)UL", m.group(1))] if m else []
    if lens != LENS:
        print("CONST **kb_lens が %s (試験は %s を期待)**" % (lens, LENS), flush=True)
        bad += 1
    if not bad:
        print("CONST 1MB / 30 ティック / 倍は 5 回まで / 長さ 7 通り 一致", flush=True)
    return bad


def check_registration():
    """[V2] 起動できるバイナリはその層の deploy.yaml と app.conf に載せる。"""
    bad = 0
    conf = APP_CONF.read_text(encoding="utf-8")
    m = re.search(r"^userland/tests/kstr_bench\s+(\d+)\s+(\d+)", conf, re.M)
    if not m:
        print("REG **build/app.conf に userland/tests/kstr_bench が無い**", flush=True)
        bad += 1
    else:
        kapi = ROOT / "sdk/include/os32/os32_kapi_shared.h"
        cur = re.search(r"^#define KAPI_VERSION\s+(\d+)",
                        kapi.read_text(encoding="utf-8"), re.M)
        # app.conf stores min_api_ver, not an exact generation requirement
        # (KAPI_SPEC T2c). The loader refuses a newer required feature version.
        if cur is None or not 0 < int(m.group(1)) <= int(cur.group(1)):
            print("REG **app.conf の要求最低 API 版 %s が現行 %s と互換でない**"
                  % (m.group(1), cur.group(1) if cur else "不明"), flush=True)
            bad += 1
    if "userland/tests/kstr_bench.bin" not in DEPLOY.read_text(encoding="utf-8"):
        print("REG **userland/deploy.yaml に kstr_bench.bin が無い ([V2])**",
              flush=True)
        bad += 1
    if not bad:
        print("REG app.conf / deploy.yaml 登録あり", flush=True)
    return bad


def check_registration_versions(tmp):
    """Exercise real registration parsing with older/current/future/zero minima."""
    global APP_CONF
    original = APP_CONF
    conf = original.read_text(encoding="utf-8")
    current = json.loads((ROOT / "sdk/kapi.json").read_text())["version"]
    probe = tmp / "app.conf"
    bad = 0
    try:
        APP_CONF = probe
        for required, expected in ((current - 1, 0), (current, 0),
                                   (current + 1, 1), (0, 1)):
            changed, count = re.subn(
                r"^(userland/tests/kstr_bench\s+)\d+", r"\g<1>" + str(required),
                conf, flags=re.M)
            assert count == 1
            probe.write_text(changed, encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()) as output:
                result = check_registration()
            if result != expected:
                print("REG minimum %d: expected %d, got %d\n%s" %
                      (required, expected, result, output.getvalue()), flush=True)
                bad += 1
    finally:
        APP_CONF = original
    if not bad:
        print("REG older/current minima accepted; future/zero rejected PASS", flush=True)
    return bad


# ---------------------------------------------------------------------------
#  ビルドと実行
# ---------------------------------------------------------------------------
def build_fake(tmp, name, tick_per_call=1, mismatch=None, short_write=False):
    exe = tmp / name
    cmd = ["gcc"] + HOST_FLAGS + HOST_INC + ["-DKSTRB_FAKE",
           "-DKSTRB_TICK_PER_CALL=%dUL" % tick_per_call]
    if mismatch:
        cmd.append('-DKSTRB_MISMATCH_NAME="%s"' % mismatch)
    if short_write:
        cmd.append("-DKSTRB_SHORT_WRITE=1")
    cmd += HOST_LINK + [str(HOST_SRC), "-o", str(exe)]
    run(cmd)
    return exe


def build_real(tmp):
    """実物の .asm と .c を a_* / c_* に改名して同じ実行ファイルに入れる。"""
    asm_o, asm_r = tmp / "ks-asm.o", tmp / "ks-asm-r.o"
    c_o, c_r = tmp / "ks-c.o", tmp / "ks-c-r.o"
    exe = tmp / "kstr-bench-real"
    funcs = asm_globals()

    run(["nasm", "-f", "elf32", str(ASM_SRC), "-o", str(asm_o)])
    (tmp / "ren_a.txt").write_text(
        "".join("%s a_%s\n" % (f, f) for f in funcs), encoding="ascii")
    run(["objcopy", "--redefine-syms=" + str(tmp / "ren_a.txt"),
         str(asm_o), str(asm_r)])

    run(["gcc"] + KSTR_C_FLAGS + KSTR_C_INC + ["-c", str(C_SRC), "-o", str(c_o)])
    (tmp / "ren_c.txt").write_text(
        "".join("%s c_%s\n" % (f, f) for f in funcs), encoding="ascii")
    run(["objcopy", "--redefine-syms=" + str(tmp / "ren_c.txt"),
         str(c_o), str(c_r)])

    run(["gcc"] + HOST_FLAGS + HOST_INC + ["-DKSTRB_REAL"] + HOST_LINK +
        [str(HOST_SRC), str(asm_r), str(c_r), "-o", str(exe)])
    return exe


def execute(exe):
    p = host32.run([str(exe)], cwd=str(ROOT), timeout=900,
                       capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


WITNESS_RE = re.compile(r"^WITNESS (?P<fn>[a-z0-9_]+) (?P<mask>\d+) (?P<odd>\d+)$")
FULL_MASK = (1 << len(LENS)) - 1


def check_witness(label, err):
    """贋物が**実際に渡された長さ**を目撃していたか (7 通りちょうど)。

    kstr_bench.c が入力を組み損ねても両版は同じ壊れた入力を見るので
    MISMATCH にはならない。表だけが静かに嘘になるので、ここが唯一の受け手。
    """
    seen = {}
    for line in err.splitlines():
        m = WITNESS_RE.match(line)
        if m:
            seen[m.group("fn")] = (int(m.group("mask")), int(m.group("odd")))
    bad = []
    for fn in asm_globals():
        if fn not in seen:
            bad.append("%s の目撃記録が無い" % fn)
            continue
        mask, odd = seen[fn]
        if mask != FULL_MASK:
            missing = [LENS[i] for i in range(len(LENS)) if not (mask & (1 << i))]
            bad.append("%s に渡らなかった長さ: %s" % (fn, missing))
        if odd:
            bad.append("%s に表に無い長さが %d 通り渡った" % (fn, odd))
    if bad:
        for b in bad[:4]:
            print("WITNESS %-14s **%s**" % (label, b), flush=True)
        return 1
    return 0


# ---------------------------------------------------------------------------
#  期待値
# ---------------------------------------------------------------------------
def expected_reps(length, tick_per_call):
    """1 ケース 1MB 以上 → 30 ティック未満なら倍 (5 回まで)。"""
    reps = TARGET_BYTES // length
    if reps == 0:
        reps = 1
    doubled = 0
    while reps * tick_per_call < MIN_TICKS and doubled < MAX_DOUBLE:
        reps *= 2
        doubled += 1
    return reps


def parse(out):
    """(データ行の表, MISMATCH の表, 問題のある行)"""
    data, miss, junk = {}, [], []
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if line == "KSTR DONE":
            if i != len(lines) - 1:
                junk.append("DONE が最後の行ではない: %d/%d" % (i + 1, len(lines)))
            continue
        m = MISS_RE.match(line)
        if m:
            miss.append((m.group("fn"), int(m.group("len")), int(m.group("al"))))
            continue
        m = DATA_RE.match(line)
        if not m:
            junk.append(line)
            continue
        key = (m.group("fn"), m.group("var"), int(m.group("len")), int(m.group("al")))
        if key in data:
            junk.append("同じケースが 2 回: %s" % (key,))
        data[key] = (int(m.group("ticks")), int(m.group("reps")))
    if not lines or lines[-1] != "KSTR DONE":
        junk.append("KSTR DONE で終わっていない")
    return data, miss, junk


def check_shape(label, out, funcs, tick_per_call=None, expect_reps=True):
    """書式・件数・13 本・回数の決め方をまとめて見る。戻り値 = 失敗の数。"""
    data, miss, junk = parse(out)
    bad = []
    for j in junk:
        bad.append("書式が違う行: %r" % j)
    if miss:
        bad.append("MISMATCH が出た: %s" % (miss[:3],))

    want = set()
    for fn in funcs:
        for ln in LENS:
            for al in ALIGNS:
                for var in ("asm", "c"):
                    want.add((fn, var, ln, al))
    if set(data) != want:
        missing = sorted(want - set(data))[:4]
        extra = sorted(set(data) - want)[:4]
        bad.append("ケースの集合がずれた 足りない=%s 余分=%s" % (missing, extra))

    seen = sorted(set(k[0] for k in data))
    if len(seen) != len(funcs):
        bad.append("表に載った関数が %d 本 (%d 本のはず)" % (len(seen), len(funcs)))

    if expect_reps and tick_per_call is not None:
        for (fn, var, ln, al), (ticks, reps) in sorted(data.items()):
            want_reps = expected_reps(ln, tick_per_call)
            if reps != want_reps:
                bad.append("%s/%s len=%d ずれ=%d 回数 %d (期待 %d)"
                           % (fn, var, ln, al, reps, want_reps))
                break
            if ticks != reps * tick_per_call:
                bad.append("%s/%s len=%d ずれ=%d ティック %d (期待 %d)"
                           % (fn, var, ln, al, ticks, reps * tick_per_call))
                break

    if bad:
        for b in bad[:6]:
            print("CASE %-18s **%s**" % (label, b), flush=True)
        return 1
    print("CASE %-18s PASS (%d 行 / %d 本)" % (label, len(data), len(seen)),
          flush=True)
    return 0


# ---------------------------------------------------------------------------
#  台本 (それぞれ「何を見るか」を 1 つ持つ)
# ---------------------------------------------------------------------------
def sc_format(tmp, tag=""):
    """書式・件数・13 本 + 倍化が起きる側 (長い長さで 3 回倍になる)。"""
    exe = build_fake(tmp, "fmt" + tag, tick_per_call=1)
    rc, out, err = execute(exe)
    bad = check_shape("format/doubling", out, asm_globals(), tick_per_call=1)
    bad += check_witness("format/doubling", err)
    if rc != 0:
        print("CASE format/doubling **終了コード %d (0 のはず)**" % rc, flush=True)
        bad += 1
    return bad


def sc_double_once(tmp, tag=""):
    """1 回だけ倍になる側 (256K は 4→8、短い長さは倍にならない)。"""
    exe = build_fake(tmp, "dbl" + tag, tick_per_call=4)
    rc, out, err = execute(exe)
    bad = check_shape("doubling-once", out, asm_globals(), tick_per_call=4)
    if rc != 0:
        print("CASE doubling-once **終了コード %d**" % rc, flush=True)
        bad += 1
    return bad


def sc_cap(tmp, tag=""):
    """時計が止まっていても終わる — 倍は 5 回まで (回数は 32 倍で頭打ち)。"""
    exe = build_fake(tmp, "cap" + tag, tick_per_call=0)
    rc, out, err = execute(exe)
    bad = check_shape("tick-frozen-cap", out, asm_globals(), tick_per_call=0)
    if rc != 0:
        print("CASE tick-frozen-cap **終了コード %d**" % rc, flush=True)
        bad += 1
    return bad


def sc_mismatch(tmp, tag=""):
    """食い違いを注入 → MISMATCH が出て**その関数の計測が飛ぶ**。

    2 通り注入する: 戻り値だけ違う版 (kstrlen) と、戻り値は同じでバッファの
    中身が違う版 (kmemcpy)。後者はバッファ全体を突き合わせないと見つからない。
    """
    bad = 0
    for victim in ("kstrlen", "kmemcpy"):
        exe = build_fake(tmp, "mis-%s%s" % (victim, tag), tick_per_call=1,
                         mismatch=victim)
        rc, out, err = execute(exe)
        data, miss, junk = parse(out)
        label = "mismatch/" + victim
        prob = []
        for j in junk:
            prob.append("書式が違う行: %r" % j)
        want_miss = set((victim, ln, al) for ln in LENS for al in ALIGNS)
        if set(miss) != want_miss:
            prob.append("MISMATCH の集合がずれた: %d 件 (%d 件のはず)"
                        % (len(set(miss)), len(want_miss)))
        if any(k[0] == victim for k in data):
            prob.append("**壊した %s の計測行が出ている (飛ばしていない)**" % victim)
        rest = [f for f in asm_globals() if f != victim]
        if sorted(set(k[0] for k in data)) != sorted(rest):
            prob.append("残り %d 本の計測が揃っていない" % len(rest))
        if rc != 1:
            prob.append("終了コード %d (食い違いありは 1 のはず)" % rc)
        if prob:
            for p in prob[:4]:
                print("CASE %-18s **%s**" % (label, p), flush=True)
            bad += 1
        else:
            print("CASE %-18s PASS (%d 件の MISMATCH、%d 本を計測)"
                  % (label, len(miss), len(rest)), flush=True)
    return bad


def sc_short_write(tmp, tag=""):
    """sys_write が 1 バイトずつしか受けなくても 1 バイトも落とさない。"""
    ref = build_fake(tmp, "sw-ref" + tag, tick_per_call=1)
    rc_ref, out_ref, _ = execute(ref)
    exe = build_fake(tmp, "sw" + tag, tick_per_call=1, short_write=True)
    rc, out, err = execute(exe)
    if rc != rc_ref or out != out_ref:
        print("CASE short-write **1 バイト書きで出力が変わった "
              "(rc %d/%d, %d/%d バイト)**"
              % (rc, rc_ref, len(out), len(out_ref)), flush=True)
        return 1
    print("CASE short-write        PASS (%d バイト一致)" % len(out), flush=True)
    return 0


def sc_real(tmp, tag=""):
    """実物の .asm と .c を同居させて回す (受入 K1 のホスト側)。"""
    exe = build_real(tmp)
    rc, out, err = execute(exe)
    data, miss, junk = parse(out)
    bad = check_shape("real-sources", out, asm_globals(), expect_reps=False)
    for (fn, var, ln, al), (ticks, reps) in data.items():
        if ticks != REAL_TICK_STEP or reps != expected_reps(ln, REAL_TICK_STEP):
            print("CASE real-sources **%s/%s len=%d: 固定歩幅のはずが "
                  "ティック %d 回数 %d**" % (fn, var, ln, ticks, reps), flush=True)
            bad += 1
            break
    if rc != 0:
        print("CASE real-sources **終了コード %d — 実物の 2 版が食い違った**"
              % rc, flush=True)
        bad += 1
    if not bad:
        print("REAL 実物の asm 版 / C 版が 13 本 × %d 長 × %d ずれ で一致"
              % (len(LENS), len(ALIGNS)), flush=True)
    return bad


SCENARIOS = {
    "format": sc_format,
    "double_once": sc_double_once,
    "cap": sc_cap,
    "mismatch": sc_mismatch,
    "short_write": sc_short_write,
    "real": sc_real,
}


# ---------------------------------------------------------------------------
#  --target: 実機と同じクロスコンパイラ
# ---------------------------------------------------------------------------
def target_compile(tmp):
    bad = 0
    cc = shutil.which("i386-elf-gcc")
    if cc is None:
        print("TARGET i386-elf SKIP (i386-elf-gcc が無い)", flush=True)
        return 1

    run([cc] + TARGET_USER + ["-c", "userland/tests/kstr_bench.c",
                              "-o", str(tmp / "kstr_bench.o")])
    print("TARGET i386-elf GNU11 -Werror COMPILE PASS (kstr_bench.c)", flush=True)

    # 外部プログラムのフラグで組んだ lib/kstring_c.c に**未定義参照が無い**こと。
    # GCC がループを memcpy / memset の呼び出しに畳むと、a_/c_ へ改名した後に
    # 自分自身への無限再帰になる (ゲストで固まる)。
    obj = tmp / "kstring_c-user.o"
    run([cc] + TARGET_USER + ["-Ilib", "-c", "lib/kstring_c.c", "-o", str(obj)])
    nm = shutil.which("i386-elf-nm") or "nm"
    out = subprocess.run([nm, str(obj)], cwd=str(ROOT), check=True,
                         capture_output=True, text=True).stdout
    undef = sorted(p.split()[1] for p in out.splitlines()
                   if p.split() and p.split()[0] == "U")
    defined = set(p.split()[2] for p in out.splitlines()
                  if len(p.split()) == 3 and p.split()[1] in "TtDdBbRrWw")
    missing = [f for f in asm_globals() if f not in defined]
    if undef:
        print("TARGET **lib/kstring_c.c (app flags) に未定義参照 = 自己再帰の危険: "
              "%s**" % " ".join(undef), flush=True)
        bad += 1
    elif missing:
        print("TARGET **lib/kstring_c.c (app flags) に %s が無い**"
              % " ".join(missing), flush=True)
        bad += 1
    else:
        print("TARGET lib/kstring_c.c (app flags) 13 本定義 / 未定義参照 0",
              flush=True)
    return bad


# ---------------------------------------------------------------------------
#  変異 (否定側)。実物の kstr_bench.c をわざと壊し、**指名した台本が落ちる**
#  ことを見る。台本を指名するのは、全台本を回すと時間が伸びるから。
# ---------------------------------------------------------------------------
MUTATIONS = [
    # 倍にしない版。時計が止まっていても回数が増えない。
    ("no_double", "cap",
     "#define KB_MAX_DOUBLE      5",
     "#define KB_MAX_DOUBLE      0"),
    # 1 ケースの目標バイト数を減らした版 (分解能が落ちる)。
    ("small_target", "double_once",
     "#define KB_TARGET_BYTES 1048576UL",
     "#define KB_TARGET_BYTES   16384UL"),
    # 食い違った関数も測ってしまう版 (違うものを比べた数字を表に載せる)。
    ("no_skip", "mismatch",
     "        if (kb_fns[fi].skip) continue;",
     "        if (0) continue;"),
    # 表の枠が足りず 13 本目 (memcmp) が黙って落ちる版 (取りこぼし)。
    ("table_too_small", "format",
     "#define KB_FN_MAX        16",
     "#define KB_FN_MAX        12"),
    # ティックと回数の欄を入れ替えた版 (集計側が「1 回あたり」を取り違える)。
    ("swap_ticks_reps", "double_once",
     "    p += kb_u32(line + p, ticks);\n"
     "    line[p++] = ' ';\n"
     "    p += kb_u32(line + p, reps);",
     "    p += kb_u32(line + p, reps);\n"
     "    line[p++] = ' ';\n"
     "    p += kb_u32(line + p, ticks);"),
    # short write を「全部書けた」ことにする版 (行が欠ける)。
    ("short_write_lost", "short_write",
     "        done += (u32)rc;",
     "        done += n - done;"),
    # 宛先バッファの突き合わせを 0 バイトにした版 (戻り値しか見ない)。
    ("ret_only", "mismatch",
     "kb_diff8(kb_da, kb_dc, KB_BUF)",
     "kb_diff8(kb_da, kb_dc, 0UL)"),
    # 前に植えた NUL を戻さない版。短いケースの終端が kb_src に残り、
    # 長いケースの文字列が 4 バイトに化ける。両版とも同じ壊れた入力を見るので
    # MISMATCH にはならない — 目撃記録だけが受け手 (この変異が唯一の門番)。
    ("stale_nul", "format",
     "    if (kb_nul_valid) kb_src[kb_nul_pos] = kb_nul_saved;",
     "    if (0) kb_src[kb_nul_pos] = kb_nul_saved;"),
]


SELF = "tools/tests/test_kstr_bench.py"
MUT_TARGET = "userland/tests/kstr_bench.c"
RC_COMPILE = 3      # --mut-scenario: コンパイルが通らなかった


def one_mutation(item):
    """変異 1 本: 写しの木の kstr_bench.c を壊し、写しの中のこの試験で指名した
    台本だけを回す (--mut-scenario)。実物は読むだけ。(印字, 見逃し) を返す。"""
    i, (name, scenario, old, new) = item
    original = (ROOT / MUT_TARGET).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-18s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-kstr-bench-mut-") as td:
        p = mutpar.run_script_in_tree(
            ROOT, td, {MUT_TARGET: original.replace(old, new, 1)}, SELF,
            ["--mut-scenario", scenario, "-m%d" % i],
            real={str(HOST_SRC.relative_to(ROOT))},
            capture_output=True, text=True, timeout=900)
    text = p.stdout
    if p.returncode == RC_COMPILE:
        return (text + "MUTATE %-18s RED (コンパイルが通らない) [%s]"
                % (name, scenario), 0)
    if p.returncode != 0:
        return (text + "MUTATE %-18s RED (期待どおり落ちた) [%s]"
                % (name, scenario), 0)
    return (text + "MUTATE %-18s **GREEN のまま = 試験が見ていない** [%s]"
            % (name, scenario), 1)


def run_mutations(tmp):
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    check-par で回せる)。対照は変異なしの写しで format の台本を回す。"""
    return mutpar.run_with_control(
        one_mutation, list(enumerate(MUTATIONS)),
        (len(MUTATIONS), ("control", "format", "", "")))


def mut_scenario(scenario, tag):
    """--mut-scenario の中身 (写しの木の中で呼ばれる)。落ちたら 1、
    コンパイルが通らなければ RC_COMPILE。"""
    with tempfile.TemporaryDirectory(prefix="os32-kstr-bench-") as tmp:
        try:
            failed = SCENARIOS[scenario](pathlib.Path(tmp), tag=tag)
        except subprocess.CalledProcessError:
            return RC_COMPILE
    return 1 if failed else 0


if __name__ == "__main__":
    if "--mut-scenario" in sys.argv:
        k = sys.argv.index("--mut-scenario")
        sys.exit(mut_scenario(sys.argv[k + 1], sys.argv[k + 2]))
    with tempfile.TemporaryDirectory(prefix="os32-kstr-bench-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        failed += check_name_sets()
        failed += check_constants()
        failed += check_registration()
        failed += check_registration_versions(tmp)

        for key in ("format", "double_once", "cap", "mismatch",
                    "short_write", "real"):
            failed += SCENARIOS[key](tmp)

        if "--target" in sys.argv:
            failed += target_compile(tmp)
        if "--mutate" in sys.argv:
            failed += run_mutations(tmp)

        print("RESULT %s (failures=%d)" % ("FAIL" if failed else "PASS", failed),
              flush=True)
        sys.exit(1 if failed else 0)
