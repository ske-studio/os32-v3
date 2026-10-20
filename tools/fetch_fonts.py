#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fetch_fonts.py — IPAex / Inconsolata / JF ドット東雲16 を取得する

ユーザー決定 (2026-09-30): 日本語フォントはリポジトリに持たず、ビルド時に IPA の
公式配布からダウンロードする。未取得ならビルドの前にライセンス (IPA Font License
Agreement v1.0) の全文を見せて同意を取る。

Inconsolata Regular 3.000 は --family inconsolata で取得する。zip/TTF/OFL の
SHA-256 を照合し、原文を Inconsolata-OFL.txt として添える。IPA 専用の同意は
要求しない。オフラインでは --zip PATH --ofl PATH を使う。

JF ドット東雲ゴシック16 は --family jfdot-shinonome16 で取得する。
zip/TTF の SHA-256 を照合し、固定した東雲ライセンス全文を添える。
オフラインでは --zip PATH を使う。同意の入力は不要。

既定 (IPAex) の流れ:
  1. assets/fonts/ipaexg.ttf と ipaexm.ttf が両方あって SHA-256 が合えば、
     何も聞かず・何も取らずに rc=0。fetch_fonts.py より古いものだけ mtime を
     更新する (make の依存を満たす。新しいものは触らない — 配備の入力なので)。
  2. 無い (または --force) ならライセンスの同意を確かめる:
       - assets/fonts/.license_accepted に同じライセンス文の SHA-256 が記録済み → 同意済み
       - OS32_ACCEPT_IPA_LICENSE=1                                          → 同意 (非対話)
       - stdin が端末                                                        → 全文を出して
                                                                              「同意しますか [y/N]」
       - それ以外 (パイプ・CI)                                               → rc=2
  3. 公式 zip (IPAexfont00401.zip) を取得し、zip 全体の SHA-256 を照合してから
     2 本を展開、それぞれの SHA-256 を照合して置く。
  4. 同意を .license_accepted (日時 + ライセンス文の SHA-256) に記録する。

取得先は IPA (文字情報技術促進協議会) の公式配布だけ。第三者ミラーは使わない。
プロキシは urllib の既定 (http_proxy / https_proxy 環境変数) に任せる。
標準ライブラリだけで動く (requirements.txt には足さない)。

終了コード: 0 = 揃った、1 = 取得・照合の失敗、2 = ライセンス未同意。
どの失敗でも最後の 1 行が案内 (build/assets.mk はそれを make の失敗文の直前に出す)。

使い方:
  python3 tools/fetch_fonts.py                # 無ければ取る (make fonts / make all が呼ぶ)
  python3 tools/fetch_fonts.py --force        # 取り直す
  python3 tools/fetch_fonts.py --zip X.zip    # 手で置いた zip から展開 (オフラインのホスト)
  python3 tools/fetch_fonts.py --print-zip-sha256   # CI の cache key 用

