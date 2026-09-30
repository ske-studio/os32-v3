#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_manifests.py — 配備マニフェストと app.conf の参照先を検査する

3 種類の食い違いを検出する。いずれも「静かに壊れる」たちの悪い部類:

1. 配備定義が挙げているのにビルドされないファイル
   → NHD 上に古いバイナリが残り続ける。KAPI レイアウトが変わると
     旧バイナリの KAPI 呼び出しが別関数へ飛び、exit 後の jmp $ で
     永久スピンして rshell ごと沈黙する (deploy.yaml 冒頭の警告)。

2. app.conf のキーが実在のターゲットに一致していない
   → 既定値 (api 7 / heap 64KB) で出荷される。設定したつもりの
     ヒープサイズが効かない。

3. ビルドされるのに配備定義に載っていないバイナリ
   → 実機に届かない。

4. 全画面 GFX (gfx_init 系) を呼ぶのに app.conf に `gfx` の宣言が無い
   → OS32X_FLAG_GFX が立たず、GUI 中の起動で画面の所有権を取れない
     (票 T8 D1a)。4 列目の書式エラーも同じ節で見る。

5. V86 (v86_* KAPI) を呼ぶのに app.conf に `cui` の宣言が無い
   → OS32X_FLAG_CUI_ONLY が立たず、GUI から起動できてしまう
     (票 T8-2、受入 F5 の不合格そのもの)。同じ節で見る。

