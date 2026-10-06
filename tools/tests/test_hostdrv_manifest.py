"""H4 (書く側): 配備マニフェスト `.deploy/manifest.txt` を書く規則。

票:   docs/archive/shell/TASK_H4.md §2-1 / §2-2 / §4-1 (M1 M2 M3 M4)
記録: tools/tests/h4_manifest_tdd.md

`tools/hostdrv_deploy.py` を**実物のまま import** し、HOSTDRV_DIR と PROJ_DIR
だけを一時ディレクトリへ向ける。配備定義 (deploy.yaml) も一時ディレクトリの
中身を指す合成物にするので、**実ファイル系 (/mnt/c/os32) には一切触らない**。

  python3 -B tools/tests/test_hostdrv_manifest.py [--mutate]

--mutate は**否定側**。この票の中心規則 (全件成功の後にだけ書く / 失敗したら
既にある名札を消す / 一時ファイル + 置き換え / --no-manifest で抑止) を崩した
版で試験が確かに落ちることを確かめる。落ちなければ試験が規則を見ていない。

make・エミュレータ・実配備には一切触れない。
"""
import errno
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools/tests"))

import hostdrv_deploy as hd          # noqa: E402
import deploy_protect as protect     # noqa: E402
import mutpar                        # noqa: E402

failures = 0
checks = 0


def check(cond, name):
    global failures, checks
    checks += 1
    print("  %s %s" % ("ok  " if cond else "FAIL", name), flush=True)
    if not cond:
        failures += 1


# --------------------------------------------------------------------------
#  足場: HOSTDRV_DIR と PROJ_DIR を一時ディレクトリへ向ける
# --------------------------------------------------------------------------

class Bench(object):
    """配備 1 回分の足場。実物の do_sync をそのまま回す。"""

    def __init__(self, tmp, names=("bin/a.bin", "bin/b.bin", "sys/c.bin")):
        self.tmp = pathlib.Path(tmp)
        self.root = self.tmp / "hostdrv"
        self.proj = self.tmp / "proj"
        self.root.mkdir(parents=True, exist_ok=True)
        (self.proj / "src").mkdir(parents=True, exist_ok=True)
        self.names = list(names)
        self.files = []
        for i, n in enumerate(self.names):
            src = self.proj / "src" / n.replace("/", "_")
            src.write_bytes(bytes(bytearray((j * 7 + i) & 0xFF
                                            for j in range(64 + i))))
            self.files.append({
                "host": os.path.relpath(str(src), str(self.proj)),
                "guest": "/" + n,
                "tags": ["t%d" % i],
            })
        self.cfg = {"filesystem": {"directories": ["/bin", "/sys"],
                                   "files": self.files}}

    def install(self):
        """モジュール側のグローバルを差し替える (実物の関数はそのまま)。"""
        self.saved = (hd.HOSTDRV_DIR, hd.PROJ_DIR, hd.load_deploy_yaml)
        hd.HOSTDRV_DIR = str(self.root)
        hd.PROJ_DIR = str(self.proj)
        hd.load_deploy_yaml = lambda: self.cfg

    def restore(self):
        hd.HOSTDRV_DIR, hd.PROJ_DIR, hd.load_deploy_yaml = self.saved

    def __enter__(self):
        self.install()
        return self

    def __exit__(self, *a):
        self.restore()
        return False

    # ---- 名札 ----
    def man(self):
        return self.root / ".deploy" / "manifest.txt"

    def man_tmp(self):
        return self.root / ".deploy" / "manifest.txt.tmp"

    def man_text(self):
        return self.man().read_text(encoding="utf-8")

    def put_stale(self, build="deadbee"):
        """前の世代の名札を置く。"""
        self.man().parent.mkdir(parents=True, exist_ok=True)
        self.man().write_text(
            "format=1\nbuild=%s\ngenerated=2000-01-01T00:00:00Z\ncount=0\n---\n"  # 旧形式
            % build, encoding="utf-8")


def parse(text):
    """名札を (ヘッダ dict, ファイル行のリスト) に割る。"""
    head, sep, body = text.partition("---\n")
    if not sep:
        return {}, None
    kv = {}
    for line in head.splitlines():
        if not line:
            continue
        k, _, v = line.partition("=")
        kv[k] = v
    lines = [l for l in body.split("\n") if l != ""]
    return kv, lines