試験: tools/tests/test_fetch_fonts.py (make check-tools-host の 1 行。ネットワーク無し)
"""

import argparse
import datetime
import hashlib
import io
import os
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "assets", "fonts")

# --- 公式配布 (2026-09-30 に HEAD/GET で到達を確認し、SHA-256 を実測した) ---
ZIP_URL = "https://moji.or.jp/wp-content/ipafont/IPAexfont/IPAexfont00401.zip"
ZIP_NAME = "IPAexfont00401.zip"
ZIP_SHA256 = "bcf8374ab3f9672c421120430dd19a51c99f5265cf06fc340d9a661ddfd7974b"
ZIP_SIZE = 9738669

# zip の中の名前 → (置き先の名前, SHA-256)
MEMBERS = {
    "IPAexfont00401/ipaexg.ttf": (
        "ipaexg.ttf",
        "3b9955a5e437ffb24c41548e79296fa724822da21ee75dc1b94f6ccdb8f400dd"),
    "IPAexfont00401/ipaexm.ttf": (
        "ipaexm.ttf",
        "7a306386f930fee80922f71eebf4ffe0f1ff2817da8e619230953487673d71c7"),
}

# Inconsolata Regular 3.000: upstream release, unmodified (T7b-a1).
INCONSOLATA_ZIP_URL = "https://github.com/googlefonts/Inconsolata/releases/download/v3.000/fonts_ttf.zip"
INCONSOLATA_ZIP_SHA256 = "626e8ee07501dbb544b50aa59ac2e4b9ec86b810670158a59c7a3cbaf475548a"
INCONSOLATA_MEMBERS = {
    "fonts/ttf/Inconsolata-Regular.ttf": (
        "Inconsolata-Regular.ttf",
        "127875d255d4c5973ca57267a43bb9d1c04397e6c7d236984a595b6cdcb12b7c"),
}
OFL_URL = "https://github.com/googlefonts/Inconsolata/releases/download/v3.000/OFL.txt"
OFL_NAME = "Inconsolata-OFL.txt"
OFL_SHA256 = "5d362a6f8690517fd9a5573128a081d8bbbb2f92714cf00556e08fbbe9600426"

# JF Dot font 2015-05-27: only the unmodified Shinonome Gothic 16.
JFDOT_ZIP_URL = "https://ftp.iij.ad.jp/pub/osdn.jp/users/8/8542/jfdotfont-20150527.zip"
JFDOT_ZIP_SHA256 = "8e574faaaa7e27294aec66464aaf427447b2f0ccec8fe00c50e69bbf509f14b1"
JFDOT_ZIP_SIZE = 41637317
JFDOT_TTF_SIZE = 1954728
JFDOT_MEMBERS = {
    "JF-Dot-Shinonome16.ttf": (
        "JF-Dot-Shinonome16.ttf",
        "5e265e45349b3328afa67dc3905a3ca3c628cf7c7e0eccea9c7ce8a8acc127cc"),
}
# Shinonome 0.9.11 LICENSE, converted from EUC-JP to UTF-8.
# JF inherits the original license and asserts no independent rights:
# http://jikasei.me/font/jf-dotfont/
# The JF zip has no license; keep the complete declaration here for offline use.
JFDOT_LICENSE_NAME = "JF-Dot-Shinonome16-LICENSE.txt"
JFDOT_LICENSE_TEXT = """●東雲フォントライセンス
                                                                    2001
                                     The Electronic Font Open Laboratory
                                        http://openlab.ring.gr.jp/efont/

このアーカイブに含まれるすべてのフォントデータ、ドキュメント、スクリプ
ト類はすべて Public Domain で提供されています 。

但し、日本に於いては現時点で著作権を放棄することは法律上不可能であり、
AUTHORS に列挙されている作者がその権利を行使しないと宣言することで実質
的な Public Domain であるとします。