先に make all を通してから実行すること。
"""

import glob
import os
import sys

try:
    import yaml
except ImportError:
    print("PyYAML が必要: pip install pyyaml", file=sys.stderr)
    sys.exit(2)

# 配備定義は所有する層ごとに分かれている。
DEPLOY_MANIFESTS = [
    "build/core.yaml",
    "userland/deploy.yaml",
]
# CD のパッケージの構成。中身は配備マニフェストのタグから作るので、ここに
# 直に書いてあるのは媒体だけの物 (ブートセクタ、settings.db) だけ。振り分けの
# 検査は make check-packages-host (tools/tests/test_packages.py)。
PACKAGE_PLAN = "build/packages.yaml"
APP_CONF = "build/app.conf"


def built_binaries():
    out = set()
    for root in ("userland",):
        for p in glob.glob(root + "/**/*.bin", recursive=True):
            out.add(p)
    return out


def check_missing_hosts():
    """戻り値: (エラー, 警告)

    glob は「あれば配る」という書き方なので一致ゼロでもエラーにしない。
    実際 manga は *.mgx と *.MGX を両方書いて大小文字を吸収しており、
    どちらかは必ず一致しない。実ファイル指定の欠落だけがエラー。
    """
    bad = []
    warn = []
    for path in DEPLOY_MANIFESTS:
        if not os.path.isfile(path):
            bad.append((path, "-", "マニフェストがない"))
            continue
        with open(path, encoding="utf-8") as f:
            d = yaml.safe_load(f) or {}
        for e in (d.get("filesystem") or {}).get("files") or []:
            h = e["host"]
            if e.get("type") == "glob" or "*" in h:
                if not glob.glob(h):
                    warn.append((path, h, "glob 一致なし"))
            elif not os.path.isfile(h):
                bad.append((path, h, "ファイルなし"))

    if not os.path.isfile(PACKAGE_PLAN):
        bad.append((PACKAGE_PLAN, "-", "パッケージ構成がない"))
    else:
        with open(PACKAGE_PLAN, encoding="utf-8") as f:
            p = yaml.safe_load(f) or {}
        for pkg, body in (p.get("packages") or {}).items():
            for e in (body or {}).get("files") or []:
                h = e["host"]
                if not os.path.isfile(h):
                    bad.append((PACKAGE_PLAN + " [" + pkg + "]", h, "ファイルなし"))
    return bad, warn


def check_doc_counts():
    """ドキュメントが書いている件数が実態と合っているか

    「17 commands」「全 11 本」の類。増減しても誰も気づかず、気づいたときには
    どれが正しいのか分からなくなる。数えられるものは数えて突き合わせる。
    """
    import re

    def count(pattern):
        return len(glob.glob(pattern))

    checks = [
        ("CLAUDE.md", r"\((\d+) commands\)",
         count("userland/cmds/*.c"), "userland/cmds/*.c"),
        # docs/INDEX.md のソースツリー複製は 2026-09-05 に削除 (件数を持つ文書は
        # CLAUDE.md の 1 か所だけにする)。
    ]

    bad = []
    for path, pat, actual, what in checks:
        if not os.path.isfile(path):
            continue
        m = re.search(pat, open(path, encoding="utf-8").read())
        if not m:
            bad.append((path, "-", "件数の記述が見つからない (%s)" % pat))
            continue
        claimed = int(m.group(1))
        if claimed != actual:
            bad.append((path, str(claimed),
                        "実際は %d (%s)" % (actual, what)))
    return bad


def check_shell_commands():
    """docs/07_shell.md のコマンド一覧が実装と合っているか

    組み込みは userland/shell/*.c の ShellCmd テーブル、外部コマンドは
    userland/cmds/*.c が実体。コマンドを足しても一覧に載せ忘れる。
    実際 ime と v86 が漏れ、組み込みも 9 件抜けていた。

    文書にしか無いものは「消したのに残っている」、実装にしか無いものは
    「足したのに書いていない」。どちらも検出する。
    """
    import re

    doc_path = "docs/07_shell.md"
    if not os.path.isfile(doc_path):
        return []

    doc = set()
    with open(doc_path, encoding="utf-8") as f:
        text = f.read()
    for line in text.splitlines():
        m = re.match(r"^\|\s*`([a-z0-9_.]+)`", line)
        if m:
            doc.add(m.group(1))
    # 「エイリアス: `cls`→`clear`」の形で挙げたものも記載済みとみなす
    for m in re.finditer(r"`([a-z0-9_.]+)`\s*→", text):
        doc.add(m.group(1))
    # 外部コマンドは列挙行に並ぶ
    for m in re.finditer(r"`([a-z0-9_.]+)`", text):
        doc.add(m.group(1))

    builtin = set()
    for path in glob.glob("userland/shell/*.c"):
        with open(path, encoding="utf-8", errors="replace") as f:
            for m in re.finditer(r'\{\s*"([a-z0-9_.]+)"\s*,\s*cmd_', f.read()):
                builtin.add(m.group(1))
    external = set(os.path.basename(p)[:-2] for p in glob.glob("userland/cmds/*.c"))

    missing = sorted((builtin | external) - doc)
    bad = []
    if missing:
        bad.append(("docs/07_shell.md", ", ".join(missing),
                    "実装にあるが一覧に無い"))
    return bad


def check_app_conf(bins):
    """app.conf のキーが実在の .bin に対応しているか"""
    bad = []
    with open(APP_CONF, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            key = s.split()[0]
            if key + ".bin" not in bins:
                bad.append((lineno, key))
    return bad


# ---------------------------------------------------------------------------
# 4. 宣言ビット (app.conf の 4 列目 = mkos32x のフラグ)
#
#   `gfx`      → --gfx       (OS32X_FLAG_GFX、票 T8 D1a)
#   `cui`      → --cui-only  (OS32X_FLAG_CUI_ONLY、票 T8-2)
#   `launcher` → --launcher  (OS32X_FLAG_LAUNCHER、票 T9 D1a)
#
# gfx を立て忘れると GUI 中に gfx_init がカーネルに蹴られ (ERR_INVAL でアプリ
# ごと畳まれる) か、WM が全画面に入らずにプログラムの画面を上書きする。
# cui を立て忘れると v86 / VDM が GUI から起動でき、画面と BIOS を丸ごと
# 持っていかれて WM が復帰できない (受入 F5 の不合格)。どちらも「静かに
# 壊れる」ので機械で見る。
#
# 除外リストは持たない。判定はソースが gfx_init 系 / v86_* を呼ぶかどうか
# だけで、ライブラリ経由 (tilemap_init → libos32gfx_init) も呼び出しグラフを
# userland/lib から作って追う。
# ---------------------------------------------------------------------------

GFX_INIT_SEED = ("libos32gfx_init", "gfx_init", "gfx_init_200")
DECL_COL = 3         # app.conf の 4 列目 (0 始まり)
GFX_COL = DECL_COL   # 旧名 (参照が残っている間の互換)
GFX_MARK = "gfx"
CUI_MARK = "cui"
# launcher は「launch_req で WM に起動を頼む」という宣言 (票 T9 D1a)。gfx / cui と
# 違い、呼び出しの有無との突き合わせはまだしない (KAPI v49 の launch_req が
# 着地するまで種を持てない)。書式として許すところまで。
LAUNCHER_MARK = "launcher"
DECL_MARKS = (GFX_MARK, CUI_MARK, LAUNCHER_MARK)

# V86 へ入る KAPI (sdk/kapi.json)。カーネル側で低位メモリを張り替え BIOS と
# テキスト VRAM を丸ごと使うので、これを呼ぶプログラムは CUI 専用。
V86_KAPI_SEED = ("v86_selftest", "v86_disktest", "v86_boot", "v86_boot2")


def _strip_comments(text):
    """C / Rust のコメントを潰す (行数は保つ)。

    コメントの中の `gfx_init()` を呼び出しと読むと libos32gfx_attach
    (「gfx_init() は呼ばない」と書いてある) まで巻き込む。
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _c_functions(text):
    """トップレベルの関数定義を (名前, 本文) で返す (雑だが十分)。"""
    import re
    funcs = []
    depth = 0
    start = 0
    name = None
    pend_start = 0
    for i, ch in enumerate(text):
        if ch == '{':
            if depth == 0:
                head = text[pend_start:i]
                m = None
                for m in re.finditer(r'([A-Za-z_]\w*)\s*\(', head):
                    pass
                name = m.group(1) if m else None
                start = i
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth <= 0:
                depth = 0
                if name:
                    funcs.append((name, text[start:i]))
                name = None
                pend_start = i + 1
    return funcs


def _call_names(seed):
    """seed に到達する関数名の集合 (userland/lib で不動点まで広げる)。"""
    import re
    names = set(seed)
    lib_funcs = []
    for path in sorted(glob.glob("userland/lib/**/*.c", recursive=True)):
        with open(path, encoding="utf-8", errors="replace") as f:
            lib_funcs.extend(_c_functions(_strip_comments(f.read())))
    changed = True
    while changed:
        changed = False
        for fname, body in lib_funcs:
            if fname in names:
                continue
            for n in names:
                if re.search(r'\b%s\s*\(' % re.escape(n), body):
                    names.add(fname)
                    changed = True
                    break
    return names