# --------------------------------------------------------------------------
#  M1 — 全件成功の配備
# --------------------------------------------------------------------------

def case_m1(tmp):
    print("== M1: 全件成功の配備 -> 名札が書かれ、count と行数が一致する ==")
    with Bench(tmp / "m1") as b:
        ok = hd.do_sync()
        check(ok is True, "配備そのものは成功する")
        check(b.man().is_file(), "名札が書かれる")
        if not b.man().is_file():
            return
        kv, lines = parse(b.man_text())
        check(kv.get("format") == "2", "format=2 (票 TASK_KAPI_DATA_FIELDS)")
        off, ver = hd.kapi_json_layout()
        check(kv.get("kapi") == str(off),
              "kapi=%s は sdk/kapi.json の配置 (%d)" % (kv.get("kapi"), off))
        check(kv.get("kapi_version") == str(ver),
              "kapi_version=%s は sdk/kapi.json の版 (%d)" % (kv.get("kapi_version"), ver))
        check("build" in kv and kv["build"] != "", "build= がある")
        check(" " not in kv.get("build", " "), "build に空白が無い")
        check(kv.get("generated", "").endswith("Z"),
              "generated は UTC (末尾 Z)")
        check(kv.get("count") == str(len(lines)),
              "count=%s と行数 %d が一致する" % (kv.get("count"), len(lines)))
        check(len(lines) == len(b.names),
              "配備した %d 件がすべて載る (いまは %d 行)"
              % (len(b.names), len(lines)))

        # 1 行 1 ファイル: パス サイズ CRC8桁 mtime、区切りは空白 1 つ
        good = True
        for line in lines:
            parts = line.split(" ")
            if len(parts) != 4:
                good = False
                break
            path, size, crc, mtime = parts
            dest = b.root / path
            if path.startswith("/") or ".." in path.split("/"):
                good = False
            if not dest.is_file():
                good = False
                break
            if int(size) != dest.stat().st_size:
                good = False
            if len(crc) != 8 or crc != crc.lower():
                good = False
            if int(crc, 16) != zlib.crc32(dest.read_bytes()) & 0xFFFFFFFF:
                good = False
            if int(mtime) != int(dest.stat().st_mtime):
                good = False
        check(good, "各行が パス/サイズ/CRC8桁小文字/mtime で、実物と一致する")
        check(lines == sorted(lines), "行は並びが決まっている (再現性)")
        check(not b.man_tmp().exists(), "一時ファイルが残らない")

        # 2 回目 (全件スキップ) でも名札は書き直される
        ok = hd.do_sync()
        kv2, lines2 = parse(b.man_text())
        check(ok is True and lines2 == lines,
              "同一内容の再配備でも名札は全件を載せる (スキップも載る)")


# --------------------------------------------------------------------------
#  M2 — 1 件でも失敗した配備
# --------------------------------------------------------------------------

def case_m2(tmp):
    print("== M2: 1 件でも失敗した配備 -> 書かない。既にある名札は消す ==")
    with Bench(tmp / "m2") as b:
        b.put_stale()
        check(b.man().is_file(), "(前提) 前の世代の名札がある")

        real = protect.check_dest
        victim = "/" + b.names[1]

        def boom(root, guest, host_src=None):
            if guest == victim:
                raise protect.ProtectError("試験が注入した失敗")
            return real(root, guest, host_src)

        protect.check_dest = boom
        try:
            ok = hd.do_sync()
        finally:
            protect.check_dest = real

        check(ok is False, "失敗を握り潰さない (do_sync が False)")
        check(not b.man().exists(),
              "**既にある名札を消す** (古い名札を「配備済み」と誤読させない)")
        check(not b.man_tmp().exists(), "一時ファイルも残らない")


# --------------------------------------------------------------------------
#  M3 — 書き込みの途中で失敗
# --------------------------------------------------------------------------

