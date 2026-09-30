#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_kapi_version.py — KAPI バージョン表記の一致検査

KAPI のバージョンは sdk/kapi.json の "version" が唯一の情報源。
そこから生成されるヘッダと、人が手で書いたドキュメントの版数が
ずれていないかを検査する。

ずれていると「どれが本当の版か」が分からなくなり、実機で動かない
バイナリの原因究明が遠回りになる。2026-08 時点では実ヘッダ v39 に対して
README が v31、docs が v35 とまちまちだった。

使い方: python3 tools/check_kapi_version.py    (make check-kapi-version)
終了コード 0 = 一致、1 = 不一致
"""

import json
import re
import sys

SSOT = "sdk/kapi.json"

# (パス, 版数を取り出す正規表現, 説明)
TARGETS = [
    ("sdk/include/os32/os32_kapi_shared.h",
     r"#define\s+KAPI_VERSION\s+(\d+)",
     "SDK 契約ヘッダ"),
    ("README.md",
     r"KernelAPI v(\d+)",
     "README"),
    ("docs/INDEX.md",
     r"KernelAPI v(\d+) 仕様書",
     "ドキュメント索引"),
    ("docs/KAPI_SPEC.md",
     r"^# KernelAPI v(\d+) 仕様書",
     "KAPI 仕様書"),
]


def check_spec_table():
    """docs/KAPI_SPEC.md の関数表が kapi.json と一致しているか

    表は手書きなので、関数を足したときに更新を忘れる。実際 v86 系の 4 本が
    丸ごと抜け、その分データフィールドのオフセットが 16 バイト手前へ
    ずれていた。オフセットは外部プログラムが構造体を引く位置そのものなので、
    ドキュメントを信じて実装すると別の関数を呼ぶ。

    名前・並び順・オフセット・シグネチャの 4 つを突き合わせる。
    """
    import os

    proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    spec = os.path.join(proj, "docs", "KAPI_SPEC.md")
    kapi = os.path.join(proj, "sdk", "kapi.json")
    if not (os.path.isfile(spec) and os.path.isfile(kapi)):
        return ["KAPI_SPEC.md または kapi.json が見つからない"]

    with open(kapi, encoding="utf-8") as f:
        kj = json.load(f)
    api = kj["api"]
    cap = kj.get("func_capacity", len(api))

    row = re.compile(r"^\|\s*(0x[0-9A-Fa-f]+)\s*\|\s*([A-Za-z_][A-Za-z0-9_]*)"
                     r"\s*\|\s*`([^`]*)`\s*\|")
    rows = []
    with open(spec, encoding="utf-8") as f:
        for line in f:
            m = row.match(line)
            if m:
                rows.append((int(m.group(1), 16), m.group(2), m.group(3)))

    end = 0x08 + 4 * len(api)          # 関数領域の終端 = データフィールドの先頭
    want = [(0x08 + 4 * i, e["name"]) for i, e in enumerate(api)]
    got = [(o, n) for o, n, _ in rows if o < end]

    problems = []
    wn = {n for _, n in want}
    gn = {n for _, n in got}
    missing = [n for _, n in want if n not in gn]
    extra = [n for _, n in got if n not in wn]
    if missing:
        problems.append("KAPI_SPEC.md に無い関数: %s" % ", ".join(missing))
    if extra:
        problems.append("KAPI_SPEC.md にのみある関数: %s" % ", ".join(extra))

    if not problems and want != got:
        for i, (a, b) in enumerate(zip(want, got)):
            if a != b:
                problems.append("並び/オフセットのずれ: idx %d 期待 0x%X %s / 文書 0x%X %s"
                                % (i, a[0], a[1], b[0], b[1]))
                break

    sig = dict((n, s) for _, n, s in rows)
    for e in api:
        w = "%s(%s)" % (e["ret"], ", ".join(e["args"]) or "void")
        g = sig.get(e["name"])
        if g is not None and g.replace(" ", "") != w.replace(" ", ""):
            problems.append("シグネチャ不一致 %s: json=%s / doc=%s"
                            % (e["name"], w, g))

    # データフィールドは v63 から関数表の容量 (func_capacity) の後ろに固定
    # (票 TASK_KAPI_DATA_FIELDS)。関数を足しても動かない。
    data_off = 0x08 + 4 * cap
    for i, df in enumerate(kj["data_fields"]):
        off = data_off + 4 * i
        found = [o for o, n, _ in rows if n == df["name"]]
        if not found:
            problems.append("データフィールドが KAPI_SPEC.md に無い: %s" % df["name"])
        elif found[0] != off:
            problems.append("データフィールドのオフセットずれ %s: 期待 0x%X / 文書 0x%X"
                            % (df["name"], off, found[0]))
    return problems


def check_layout():
    """データ欄の固定配置と crt の kapi の実名が、生成物と手書きの写しで
    食い違っていないか (票 TASK_KAPI_DATA_FIELDS)。"""
    import os

    proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    def read(rel):
        with open(os.path.join(proj, rel), encoding="utf-8") as f:
            return f.read()

    kj = json.loads(read("sdk/kapi.json"))
    n = len(kj["api"])
    cap = kj.get("func_capacity")
    problems = []
    if not isinstance(cap, int) or cap < n:
        problems.append("func_capacity (%r) が関数数 %d より小さいか無い" % (cap, n))
        return problems
    data_off = 8 + 4 * cap
    sym = kj.get("crt_kapi_symbol", "")

    gen = read("sdk/include/os32/os32_kapi_generated.h")
    want = [
        (r"#define\s+KAPI_FUNC_CAPACITY\s+(\d+)", str(cap), "KAPI_FUNC_CAPACITY"),
        (r"#define\s+KAPI_DATA_FIELDS_OFF\s+(0x[0-9A-Fa-f]+)", "0x%X" % data_off,
         "KAPI_DATA_FIELDS_OFF"),
        (r"#define\s+kapi\s+(\w+)", sym, "#define kapi"),
        (r"\.long 0x([0-9A-Fa-f]+)", "%X" % data_off, "刻印 (OS32_KAPI_LAYOUT_STAMP)"),
    ]
    for pat, val, label in want:
        m = re.search(pat, gen)
        if not m or m.group(1).upper() != val.upper():
            problems.append("os32_kapi_generated.h の %s が %s でない (生成し直す)"
                            % (label, val))

    rs = read("sdk/rust/os32api/src/kapi_generated.rs")
    m = re.search(r"KAPI_DATA_FIELDS_OFF: u32 = 0x([0-9A-Fa-f]+)", rs)
    if not m or int(m.group(1), 16) != data_off:
        problems.append("kapi_generated.rs の KAPI_DATA_FIELDS_OFF が 0x%X でない" % data_off)

    # Rust の shlib が C の libos32cfg に供給する kapi の実名 (手書きの写し)
    cfgro = read("userland/rust/libos32gui/src/cfgro.rs")
    m = re.search(r'#\[export_name\s*=\s*"(\w+)"\]\s*pub static mut kapi', cfgro)
    if not m or m.group(1) != sym:
        problems.append("userland/rust/libos32gui/src/cfgro.rs の kapi の export_name が "
                        "kapi.json の crt_kapi_symbol (%s) と違う" % sym)

    # ヘッダ v3 の最低版 (C / Rust / 生成器の 3 か所の写し)
    shared = read("sdk/include/os32/os32_kapi_shared.h")
    hdrpy = read("sdk/os32x_hdr.py")
    m1 = re.search(r"#define\s+OS32X_HDR_V3_MIN_API\s+(\d+)", shared)
    m2 = re.search(r"OS32X_HDR_V3_MIN_API: u32 = (\d+);", rs)
    m3 = re.search(r"^OS32X_HDR_V3_MIN_API = (\d+)", hdrpy, re.M)
    vals = [x.group(1) if x else None for x in (m1, m2, m3)]
    if None in vals or len(set(vals)) != 1:
        problems.append("OS32X_HDR_V3_MIN_API の写しが食い違う (shared.h / "
                        "kapi_generated.rs / sdk/os32x_hdr.py): %s" % vals)
    return problems


def main():
    with open(SSOT, encoding="utf-8") as f:
        expected = int(json.load(f)["version"])

    bad = []
    for path, pattern, label in TARGETS:
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except IOError:
            bad.append((path, label, "ファイルが読めない"))
            continue
        m = re.search(pattern, text, re.M)
        if not m:
            bad.append((path, label, "版数の記述が見つからない"))
        elif int(m.group(1)) != expected:
            bad.append((path, label, "v{} (期待 v{})".format(m.group(1), expected)))

    if bad:
        print("KAPI バージョン不一致 ({} は v{})".format(SSOT, expected))
        for path, label, why in bad:
            print("  {:44s} {:16s} {}".format(path, label, why))
        print()
        print("sdk/kapi.json の version に合わせて上記を更新すること。")
        return 1

    print("KAPI バージョン一致: v{} ({} 箇所)".format(expected, len(TARGETS)))

    spec = check_spec_table()
    if spec:
        print("")
        print("KAPI_SPEC.md の関数表が kapi.json と食い違っている")
        for s in spec:
            print("  - {}".format(s))
        print("")
        print("表は手書きなので、関数を足したら同じコミットで更新すること。")
        return 1

    print("KAPI_SPEC.md の関数表: kapi.json と一致")

    lay = check_layout()
    if lay:
        print("")
        print("KAPI データ欄の固定配置 (票 TASK_KAPI_DATA_FIELDS) の写しが食い違っている")
        for s in lay:
            print("  - {}".format(s))
        return 1
    print("KAPI データ欄の固定配置: 生成物・写しと一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