def _gfx_call_names():
    return _call_names(GFX_INIT_SEED)


def _v86_call_names():
    return _call_names(V86_KAPI_SEED)


def _calls_any(paths, names):
    import re
    pats = [re.compile(r'\b%s\s*\(' % re.escape(n)) for n in names]
    # Rust の `(a.gfx_init)()` 形式 (KAPI 構造体のフィールド呼び出し)
    rust_pats = [re.compile(r'\b%s\s*\)' % re.escape(n)) for n in names]
    for p in paths:
        with open(p, encoding="utf-8", errors="replace") as f:
            src = _strip_comments(f.read())
        use = pats + (rust_pats if p.endswith(".rs") else [])
        for pat in use:
            if pat.search(src):
                return True
    return False


def _rust_program_units():
    """build/programs.mk の DEFINE_RUST_PROGRAM 登録から crate → 出力先を読む。"""
    import re
    units = {}
    mk = "build/programs.mk"
    if not os.path.isfile(mk):
        return units
    with open(mk, encoding="utf-8") as f:
        for m in re.finditer(r'DEFINE_RUST_PROGRAM,([A-Za-z0-9_]+),([^,)]+)',
                             f.read()):
            crate, outdir = m.group(1), m.group(2).strip()
            srcs = glob.glob("userland/rust/%s/src/**/*.rs" % crate,
                             recursive=True)
            if srcs:
                units["%s/%s" % (outdir, crate)] = srcs
    return units


def program_units():
    """app.conf のキー → そのプログラムのソース一覧。"""
    units = {}
    for d in ("userland/cmds", "userland/tests", "userland/system"):
        for p in glob.glob(d + "/*.c"):
            units[p[:-2]] = [p]
    for sub in glob.glob("userland/tests/*/"):
        srcs = glob.glob(sub + "**/*.c", recursive=True)
        if srcs:
            units[sub.rstrip("/")] = srcs
    units.update(_rust_program_units())
    for key, pat in (("userland/shell", "userland/shell/*.c"),
                     # sh は同じソースの CPL=3 版 (-DSHELL_AS_APP、票 T9 D1)
                     ("userland/sh", "userland/shell/*.c"),
                     ("userland/gshell", "userland/gshell/src/**/*.rs")):
        srcs = glob.glob(pat, recursive=True)
        if srcs:
            units[key] = srcs
    return units