自由な改造、他フォーマットへの変換、組込み、再配布を行うことができます。
同時に、これらはすべて完全に無保証です。
""".encode("utf-8")

# 同梱しているライセンス文 (zip の IPA_Font_License_Agreement_v1.0.txt と同一)
LICENSE_NAME = "IPA_Font_License_Agreement_v1.0.txt"
LICENSE_SHA256 = "4c84dd528ec3044638ec346fc1ee27cd1eb95dfc04cbc6a881b3ca7a7f517e54"
ACCEPTED_NAME = ".license_accepted"

ENV_ACCEPT = "OS32_ACCEPT_IPA_LICENSE"

RC_OK = 0
RC_FETCH = 1
RC_LICENSE = 2

GUIDE_LICENSE = ("フォント未取得: `make fonts` でライセンスに同意して取得するか、"
                 "%s=1 を付けて非対話で同意" % ENV_ACCEPT)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def fonts_ok(font_dir, members=None):
    """2 本とも存在して SHA-256 が合えば True。"""
    for _, (name, digest) in (MEMBERS if members is None else members).items():
        path = os.path.join(font_dir, name)
        if not os.path.isfile(path) or sha256_file(path) != digest:
            return False
    return True


def read_license(font_dir):
    """同梱のライセンス文を読む。SHA-256 が上流と違えば None。"""
    path = os.path.join(font_dir, LICENSE_NAME)
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as f:
        data = f.read()
    if sha256_bytes(data) != LICENSE_SHA256:
        return None
    return data


def accepted_recorded(font_dir):
    """.license_accepted に今のライセンス文の SHA-256 が記録されていれば True。"""
    path = os.path.join(font_dir, ACCEPTED_NAME)
    if not os.path.isfile(path):
        return False
    try:
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                if line.strip() == "license-sha256: " + LICENSE_SHA256:
                    return True
    except (OSError, UnicodeDecodeError):
        return False
    return False


def record_acceptance(font_dir, how):
    path = os.path.join(font_dir, ACCEPTED_NAME)
    now = datetime.datetime.now(datetime.timezone.utc).astimezone()
    with open(path, "w", encoding="utf-8") as f:
        f.write("# IPA Font License Agreement v1.0 への同意の記録 (tools/fetch_fonts.py)\n")
        f.write("accepted: %s\n" % now.strftime("%Y-%m-%dT%H:%M:%S%z"))
        f.write("how: %s\n" % how)
        f.write("license: %s\n" % LICENSE_NAME)
        f.write("license-sha256: %s\n" % LICENSE_SHA256)


def env_accepts(environ):
    return environ.get(ENV_ACCEPT, "").strip() == "1"


def ask_license(font_dir, license_text, stdin, stdout):
    """全文を標準出力に出して y/N を聞く。同意なら True。"""
    stdout.write(license_text.decode("utf-8-sig").replace("\r\n", "\n"))
    stdout.write("\n")
    stdout.write("=" * 72 + "\n")
    stdout.write("IPAex フォント (ipaexg.ttf / ipaexm.ttf) を %s から取得します。\n" % ZIP_URL)
    stdout.write("上の IPA Font License Agreement v1.0 に同意しますか [y/N] ")
    stdout.flush()
    answer = stdin.readline()
    if not answer:
        return False
    return answer.strip().lower() in ("y", "yes")


def confirm_license(font_dir, environ, stdin, stdout, stderr):
    """同意の判定。戻り値: (同意したか, 記録すべき how または None)。"""
    if accepted_recorded(font_dir):
        return True, None
    if env_accepts(environ):
        return True, "env %s=1" % ENV_ACCEPT
    license_text = read_license(font_dir)
    if license_text is None:
        stderr.write("%s: %s が無いか上流と一致しない (SHA-256 %s のはず)\n"
                     % (os.path.basename(__file__), os.path.join(font_dir, LICENSE_NAME),
                        LICENSE_SHA256[:16]))
        return False, None
    is_tty = False
    try:
        is_tty = stdin.isatty()
    except (AttributeError, ValueError):
        is_tty = False
    if not is_tty:
        stderr.write("%s: 対話できない (stdin が端末でない) — ライセンスに同意するには "
                     "%s=1 を付ける\n" % (os.path.basename(__file__), ENV_ACCEPT))
        return False, None
    if ask_license(font_dir, license_text, stdin, stdout):
        return True, "interactive y"
    return False, None


def download(url, stderr, opener=None):
    """zip 等を丸ごとメモリに取る。urllib の既定 (プロキシ環境変数) に任せる。"""
    stderr.write("取得中: %s\n" % url)
    req = urllib.request.Request(url, headers={"User-Agent": "os32-fetch-fonts/1"})
    open_fn = opener.open if opener is not None else urllib.request.urlopen
    with open_fn(req, timeout=120) as resp:
        return resp.read()


def extract(zip_bytes, font_dir, stderr, zip_sha256=None, members=None, extra_outputs=()):
    """zip と指定メンバーの SHA-256 を全て照合してから置く。失敗は例外。"""
    expected = ZIP_SHA256 if zip_sha256 is None else zip_sha256
    got = sha256_bytes(zip_bytes)
    if got != expected:
        raise ValueError("zip の SHA-256 が合わない: 期待 %s / 実測 %s (%d バイト)"
                         % (expected, got, len(zip_bytes)))
    zf = zipfile.ZipFile(io.BytesIO(zip_bytes))
    names = set(zf.namelist())
    outputs = list(extra_outputs)
    for member, (name, digest) in (MEMBERS if members is None else members).items():
        if member not in names:
            raise ValueError("zip に %s が無い" % member)
        data = zf.read(member)
        got = sha256_bytes(data)
        if got != digest:
            raise ValueError("%s の SHA-256 が合わない: 期待 %s / 実測 %s" % (name, digest, got))
        outputs.append((name, data))
    os.makedirs(font_dir, exist_ok=True)
    for name, data in outputs:
        path = os.path.join(font_dir, name)
        tmp = path + ".part"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
        stderr.write("置いた: %s (%d バイト)\n" % (path, len(data)))


def run_inconsolata(args, stdout, stderr, fetch):
    # OFL: keep copyright and full license alongside the unmodified font.
    # IPA's explicit first-fetch consent policy applies only to IPA fonts.
    if args.print_zip_sha256:
        stdout.write(INCONSOLATA_ZIP_SHA256 + "\n")
        return RC_OK
    members = dict(INCONSOLATA_MEMBERS)
    members[OFL_NAME] = (OFL_NAME, OFL_SHA256)
    valid = fonts_ok(args.font_dir, members)
    if args.check:
        return RC_OK if valid else RC_FETCH
    if valid and not args.force:
        for name, _ in members.values():
            touch_if_stale(os.path.join(args.font_dir, name))
        return RC_OK
    try:
        fetch_fn = download if fetch is None else fetch
        if args.zip:
            with open(args.zip, "rb") as f:
                data = f.read()
        else:
            data = fetch_fn(INCONSOLATA_ZIP_URL, stderr)
        # A local license allows offline --zip use and avoids downloading on cache hits.
        ofl_path = os.path.join(args.font_dir, OFL_NAME)
        if args.ofl:
            with open(args.ofl, "rb") as f:
                ofl = f.read()
        elif os.path.isfile(ofl_path) and sha256_file(ofl_path) == OFL_SHA256:
            with open(ofl_path, "rb") as f:
                ofl = f.read()
        else:
            ofl = fetch_fn(OFL_URL, stderr)
        if sha256_bytes(ofl) != OFL_SHA256:
            raise ValueError("OFL の SHA-256 が合わない")
        extract(data, args.font_dir, stderr, INCONSOLATA_ZIP_SHA256,
                INCONSOLATA_MEMBERS, [(OFL_NAME, ofl)])
    except Exception as e:
        stderr.write("Inconsolata 取得失敗: %s — オフラインでは --family inconsolata --zip PATH --ofl PATH\n" % e)
        return RC_FETCH
    return RC_OK


def run_jfdot(args, stdout, stderr, fetch):
    if args.print_zip_sha256:
        stdout.write(JFDOT_ZIP_SHA256 + "\n")
        return RC_OK
    members = dict(JFDOT_MEMBERS)
    members[JFDOT_LICENSE_NAME] = (JFDOT_LICENSE_NAME, sha256_bytes(JFDOT_LICENSE_TEXT))
    valid = fonts_ok(args.font_dir, members)
    if args.check:
        return RC_OK if valid else RC_FETCH
    if valid and not args.force:
        for name, _ in members.values():
            touch_if_stale(os.path.join(args.font_dir, name))
        return RC_OK
    try:
        if args.zip:
            with open(args.zip, "rb") as f:
                data = f.read()
        else:
            fetch_fn = download if fetch is None else fetch
            data = fetch_fn(JFDOT_ZIP_URL, stderr)
        extract(data, args.font_dir, stderr, JFDOT_ZIP_SHA256, JFDOT_MEMBERS,
                [(JFDOT_LICENSE_NAME, JFDOT_LICENSE_TEXT)])
    except Exception as e:
        stderr.write("JF ドット東雲16 取得失敗: %s — オフラインでは --family jfdot-shinonome16 --zip PATH\n" % e)
        return RC_FETCH
    return RC_OK


def touch_if_stale(path):
    """make の依存 (tools/fetch_fonts.py) より古いときだけ mtime を進める。
    T7b-b3 から GUI の標準 set は配備の入力なので、揃っている取得物を毎回 touch
    すると作った媒体が入力より古く見える (check_artifacts.require_fresh)。"""
    if os.stat(path).st_mtime < os.stat(os.path.abspath(__file__)).st_mtime:
        os.utime(path, None)


def touch_fonts(font_dir):
    for _, (name, _) in MEMBERS.items():
        touch_if_stale(os.path.join(font_dir, name))


def run(argv, environ=None, stdin=None, stdout=None, stderr=None, fetch=None):
    """本体。試験から差し替えられるよう、環境・入出力・取得関数を引数で受ける。"""
    environ = os.environ if environ is None else environ
    stdin = sys.stdin if stdin is None else stdin
    stdout = sys.stdout if stdout is None else stdout
    stderr = sys.stderr if stderr is None else stderr

    ap = argparse.ArgumentParser(description="IPAex / Inconsolata / JF ドット東雲16 を取得する")
    ap.add_argument("--family", choices=("ipaex", "inconsolata", "jfdot-shinonome16"), default="ipaex")
    ap.add_argument("--ofl", metavar="PATH", help="Inconsolata の OFL 原文 (オフライン用)")
    ap.add_argument("--font-dir", default=FONT_DIR, help="置き先 (既定 assets/fonts)")
    ap.add_argument("--force", action="store_true", help="揃っていても取り直す")
    ap.add_argument("--zip", metavar="PATH", help="ダウンロードせずこの zip から展開する")
    ap.add_argument("--print-zip-sha256", action="store_true",
                    help="固定している zip の SHA-256 を出して終わる (CI の cache key)")
    ap.add_argument("--check", action="store_true",
                    help="揃っているかだけ見る (0 = 揃っている、1 = 揃っていない)")
    args = ap.parse_args(argv)

    if args.family == "jfdot-shinonome16":
        return run_jfdot(args, stdout, stderr, fetch)

    if args.family == "inconsolata":
        return run_inconsolata(args, stdout, stderr, fetch)

    if args.print_zip_sha256:
        stdout.write(ZIP_SHA256 + "\n")
        return RC_OK

    font_dir = args.font_dir
    if args.check:
        return RC_OK if fonts_ok(font_dir) else RC_FETCH

    if not args.force and fonts_ok(font_dir):
        touch_fonts(font_dir)
        return RC_OK

    ok, how = confirm_license(font_dir, environ, stdin, stdout, stderr)
    if not ok:
        stderr.write(GUIDE_LICENSE + "\n")
        return RC_LICENSE
    if how is not None:
        # 同意した事実を先に記録する (取得に失敗しても次回は聞かない)
        record_acceptance(font_dir, how)

    try:
        if args.zip:
            with open(args.zip, "rb") as f:
                zip_bytes = f.read()
        else:
            fetch_fn = fetch if fetch is not None else download
            zip_bytes = fetch_fn(ZIP_URL, stderr)
        extract(zip_bytes, font_dir, stderr)
    except Exception as e:  # urllib.error.URLError, OSError, ValueError, zipfile.BadZipFile
        stderr.write("%s: %s\n" % (os.path.basename(__file__), e))
        stderr.write("フォント取得失敗: %s に届かないか内容が違う — "
                     "手で取った zip (SHA-256 %s…) を `python3 tools/fetch_fonts.py --zip PATH` で渡す\n"
                     % (ZIP_URL, ZIP_SHA256[:16]))
        return RC_FETCH
    return RC_OK


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