def case_m3(tmp):
    print("== M3: 名札の書き込み中に失敗 -> 中途半端な名札が残らない ==")

    # (a) 置き換え (os.replace) が落ちる
    with Bench(tmp / "m3a") as b:
        b.put_stale()
        real = os.replace

        def boom(a, c):
            raise OSError(errno.EIO, "試験が注入した置き換え失敗")

        os.replace = boom
        try:
            ok = hd.do_sync()
        finally:
            os.replace = real
        check(ok is False, "(a) 置き換えの失敗を握り潰さない")
        check(not b.man().exists(), "(a) 中途半端な名札が残らない")
        check(not b.man_tmp().exists(), "(a) 一時ファイルを片づける")

    # (b) 行を作る途中で配備先が消える (CRC が取れない)
    with Bench(tmp / "m3b") as b:
        b.put_stale()
        real = hd.manifest_line

        def boom(rel, dest):
            if rel.endswith("b.bin"):
                raise OSError(errno.EIO, "試験が注入した読み取り失敗")
            return real(rel, dest)

        hd.manifest_line = boom
        try:
            ok = hd.do_sync()
        finally:
            hd.manifest_line = real
        check(ok is False, "(b) 途中の失敗を握り潰さない")
        check(not b.man().exists(), "(b) 中途半端な名札が残らない")
        check(not b.man_tmp().exists(), "(b) 一時ファイルを片づける")

    # (d) **最終パスを書き込みで開かない** — 一時ファイル + 置き換えの観測窓。
    #     「落ちたあと名札が無い」だけでは直書きと区別がつかない (直書きでも
    #     後始末で消えるので緑になる)。開いたパスそのものを数える。
    with Bench(tmp / "m3d") as b:
        opened_w = []
        real_open = open

        def spy(path, mode="r", *a, **kw):
            if "w" in mode or "a" in mode or "+" in mode:
                opened_w.append(os.path.abspath(str(path)))
            return real_open(path, mode, *a, **kw)

        hd.open = spy
        try:
            ok = hd.do_sync()
        finally:
            del hd.open
        check(ok is True, "(d) 配備は成功する")
        check(os.path.abspath(str(b.man_tmp())) in opened_w,
              "(d) 一時ファイルを書き込みで開く")
        check(os.path.abspath(str(b.man())) not in opened_w,
              "(d) **最終パスは書き込みで開かない** (中途半端な名札を読ませない)")

    # (c) 配備対象のパスに空白がある -> 書く側が拒否する (§2-1)
    with Bench(tmp / "m3c", names=("bin/a.bin",)) as b:
        src = b.proj / "src" / "sp"
        src.write_bytes(b"x")
        b.files.append({"host": os.path.relpath(str(src), str(b.proj)),
                        "guest": "/bin/with space.bin", "tags": ["sp"]})
        ok = hd.do_sync()
        check(ok is False, "(c) パスに空白のある配備を拒否する")
        check(not b.man().exists(), "(c) 名札を書かない")


# --------------------------------------------------------------------------
#  M4 — --no-manifest
# --------------------------------------------------------------------------

def case_m4(tmp):
    print("== M4: --no-manifest -> 書かない。配備自体は今までどおり ==")
    with Bench(tmp / "m4") as b:
        ok = hd.do_sync(write_manifest=False)
        check(ok is True, "配備そのものは成功する")
        check(not b.man().exists(), "名札を書かない")
        for n in b.names:
            check((b.root / n).is_file(), "配備自体は今までどおり: %s" % n)

    # 古い名札が残っていたら消す — 残すと「新しいファイル + 古い名札」で
    # `--expect-build <古い ID>` が**一致**してしまい、H4 が防ぐ事故が裏返る。
    with Bench(tmp / "m4b") as b:
        b.put_stale()
        ok = hd.do_sync(write_manifest=False)
        check(ok is True, "(b) 配備は成功する")
        check(not b.man().exists(),
              "(b) 抑止しても**古い名札は消す** (嘘の世代を残さない)")


# --------------------------------------------------------------------------
#  タグ絞り込み — 部分配備で名札を書かない
# --------------------------------------------------------------------------

def case_tag(tmp):
    print("== 追加: --tag の部分配備では名札を書かず、古い名札を消す ==")
    with Bench(tmp / "tag") as b:
        b.put_stale()
        ok = hd.do_sync(tag_filter="t0")
        check(ok is True, "部分配備は成功する")
        check((b.root / b.names[0]).is_file(), "対象の 1 件は配備される")
        check(not (b.root / b.names[1]).is_file(), "対象外は配備されない")
        check(not b.man().exists(),
              "部分配備の後に名札を残さない (全体の世代を表せない)")


