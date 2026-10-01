"""KAPI の出力ポインタ宣言 `out` とその生成コードのホスト試験。

記録: tools/tests/kapi_out_tdd.md
票  : docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md 受入 G1

見るものは 2 つ。

  (a) **書き忘れが落ちること** — 非 const のポインタ引数があるのに `out` が
      無いエントリを 1 つ作ると `sdk/gen_kapi.py` が非ゼロで終わる。これが
      この仕組みの要で、落ちなければ次に KAPI を足した人が黙って穴を開ける。
  (b) **生成されるコードの意味** — `len` 引数 / 固定 `size` / `unit` の解釈と、
      **全範囲を検査してから target を呼ぶ**順序。出力が 2 本あるときに
      1 本目だけ書かれる、という壊れ方をしないこと。

生成器は書き出し先を**相対パス**で持つので、合成した kapi.json は
**一時ディレクトリを cwd にして**回す (実物の生成物には触らない)。

  python3 -B tools/tests/test_kapi_out.py           # 全ケース
  python3 -B tools/tests/test_kapi_out.py --mutate  # 否定側 (生成器を壊す)
"""
import copy
import json
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "sdk/gen_kapi.py"
KAPI_JSON = ROOT / "sdk/kapi.json"

# 合成した最小の kapi.json。実物の並びに依存せず解釈だけを見る。
SYNTH = {
    "version": 1,
    "generations": json.loads((ROOT / "sdk/kapi.json").read_text())["generations"],
    # 票 TASK_KAPI_DATA_FIELDS (v63) で必須になった関数表の容量と crt の kapi の実名。
    # この試験は `out` の解釈だけを見るので、関数数より十分大きい値にしておく。
    "func_capacity": 32,
    "crt_kapi_symbol": "os32_kapi_synth",
    "includes": [],
    "externs": [],
    "data_fields": [],
    "api": [
        {"name": "t_len_u", "ret": "int", "args": ["void *buf", "u32 size"],
         "target": "t_len_u_impl", "out": [{"arg": "buf", "len": "size"}]},
        {"name": "t_len_s", "ret": "void", "args": ["char *buf", "int size"],
         "target": "t_len_s_impl", "out": [{"arg": "buf", "len": "size"}]},
        {"name": "t_unit", "ret": "int", "args": ["u32 lba", "int count", "void *buf"],
         "target": "t_unit_impl",
         "out": [{"arg": "buf", "len": "count", "unit": 512}]},
        {"name": "t_size", "ret": "void", "args": ["void *info"],
         "target": "t_size_impl", "out": [{"arg": "info", "size": 40}]},
        {"name": "t_expr", "ret": "void", "args": ["void *info"],
         "target": "t_expr_impl", "out": [{"arg": "info", "size": "sizeof(IdeInfo)"}]},
        {"name": "t_three", "ret": "void", "args": ["u8 *r", "u8 *g", "u8 *b"],
         "target": "t_three_impl",
         "out": [{"arg": "r", "size": 1}, {"arg": "g", "size": 1},
                 {"arg": "b", "size": 1}]},
        {"name": "t_none", "ret": "void", "args": ["void *cb", "const char *path"],
         "target": "t_none_impl", "out": "none"},
        {"name": "t_target", "ret": "int", "args": ["u32 *lo"],
         "target": "t_target_impl", "out": "target"},
        {"name": "t_in", "ret": "int", "args": ["const char *path"],
         "target": "t_in_impl"},
    ],
}


def gen(tmp, data, args=()):
    """**呼び出しごとに空の** 一時ディレクトリを cwd にして生成器を回す。
    生成器の書き出し先は相対パスなので、実物の生成物には触らない。
    戻り: (rc, stdout+stderr, その一時ディレクトリ)"""
    d = pathlib.Path(tempfile.mkdtemp(dir=tmp))
    for sub in ("sdk/include/os32", "kapi", "exec", "sdk/rust/os32api/src", "sdk/crt", "sdk/link"):
        (d / sub).mkdir(parents=True, exist_ok=True)
    jp = d / "in.json"
    jp.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8")
    p = subprocess.run([sys.executable, "-B", str(SCRIPT), str(jp)] + list(args),
                       cwd=str(d), capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or ""), d


def wrapper(text, name):
    m = re.search(r"wrap_%s\([^)]*\)\n\{\n(.*?)\n\}\n" % name, text, re.S)
    assert m, "wrap_%s が生成されていない" % name
    return m.group(1)


def case_interpretation(tmp):
    """(b) len / size / unit の解釈と検査順"""
    rc, out, d = gen(tmp, SYNTH)
    assert rc == 0, "生成が落ちた: %s" % out
    c = (d / "kapi/kapi_generated.c").read_text(encoding="utf-8")

    w = wrapper(c, "t_len_u")
    assert "KAPI_OUT_LEN(buf, size)" in w, w
    w = wrapper(c, "t_len_s")
    assert "KAPI_OUT_LEN_S(buf, size)" in w, "int の長さは 0 に丸める形で出す: " + w
    w = wrapper(c, "t_unit")
    assert "kapi_out_mul(KAPI_OUT_LEN_S(buf, count), 512u)" in w, w
    w = wrapper(c, "t_size")
    assert "KAPI_OUT_LEN(info, 40u)" in w, w
    w = wrapper(c, "t_expr")
    assert "KAPI_OUT_LEN(info, sizeof(IdeInfo))" in w, w

    # 書かないポインタ / target が自分で見るものには 1 行も出さない
    for name in ("t_none", "t_target", "t_in"):
        assert "ring3_user_ranges_writable" not in wrapper(c, name), name

    # **全範囲を検査してから target を呼ぶ** (3 本なら 2 回の呼び出し)
    w = wrapper(c, "t_three")
    assert w.count("ring3_user_ranges_writable") == 2, w
    assert "ring3_fault_kill();" in w, w
    for arg in ("(u32)r", "(u32)g", "(u32)b"):
        assert arg in w, w
    assert w.index("ring3_fault_kill") < w.index("t_three_impl("), \
        "検査は target より前に出すこと: " + w

    # あふれ / NULL / 0 は共通のヘルパが持つ (1 か所だけ)
    assert c.count("#define KAPI_OUT_LEN(") == 1
    assert "0xFFFFFFFFUL;" in c, "個数 × 単位のあふれは必ず拒否する"
    print("CASE interpretation PASS", flush=True)
    return 0