def read_app_conf():
    """キー → (行番号, 列リスト)"""
    conf = {}
    with open(APP_CONF, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            cols = s.split()
            conf[cols[0]] = (lineno, cols)
    return conf


def _declared(conf, key, mark):
    return (key in conf and len(conf[key][1]) > DECL_COL
            and conf[key][1][DECL_COL] == mark)


def check_gfx_flag():
    """戻り値: (列の書式エラー, gfx 漏れ, gfx 余分, cui 漏れ, cui 余分)"""
    conf = read_app_conf()

    why = "4 列目は 'gfx' / 'cui' / 'launcher' か省略のみ"
    bad_col = []
    for key, (lineno, cols) in sorted(conf.items()):
        if len(cols) > DECL_COL + 1:
            bad_col.append((lineno, key, " ".join(cols[DECL_COL:]), why))
        elif len(cols) == DECL_COL + 1 and cols[DECL_COL] not in DECL_MARKS:
            bad_col.append((lineno, key, cols[DECL_COL], why))

    gfx_names = _gfx_call_names()
    v86_names = _v86_call_names()
    missing, extra = [], []
    cui_missing, cui_extra = [], []
    for key, srcs in sorted(program_units().items()):
        calls_gfx = _calls_any(srcs, gfx_names)
        calls_v86 = _calls_any(srcs, v86_names)
        if calls_gfx and not _declared(conf, key, GFX_MARK):
            missing.append(key)
        elif _declared(conf, key, GFX_MARK) and not calls_gfx:
            extra.append(key)
        if calls_v86 and not _declared(conf, key, CUI_MARK):
            cui_missing.append(key)
        elif _declared(conf, key, CUI_MARK) and not calls_v86:
            cui_extra.append(key)
    return bad_col, missing, extra, cui_missing, cui_extra


def check_undeployed(bins):
    deployed = set()
    for path in DEPLOY_MANIFESTS:
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            d = yaml.safe_load(f) or {}
        for e in (d.get("filesystem") or {}).get("files") or []:
            h = e["host"]
            if "*" in h:
                deployed.update(glob.glob(h))
            else:
                deployed.add(h)
    return sorted(bins - deployed)


def main():
    bins = built_binaries()
    if not bins:
        print("ビルド成果物が見つからない。先に make all を実行すること。",
              file=sys.stderr)
        return 2

    rc = 0

    missing, warn = check_missing_hosts()
    print("== 1. マニフェストが挙げているのに存在しないファイル ==")
    if missing:
        rc = 1
        for src, h, why in missing:
            print("  [NG] {:34s} {}  ({})".format(src, h, why))
    else:
        print("  なし")
    if warn:
        for src, h, why in warn:
            print("  [--] {:34s} {}  ({})".format(src, h, why))

    doc_counts = check_doc_counts()
    print("== 1b. ドキュメントの件数が実態と合っているか ==")
    if doc_counts:
        rc = 1
        for src, claimed, why in doc_counts:
            print("  [NG] {:34s} {}  ({})".format(src, claimed, why))
    else:
        print("  なし")

    shell_cmds = check_shell_commands()
    print("== 1c. シェルコマンド一覧が実装と合っているか ==")
    if shell_cmds:
        rc = 1
        for src, what, why in shell_cmds:
            print("  [NG] {:34s} {}  ({})".format(src, what, why))
    else:
        print("  なし")

    bad_keys = check_app_conf(bins)
    print("== 2. 実在ターゲットに一致しない app.conf のキー ==")
    if bad_keys:
        rc = 1
        for lineno, key in bad_keys:
            print("  [NG] {}:{}  '{}' に対応する .bin がない".format(
                APP_CONF, lineno, key))
    else:
        print("  なし")

    (bad_col, gfx_missing, gfx_extra,
     cui_missing, cui_extra) = check_gfx_flag()
    print("== 2b. 宣言ビット (app.conf の 4 列目 gfx / cui / launcher) ==")
    if bad_col:
        rc = 1
        for lineno, key, col, why in bad_col:
            print("  [NG] {}:{}  '{}' の 4 列目 '{}'  ({})".format(
                APP_CONF, lineno, key, col, why))
    if gfx_missing:
        rc = 1
        for key in gfx_missing:
            print("  [NG] {:44s} gfx_init 系を呼ぶのに app.conf に 'gfx' が無い"
                  .format(key))
    if cui_missing:
        rc = 1
        for key in cui_missing:
            print("  [NG] {:44s} v86_* KAPI を呼ぶのに app.conf に 'cui' が無い"
                  .format(key))
    if gfx_extra:
        for key in gfx_extra:
            print("  [--] {:44s} 'gfx' 宣言があるが gfx_init 系の呼び出しが"
                  "見当たらない".format(key))
    if cui_extra:
        for key in cui_extra:
            print("  [--] {:44s} 'cui' 宣言があるが v86_* KAPI の呼び出しが"
                  "見当たらない".format(key))
    if not bad_col and not gfx_missing and not gfx_extra and not cui_missing \
            and not cui_extra:
        print("  なし")

    undeployed = check_undeployed(bins)
    print("== 3. ビルドされるが配備定義に載っていないバイナリ ==")
    if undeployed:
        for h in undeployed:
            print("  [--] {}".format(h))
        print("  ({} 件。意図的に配備しないものはここに出てよい)".format(
            len(undeployed)))
    else:
        print("  なし")

    return rc


if __name__ == "__main__":
    sys.exit(main())