# --------------------------------------------------------------------------
#  build 名札そのもの
# --------------------------------------------------------------------------

def case_build_id(tmp):
    print("== 追加: build 名札は <短い SHA>(+dirty)、空白なし ==")
    bid = hd.build_id()
    check(isinstance(bid, str) and bid != "", "build_id が空でない")
    check(" " not in bid and "\t" not in bid and "\n" not in bid,
          "build_id に空白が無い (key=value 1 行に収まる)")
    check(len(bid) < 64, "build_id は 64 文字未満 (読む側の上限)")
    check(bid.endswith("+dirty") or bid == "unknown" or
          all(c in "0123456789abcdef" for c in bid),
          "build_id は 16 進の短い SHA か +dirty か unknown: %r" % bid)


# --------------------------------------------------------------------------
#  変異 (否定側)
# --------------------------------------------------------------------------

MUTATIONS = [
    # 変異 1: 失敗しても古い名札を消さない版 (§2-2 の否定側)。
    ("stale_manifest_kept",
     "def sync_failed():",
     "def sync_failed():\n    return False\n\n\ndef _unused_sync_failed():"),
    # 変異 2: 一時ファイルを使わず直接書く版 (§2-2 の否定側)。
    ("no_temp_file",
     "    tmp = manifest_tmp_path()",
     "    tmp = manifest_path()"),
    # 変異 3: --no-manifest でも書いてしまう版 (M4 の否定側)。
    ("no_manifest_ignored",
     "    if not write_manifest:",
     "    if False:"),
    # 変異 4: count を行数と別に数える版 (M1 の否定側)。
    ("count_not_lines",
     "    head = MANIFEST_HEAD_FMT % (MANIFEST_FORMAT, build, generated,\n"
     "                                kapi_off, kapi_ver, len(lines))",
     "    head = MANIFEST_HEAD_FMT % (MANIFEST_FORMAT, build, generated,\n"
     "                                kapi_off, kapi_ver, len(lines) + 1)"),
    # 変異 K1: 配備物のヘッダを見ず kapi.json の値を書く版 (旧成果物の混入を
    #          「確かめた」ことにしてしまう)。
    ("kapi_not_from_binaries",
     "    kapi_off, why = deployed_kapi_layout(deployed)",
     "    kapi_off, why = kapi_json_layout()[0], None"),
    # 変異 K2: 値の食い違いを見逃す版。
    ("kapi_mixed_allowed",
     "    if len(seen) != 1:",
     "    if False:"),
    # 変異 K3: v3 でない (配置を持たない) 成果物を見逃す版。
    ("kapi_old_header_allowed",
     "        if h['version'] < os32x_hdr.OS32X_HDR_VERSION or off is None:",
     "        if off is None:\n            continue\n        if False:"),
    # 変異 5: パスの空白を見逃す版 (§2-1 の否定側)。
    ("space_in_path_allowed",
     "        if not manifest_path_ok(rel):",
     "        if False:"),
]


# --------------------------------------------------------------------------
#  K — KAPI の配置 (票 TASK_KAPI_DATA_FIELDS)
# --------------------------------------------------------------------------

def os32x_blob(version, kapi_off, body=b"\x90" * 32):
    """OS32X のヘッダを付けた最小の中身。version 2 は 44 バイト (kapi 無し)。"""
    import struct
    if version == hd.os32x_hdr.OS32X_HDR_VERSION:
        h = hd.os32x_hdr
        hdr = struct.pack("<15I", h.OS32X_MAGIC, h.OS32X_HDR_SIZE,
                          h.OS32X_HDR_VERSION, 0, 0, len(body), 0, 0, 0,
                          h.OS32X_MIN_API, h.OS32X_APP_LOAD_ADDR, kapi_off,
                          h.OS32_KAPI_ABI_GENERATION,
                          h.OS32_MEMORY_LAYOUT_GENERATION, 0)
    elif version >= 3:
        hdr = struct.pack("<12I", 0x4F533332, 48, 3, 0, 0, len(body), 0, 0, 0,
                          63, 0x500000, kapi_off)
    else:
        hdr = struct.pack("<11I", 0x4F533332, 44, 2, 0, 0, len(body), 0, 0, 0,
                          39, 0x500000)
    return hdr + body