BAD = [
    ("out 無し", lambda d: d["api"][0].pop("out"), "t_len_u"),
    ("arg が引数に無い",
     lambda d: d["api"][0].__setitem__("out", [{"arg": "nope", "len": "size"}]),
     "nope"),
    ("len が引数に無い",
     lambda d: d["api"][0].__setitem__("out", [{"arg": "buf", "len": "nope"}]),
     "nope"),
    ("出力が 1 本足りない",
     lambda d: d["api"][5].__setitem__("out", [{"arg": "r", "size": 1},
                                               {"arg": "g", "size": 1}]),
     '"b"'),
    ("len と size の両方",
     lambda d: d["api"][0].__setitem__("out", [{"arg": "buf", "len": "size",
                                                "size": 4}]),
     "どちらか一方"),
    ("size が 0",
     lambda d: d["api"][3].__setitem__("out", [{"arg": "info", "size": 0}]),
     "正の整数"),
    ("len が const ポインタ引数",
     lambda d: d["api"][6].__setitem__("out", [{"arg": "cb", "len": "path"}]),
     "ポインタ"),
    ("ポインタが無いのに out がある",
     lambda d: d["api"][8].__setitem__("out", [{"arg": "path", "size": 4}]),
     "t_in"),
]


def case_refuse(tmp):
    """(a) 書き忘れ・間違いで生成が落ちる"""
    bad = 0
    for why, mutate_fn, needle in BAD:
        d = copy.deepcopy(SYNTH)
        mutate_fn(d)
        rc, out, outdir = gen(tmp, d)
        ok = rc != 0 and needle in out
        # 落ちたときは**生成物を 1 バイトも書かない** (半分だけ新しい木を残さない)
        if ok and (outdir / "kapi/kapi_generated.c").exists():
            ok = False
            out += "\n(検査に落ちたのに kapi_generated.c を書いている)"
        print("  %-28s %s" % (why, "RED (落ちた)" if ok else "**通った (見逃し)**"),
              flush=True)
        if not ok:
            bad += 1
    print("CASE refuse %s" % ("PASS" if not bad else "FAIL"), flush=True)
    return bad


def case_real(tmp):
    """実物の sdk/kapi.json が全エントリ申告済みであること"""
    p = subprocess.run([sys.executable, "-B", str(SCRIPT), "--check-only"],
                       cwd=str(ROOT), capture_output=True, text=True)
    if p.returncode != 0:
        print("CASE real FAIL:\n%s%s" % (p.stdout, p.stderr), flush=True)
        return 1
    data = json.loads(KAPI_JSON.read_text(encoding="utf-8"))
    n = sum(1 for a in data["api"]
            if any("*" in x and "const" not in x for x in a.get("args", [])))
    print("CASE real PASS (非 const ポインタを持つ %d 本すべてに out)" % n, flush=True)
    return 0


# 否定側: 生成器の検査そのものを壊すと、上の refuse が落ちなくなること。
MUTATIONS = [
    (r'    if spec is None:\n        if outs:',
     '    if spec is None:\n        if False:',
     "out の書き忘れを見逃す"),
    (r'    for a in outs:\n        if a not in seen:',
     '    for a in outs:\n        if False:',
     "出力の数え落としを見逃す"),
    (r'        if has_len == has_size:',
     '        if False:',
     "len と size の同時指定を見逃す"),
]


def mutate(tmp):
    original = SCRIPT.read_text(encoding="utf-8")
    bad = 0
    try:
        for i, (pat, repl, why) in enumerate(MUTATIONS, 1):
            mutated, n = re.subn(pat, repl, original, count=1)
            if n != 1:
                print("MUTATION %d NOT APPLICABLE: %s" % (i, why), flush=True)
                bad += 1
                continue
            SCRIPT.write_text(mutated, encoding="utf-8")
            hits = case_refuse_quiet(tmp)
            status = "RED" if hits else "**GREEN (見逃し)**"
            print("MUTATION %d %s (%d 件): %s" % (i, status, hits, why), flush=True)
            bad += not hits
    finally:
        SCRIPT.write_text(original, encoding="utf-8")
    return bad


def case_refuse_quiet(tmp):
    hits = 0
    for _why, mutate_fn, needle in BAD:
        d = copy.deepcopy(SYNTH)
        mutate_fn(d)
        rc, out, _d = gen(tmp, d)
        if rc == 0 or needle not in out:
            hits += 1
    return hits


if __name__ == "__main__":
    args = sys.argv[1:]
    rc = 0
    with tempfile.TemporaryDirectory(prefix="os32-kapi-out-") as tmp:
        rc += case_interpretation(tmp)
        rc += case_refuse(tmp)
        rc += case_real(tmp)
        if "--mutate" in args:
            rc += mutate(tmp)
    sys.exit(bool(rc))