def case_kapi(tmp):
    print("== K: 名札の kapi= は配備した OS32X のヘッダから取る ==")
    off, ver = hd.kapi_json_layout()

    # K1: 現行形式で配置が一致 -> kapi= に書く
    with Bench(tmp / "k1") as b:
        pathlib.Path(b.proj / b.files[0]["host"]).write_bytes(os32x_blob(hd.os32x_hdr.OS32X_HDR_VERSION, off))
        pathlib.Path(b.proj / b.files[1]["host"]).write_bytes(os32x_blob(hd.os32x_hdr.OS32X_HDR_VERSION, off))
        ok = hd.do_sync()
        check(ok is True, "K1 現行形式・一致なら配備は成功する")
        kv, _lines = parse(b.man_text()) if b.man().is_file() else ({}, None)
        check(kv.get("kapi") == str(off), "K1 kapi=%d" % off)

    # K2: v2 の成果物が混ざる -> 名札を書かない (配備失敗)
    with Bench(tmp / "k2") as b:
        b.put_stale()
        pathlib.Path(b.proj / b.files[0]["host"]).write_bytes(os32x_blob(hd.os32x_hdr.OS32X_HDR_VERSION, off))
        pathlib.Path(b.proj / b.files[1]["host"]).write_bytes(os32x_blob(2, 0))
        ok = hd.do_sync()
        check(ok is False, "K2 旧ヘッダ (v2) が混ざれば配備は失敗")
        check(not b.man().is_file(), "K2 名札を書かない (古い名札も残さない)")

    # K3: 配置の食い違い -> 名札を書かない
    with Bench(tmp / "k3") as b:
        pathlib.Path(b.proj / b.files[0]["host"]).write_bytes(os32x_blob(hd.os32x_hdr.OS32X_HDR_VERSION, off))
        pathlib.Path(b.proj / b.files[1]["host"]).write_bytes(os32x_blob(hd.os32x_hdr.OS32X_HDR_VERSION, off - 4))
        ok = hd.do_sync()
        check(ok is False, "K3 配置が食い違えば配備は失敗")
        check(not b.man().is_file(), "K3 名札を書かない")

    # K4: 全部同じでも kapi.json と違う -> 名札を書かない
    with Bench(tmp / "k4") as b:
        pathlib.Path(b.proj / b.files[0]["host"]).write_bytes(os32x_blob(hd.os32x_hdr.OS32X_HDR_VERSION, off + 4))
        ok = hd.do_sync()
        check(ok is False, "K4 sdk/kapi.json と違う配置なら配備は失敗")
        check(not b.man().is_file(), "K4 名札を書かない")


MUT_TARGET = "tools/hostdrv_deploy.py"
SELF = "tools/tests/test_hostdrv_manifest.py"


def one_mutation(item):
    """変異 1 本: 写しの木の hostdrv_deploy.py を壊し、写しの中のこの試験を
    流し直す (実物は読むだけ)。(印字, 見逃し) を返す。"""
    name, old, new = item
    original = (ROOT / MUT_TARGET).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-24s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-hostdrv-man-mut-") as td:
        rc = mutpar.run_script_in_tree(
            ROOT, td, {MUT_TARGET: original.replace(old, new, 1)}, SELF,
            capture_output=True, timeout=300).returncode
    if rc == 0:
        return ("MUTATE %-24s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    return "MUTATE %-24s RED (期待どおり落ちた)" % name, 0


def run_mutations():
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, MUTATIONS, ("control", "", ""))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-h4-host-") as td:
        td = pathlib.Path(td)
        case_m1(td)
        case_m2(td)
        case_m3(td)
        case_m4(td)
        case_tag(td)
        case_build_id(td)
        case_kapi(td)

    print("\n%d checks, %d failures" % (checks, failures))
    if failures:
        sys.exit(1)
    if "--mutate" in sys.argv:
        sys.exit(1 if run_mutations() else 0)
    sys.exit(0)
