#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CD のパッケージが配備マニフェストと一致しているか (make check-packages-host)

CD インストール (userland/system/cdinst.c) の .PKG は、配備の正典
(build/core.yaml + userland/deploy.yaml) のタグから tools/mkpkg.py --plan が
作る (構成は build/packages.yaml)。2026-09-24 までは一覧を手で写していて、
配備 179 本のうち 27 本 (gshell / libos32gui.shlib / 既定フォント …) が CD に
入っていなかった。ここで見るのは:

  1. 構成の検査 (実物): 配備マニフェストの各行がちょうど 1 つのパッケージに
     当たるか、除外に理由があるか、userland/tests/ 由来が test タグか
  2. 否定側: 当たらないタグ / 2 つに当たる / tests 由来が programs /
     理由の無い除外 / 古い除外 / ゲストパスの重複 をそれぞれ報告するか
  3. 分割: 128 項目を超えると NAME, NAME2, … に分かれ、どれも 128 以下
  4. 媒体 (実物): 実物の mkpkg で作った PKG を読み戻し、配備の全ファイル
     (除外以外) + 媒体だけの物が**同じバイト列で**ちょうど 1 回ずつ入っているか
  5. ISO (実物): images/os32_install.iso の中の PKG を取り出して 4 と同じ検査
     (isoinfo が無ければ SKIP と表示する)
  6. cdinst.c のベース名が構成の名前と一致し、展開の順が MINIMAL → GUI →
     NORMAL → DEBUG で、pkg.h の上限が mkpkg と一致するか
  7. 起動 FD: build/image.mk が一覧を手で持たず、FD の中身が BOOT + MINIMAL
     (+ fd.only) と**等しい**か。構成 (fd_plan) と、実物のイメージ
     (images/os32_boot.img = 2HD の D88 のもと、images/os32_boot144.img) を
     FAT12 として読み戻してバイト列で見る。否定側 (8.3 違反・理由なし・古い
     rename) も。空きを表示し、**FD_2HD_MIN_FREE_KB を切ったら落ちる** (否定側:
     閾値の 1KB 下を落とす)。配布物の D88 (images/os32_boot.d88) のセクタを
     読み戻し、RAW (os32_boot.img) と一致するか (否定側: D88 を 1 セクタ壊すと落ちる)
  8. 8.3 の短い名前へのフォールバックは FD (FAT) のときだけ: 実物の
     kernel/boot_font.c をホストで取り込み、HDD (ext2) では正規名が無ければ
     失敗のまま (短い名前を読まない) であることと、その否定側 (FS を見ない変異)
  9. FD → install → HDD: install.c の逆変換表 (fd_renames / fd_only) が
     packages.yaml の fd.rename / fd.only と一致し、実物の install.c を
     ホストで回すと (tools/tests/install_fresh_host.c の段 fdset) HDD に
     出来るファイルの集合と大きさが MINIMAL と等しい。否定側: 逆変換を
     当てない変異で不一致になる

先に make all を通してから実行すること (4 と 5 は成果物を読む)。
"""

import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import deploy_manifests as dm  # noqa: E402
import mkpkg  # noqa: E402

PLAN = os.path.join(ROOT, 'build', 'packages.yaml')
ISO = os.path.join(ROOT, 'images', 'os32_install.iso')
CDINST = os.path.join(ROOT, 'userland', 'system', 'cdinst.c')
PKG_H = os.path.join(ROOT, 'userland', 'lib', 'rt', 'pkg.h')

fails = []

D88 = os.path.join(ROOT, 'images', 'os32_boot.d88')
BOOT_FONT_C = os.path.join(ROOT, 'kernel', 'boot_font.c')
FD_IMAGES = [('2HD', os.path.join(ROOT, 'images', 'os32_boot.img'),
              'boot/loader_fat_new.bin'),
             ('1.44MB', os.path.join(ROOT, 'images', 'os32_boot144.img'),
              'boot/loader_fat144.bin')]
IMAGE_MK = os.path.join(ROOT, 'build', 'image.mk')

# 起動 FD の空きの下限 (KB)。2HD (1232KB) と 1.44MB の両方に当てる (1.44MB は
# 同じ中身で 2HD より約 200KB 広いので、実際に効くのは 2HD)。
# 2026-09-25 にカーネルが CD 読みの高速化で約 4KB 増え、2HD の FD が前触れなく
# 作れなくなった (mkfat12「ディスク容量不足」、残り 55KB → 溢れ)。既定フォント
# (184KB) を MINIMAL から NORMAL へ移して空けたうえで、ここで見張る。
# 64KB = カーネルの 1 回の増分 (数 KB) の 10 倍以上 + 小さなコマンド数本。これを
# 切ったら MINIMAL に足す物を見直すか、何を FD から外すかを決める合図
# (docs/08_build.md の「起動 FD と MINIMAL」)。
FD_MIN_FREE_KB = 64

# MINIMAL の外部コマンド (タグ base)。MINIMAL =「起動して、HDD に入れて、壊れた
# ときに直して、残りを取ってこられる」レスキュー兼インストーラ (2026-09-25、
# ユーザー決定。build/packages.yaml の冒頭)
MINIMAL_BASE_CMDS = ['less', 'grep', 'hexdump', 'cfg']
MINIMAL_SBIN = ['install', 'cdinst', 'hsync']
# 2026-09-25 に MINIMAL (= 起動 FD) から NORMAL へ移したコマンド。2HD の FD の空きのため
MOVED_TO_NORMAL_CMDS = ['more', 'find', 'sort', 'head', 'tail', 'wc', 'tee', 'touch',
                        'sleep', 'diff', 'du', 'cal', 'man', 'sndctl']
FONT_GUEST = '/sys/font/default.kcgfont'
GUI_FILES = ['/bin/gshell.bin', '/sys/lib/libos32gui.shlib',
             '/usr/bin/filer.bin', '/usr/bin/edit_gui.bin', '/usr/bin/about.bin']


def check(cond, label):
    print(f"  {'ok  ' if cond else 'FAIL'} {label}")
    if not cond:
        fails.append(label)
    return cond


# ---------------------------------------------------------------- 1. 実物の構成

def case_real_plan():
    print("case 1: 実物の構成 (build/packages.yaml + 配備マニフェスト)")
    plan = mkpkg.load_plan(PLAN)
    resolved, problems = mkpkg.expand_plan(plan, ROOT)
    for p in problems:
        print(f"       {p}")
    check(problems == [], "振り分けに問題が無い")
    for name, _, _, files in resolved:
        nent = len(files) + len(mkpkg.entry_dirs(g for g, _ in files))
        check(nent <= mkpkg.PKG_MAX_ENTRIES,
              f"{name.upper()}.PKG: {len(files)} files, {nent} entries <= "
              f"{mkpkg.PKG_MAX_ENTRIES}")
    # Minimal =「CUI のシェル + 基本コマンド」(cdinst の選択肢 1 の文言)
    minimal = {g for n, _, _, fs in resolved if n == 'minimal' for g, _ in fs}
    need = ['/sys/shell.bin', '/boot/vmkernel.lz4', '/sys/unicode.bin',
            '/etc/filetypes', '/etc/settings.tsv', '/etc/settings.db'] + \
        ['/bin/%s.bin' % c for c in MINIMAL_BASE_CMDS] + \
        ['/sbin/%s.bin' % c for c in MINIMAL_SBIN]
    lack = [g for g in need if g not in minimal]
    check(not lack, f"MINIMAL にシェル・インストーラ・hsync・直す道具が入っている (欠け {lack})")
    # MINIMAL の外部コマンドはちょうど上の顔ぶれ (足すなら FD の空きと相談して表も直す)
    cmds = sorted(g for g in minimal if g.startswith(('/bin/', '/sbin/')))
    want = sorted(['/bin/%s.bin' % c for c in MINIMAL_BASE_CMDS] +
                  ['/sbin/%s.bin' % c for c in MINIMAL_SBIN])
    check(cmds == want, f"MINIMAL のコマンドの顔ぶれ {cmds}")
    # 既定フォント (184KB) は NORMAL (2026-09-25)。MINIMAL と起動 FD には入れない —
    # 無ければカーネルは本体のフォント ROM で描く (kernel/boot_font.c、drivers/kcg.c)
    normal = {g for n, _, _, fs in resolved if n.startswith('normal') for g, _ in fs}
    check(FONT_GUEST not in minimal and FONT_GUEST in normal,
          f"既定フォント {FONT_GUEST} は NORMAL にあり MINIMAL に無い")
    moved = ['/bin/%s.bin' % c for c in MOVED_TO_NORMAL_CMDS]
    check(all(g in normal for g in moved),
          f"移したコマンドは NORMAL にある (無い {[g for g in moved if g not in normal]})")
    # GUI は MINIMAL に無く、GUI.PKG が gui タグの行とちょうど一致する
    gui = sorted(g for n, _, _, fs in resolved if n.startswith('gui') for g, _ in fs)
    check(not (set(GUI_FILES) & minimal), "MINIMAL に GUI (gshell / shlib / GUI アプリ) が無い")
    man = dm.load_merged(dm.CORE_MANIFEST_RELPATHS)
    tagged = sorted(g for e in man['filesystem']['files'] if 'gui' in (e.get('tags') or [])
                    for _, g in dm.resolve_entry(e, ROOT))
    check(gui == tagged, f"GUI.PKG の中身 = gui タグの行 ({len(gui)} 件)")
    check(gui == sorted(GUI_FILES), f"GUI.PKG の顔ぶれ {gui}")
    order = [n for n, _, _, _ in resolved]
    check([n for n in order if not n[-1].isdigit()] == ['boot', 'minimal', 'gui', 'normal', 'debug'],
          f"パッケージの並びが依存の順 {order}")
    return plan, resolved


# ---------------------------------------------------------------- 2. 否定側

def fake_manifest(files, loader='boot/loader_hdd.bin'):
    for e in files:
        e.setdefault('_manifest', 'fake.yaml')
    return {'boot': {'loader': loader},
            'filesystem': {'directories': [], 'files': files}}


def fake_plan(exclude=None, extra_pkg=None):
    pk = {
        'boot': {'type': 'boot', 'files': [
            {'host': 'boot/loader_hdd.bin', 'guest': '/boot/loader_hdd.bin'}]},
        'minimal': {'tags': ['core']},
        'normal': {'tags': ['programs', 'docs', 'data']},
        'debug': {'tags': ['test']},
    }
    if extra_pkg:
        pk.update(extra_pkg)
    return {'packages': pk, 'exclude': exclude or []}


def problems_of(plan, files, loader='boot/loader_hdd.bin'):
    _, probs = mkpkg.plan_packages(plan, ROOT, fake_manifest(files, loader))
    return probs


def case_negative():
    print("case 2: 否定側 (作った配備マニフェストで)")
    base = [{'host': 'userland/shell.bin', 'guest': '/sys/shell.bin', 'tags': ['core']}]

    p = problems_of(fake_plan(), base)
    check(p == [], "正常系は問題なし")

    p = problems_of(fake_plan(), base + [
        {'host': 'userland/cmds/ls.bin', 'guest': '/bin/ls.bin', 'tags': ['tools']}])
    check(any('当たらないタグ' in x for x in p), "当たらないタグを報告する")

    p = problems_of(fake_plan(), base + [
        {'host': 'userland/cmds/ls.bin', 'guest': '/bin/ls.bin'}])
    check(any('0 個' in x for x in p), "タグの無い行を報告する")

    p = problems_of(fake_plan(), base + [
        {'host': 'userland/x.bin', 'guest': '/bin/x.bin', 'tags': ['core', 'test']}])
    check(any('2 個' in x for x in p), "2 つのパッケージに当たる行を報告する")

    p = problems_of(fake_plan(), base + [
        {'host': 'userland/tests/foo.bin', 'guest': '/usr/bin/foo.bin',
         'tags': ['programs']}])
    check(any('userland/tests/' in x and 'test' in x for x in p),
          "userland/tests/ 由来が programs なのを報告する")

    p = problems_of(fake_plan(extra_pkg={'extra': {'tags': ['core']}}), base)
    check(any('2 つのパッケージ' in x for x in p), "1 つのタグを 2 つのパッケージが取るのを報告する")

    p = problems_of(fake_plan(exclude=[{'guest': '/sys/shell.bin'}]), base)
    check(any('理由' in x for x in p), "理由の無い除外を報告する")

    p = problems_of(fake_plan(exclude=[{'guest': '/nope', 'reason': 'x'}]), base)
    check(any('古い除外' in x for x in p), "どれにも当たらない除外を報告する")

    plan = fake_plan(exclude=[{'guest': '/sys/shell.bin', 'reason': '試験'}])
    pkgs, p = mkpkg.plan_packages(plan, ROOT, fake_manifest(base))
    inside = [g for _, _, fs in pkgs for g, _ in fs]
    check(p == [] and '/sys/shell.bin' not in inside,
          "理由つきの除外はどのパッケージにも入らず、問題にもならない")

    p = problems_of(fake_plan(), base + [
        {'host': 'userland/sh.bin', 'guest': '/sys/shell.bin', 'tags': ['programs']}])
    check(any('2 回' in x for x in p), "ゲストパスの重複を報告する")

    p = problems_of(fake_plan(), base, loader='boot/other.bin')
    check(any('boot.loader' in x for x in p), "BOOT が配備の boot.loader を含まないのを報告する")

    p = problems_of(fake_plan(extra_pkg={'toolong8': {'tags': ['x']}}), base)
    check(any('文字を超える' in x for x in p), "8 文字の名前を報告する (連番の余地)")

    # glob の行: exclude: のベース名は配備と同じく外れる
    g = [{'host': 'userland/cmds/*.bin', 'guest': '/bin/', 'type': 'glob',
          'exclude': ['more.bin'], 'tags': ['programs']}]
    pkgs, p = mkpkg.plan_packages(fake_plan(), ROOT, fake_manifest(base + g))
    inside = [gp for _, _, fs in pkgs for gp, _ in fs]
    check('/bin/more.bin' not in inside and '/bin/less.bin' in inside,
          "glob の exclude: は配備と同じく外れる")


# ---------------------------------------------------------------- 3. 分割

def case_split():
    print("case 3: 分割")
    files = [(f"/usr/man/p{i:03d}.1", "x") for i in range(200)] + \
            [(f"/bin/c{i:03d}.bin", "x") for i in range(60)]
    parts = mkpkg.split_files('normal', files)
    names = [n for n, _ in parts]
    check(names == ['normal', 'normal2', 'normal3'], f"名前 {names}")
    ok = all(len(fs) + len(mkpkg.entry_dirs(g for g, _ in fs)) <= mkpkg.PKG_MAX_ENTRIES
             for _, fs in parts)
    check(ok, "どの分割も 128 項目以下")
    check([f for _, fs in parts for f in fs] == files, "順序を保ち、欠けも重複も無い")
    parts = mkpkg.split_files('minimal', files[:10])
    check([n for n, _ in parts] == ['minimal'], "小さければ分けない")
    # ちょうど 128 (126 ファイル + /usr と /usr/man)
    exact = [(f"/usr/man/q{i:03d}.1", "x") for i in range(126)]
    check(len(mkpkg.split_files('n', exact)) == 1, "ちょうど 128 項目は 1 本")
    check(len(mkpkg.split_files('n', exact + [("/usr/man/z.1", "x")])) == 2,
          "129 項目で 2 本")
    # 読み手 (read_pkg) の LZSS 展開が書き手と往復する
    tmp = tempfile.mkdtemp(prefix='pkgrt_')
    try:
        a = os.path.join(tmp, 'a')
        b = os.path.join(tmp, 'b')
        open(a, 'wb').write(bytes((i * 7) & 0xFF for i in range(9000)) + b'abc' * 3000)
        open(b, 'wb').write(os.urandom(5000))
        blob = mkpkg.build_pkg('rt', 1, [('/x/a', a), ('/b', b)], True, 0)
        out = os.path.join(tmp, 'RT.PKG')
        open(out, 'wb').write(blob)
        hdr, ents = mkpkg.read_pkg(out)
        files = {g: d for g, t, d in ents if t == mkpkg.PKG_TYPE_FILE}
        check(hdr['flags'] & mkpkg.PKG_FLAG_LZSS and
              files == {'/x/a': open(a, 'rb').read(), '/b': open(b, 'rb').read()},
              "LZSS の PKG を読み戻すと元のバイト列")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------- 4/5. 媒体

def expected_files(plan):
    """[(guest, host_abs)] — 配備マニフェスト (除外以外) + 構成に直に書いた物"""
    excl = {e['guest'] for e in plan.get('exclude') or []}
    want = {}
    man = dm.load_merged(dm.CORE_MANIFEST_RELPATHS)
    for e in man['filesystem']['files']:
        for host, guest in dm.resolve_entry(e, ROOT):
            if guest not in excl:
                want[guest] = os.path.join(ROOT, host)
    for pdef in plan['packages'].values():
        for f in (pdef or {}).get('files') or []:
            want[f['guest']] = os.path.join(ROOT, f['host'])
    return want


def verify_media(pkg_paths, want, label):
    """PKG 群を読み戻し、want とちょうど一致するか"""
    got = {}
    dup = []
    bad_bytes = []
    ok = True
    for path in pkg_paths:
        try:
            hdr, entries = mkpkg.read_pkg(path)
        except ValueError as e:
            check(False, f"{label}: {os.path.basename(path)} が読めない: {e}")
            ok = False
            continue
        if hdr['entry_count'] > mkpkg.PKG_MAX_ENTRIES:
            check(False, f"{label}: {os.path.basename(path)} {hdr['entry_count']} 項目")
            ok = False
        for g, t, data in entries:
            if t != mkpkg.PKG_TYPE_FILE:
                continue
            if g in got:
                dup.append(g)
            got[g] = os.path.basename(path)
            h = want.get(g)
            if h is not None:
                with open(h, 'rb') as f:
                    if f.read() != data:
                        bad_bytes.append(g)
    missing = sorted(set(want) - set(got))
    extra = sorted(set(got) - set(want))
    for g in missing[:20]:
        print(f"       missing {g}")
    for g in extra[:20]:
        print(f"       extra   {g}")
    for g in bad_bytes[:20]:
        print(f"       bytes   {g}")
    ok &= check(not missing, f"{label}: 配備の全ファイルが入っている ({len(want)} 件、欠け {len(missing)})")
    ok &= check(not extra, f"{label}: 余分な物が無い (余分 {len(extra)})")
    ok &= check(not dup, f"{label}: 重複が無い ({len(dup)})")
    ok &= check(not bad_bytes, f"{label}: 中身が同じバイト列 (違い {len(bad_bytes)})")
    return ok


def case_media(plan, resolved):
    print("case 4: 実物の mkpkg で作った PKG を読み戻す")
    want = expected_files(plan)
    missing_art = [h for h in want.values() if not os.path.isfile(h)]
    if missing_art:
        for h in missing_art[:10]:
            print(f"       成果物が無い: {os.path.relpath(h, ROOT)}")
        check(False, "成果物が揃っている (先に make all)")
        return None
    tmp = tempfile.mkdtemp(prefix='pkgtest_')
    try:
        # 古い PKG が消えることも見る
        open(os.path.join(tmp, 'FULL.PKG'), 'wb').write(b'stale')
        r = subprocess.run([sys.executable, '-B', os.path.join(ROOT, 'tools', 'mkpkg.py'),
                            '--plan', PLAN, '--output', tmp, '--base', ROOT],
                           capture_output=True, text=True)
        check(r.returncode == 0, f"mkpkg --plan が通る (rc={r.returncode})")
        if r.returncode != 0:
            print(r.stderr)
            return None
        names = sorted(os.listdir(tmp))
        check('FULL.PKG' not in names, "出力先の古い PKG を消す")
        check(names == sorted(n.upper() + '.PKG' for n, _, _, _ in resolved),
              f"PKG の顔ぶれ {names}")
        verify_media([os.path.join(tmp, n) for n in names if n != 'BOOT.PKG'],
                     {g: h for g, h in want.items()
                      if g not in {f['guest'] for f in plan['packages']['boot']['files']}},
                     "packages")
        hdr, _ = mkpkg.read_pkg(os.path.join(tmp, 'BOOT.PKG'))
        check(hdr['flags'] == 0, "BOOT.PKG は無圧縮 (cdinst が断る)")
        return {n: open(os.path.join(tmp, n), 'rb').read() for n in names}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def case_iso(plan, fresh):
    print("case 5: ISO の中の PKG")
    isoinfo = shutil.which('isoinfo')
    if not isoinfo:
        print("  SKIP isoinfo が無い (genisoimage パッケージ)")
        return
    if not os.path.isfile(ISO):
        check(False, f"{os.path.relpath(ISO, ROOT)} がある (先に make all)")
        return
    r = subprocess.run([isoinfo, '-f', '-i', ISO], capture_output=True, text=True)
    names = sorted(l.strip().lstrip('/').split(';')[0] for l in r.stdout.splitlines()
                   if l.strip())
    check(fresh is not None and names == sorted(fresh),
          f"ISO の顔ぶれが今の構成と同じ {names} (違えば make iso)")
    tmp = tempfile.mkdtemp(prefix='isotest_')
    try:
        paths = []
        for n in names:
            out = os.path.join(tmp, n)
            with open(out, 'wb') as f:
                subprocess.run([isoinfo, '-i', ISO, '-x', f'/{n};1'], stdout=f, check=True)
            paths.append(out)
            if fresh and n in fresh:
                check(open(out, 'rb').read() == fresh[n],
                      f"ISO の {n} が今作った物と同じバイト列 (違えば make iso)")
        boot = {f['guest'] for f in plan['packages']['boot']['files']}
        want = {g: h for g, h in expected_files(plan).items() if g not in boot}
        verify_media([p for p in paths if not p.endswith('BOOT.PKG')], want, "ISO")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------- 6. 消費側

def case_consumer(plan):
    print("case 6: cdinst.c / pkg.h との突き合わせ")
    src = open(CDINST, encoding='utf-8').read()
    bases = set(re.findall(r'#define\s+PKG_BASE_\w+\s+"(\w+)"', src))
    want = {n.upper() for n, p in plan['packages'].items()
            if (p or {}).get('type') != 'boot'}
    check(bases == want, f"cdinst のベース名 {sorted(bases)} = 構成 {sorted(want)}")
    check('BOOT.PKG' in src, "cdinst は BOOT.PKG を読む")
    seq = [m.group(1) for m in re.finditer(r'install_series\(PKG_BASE_(\w+)\)', src)]
    check(seq == ['MINIMAL', 'GUI', 'NORMAL', 'DEBUG'],
          f"cdinst の展開順 {seq} = MINIMAL → GUI → NORMAL → DEBUG")
    check('FULL.PKG' not in src and 'APPEND.PKG' not in src,
          "cdinst に古い FULL / APPEND が残っていない")
    m = re.search(r'#define\s+PKG_SERIES_MAX\s+(\d+)', src)
    check(m is not None and int(m.group(1)) == 9, "cdinst の連番上限 9 = mkpkg の上限")
    h = open(PKG_H, encoding='utf-8').read()
    mp = re.search(r'#define\s+PKG_MAX_PATH\s+(\d+)', h)
    me = re.search(r'#define\s+PKG_MAX_ENTRIES\s+(\d+)', h)
    check(mp and int(mp.group(1)) == mkpkg.PKG_MAX_PATH, "PKG_MAX_PATH が一致")
    check(me and int(me.group(1)) == mkpkg.PKG_MAX_ENTRIES, "PKG_MAX_ENTRIES が一致")


# ---------------------------------------------------------------- 7. 起動 FD

def fat12_files(path):
    """FAT12 イメージのファイルを {PATH (大文字): bytes} と空きバイトで返す

    BPB (オフセット 0x0B) から配置を読む。mkfat12 が作る 2HD (1024B/セクタ) と
    1.44MB (512B/セクタ) の両方。. / .. と削除済みは飛ばす。
    """
    img = open(path, 'rb').read()
    import struct
    bps, spc, rsv, nfat, nroot, tot, _media, fatsz = \
        struct.unpack_from('<HBHBHHBH', img, 0x0B)
    fat = img[rsv * bps:(rsv + fatsz) * bps]
    root_off = (rsv + nfat * fatsz) * bps
    root_secs = (nroot * 32 + bps - 1) // bps
    data_sec = rsv + nfat * fatsz + root_secs
    nclus = (tot - data_sec) // spc
    csize = bps * spc

    def nxt(c):
        v = fat[c * 3 // 2] | (fat[c * 3 // 2 + 1] << 8)
        return (v >> 4) if c & 1 else (v & 0xFFF)

    def chain(c):
        out = []
        while 2 <= c < 0xFF8 and len(out) <= nclus:
            out.append(c)
            c = nxt(c)
        return out

    def read_chain(c):
        return b''.join(img[(data_sec + (x - 2) * spc) * bps:][:csize] for x in chain(c))

    files = {}

    def walk(raw, prefix):
        for i in range(0, len(raw) - 31, 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[0] == ord('.'):
                continue
            base = e[0:8].decode('ascii').rstrip()
            ext = e[8:11].decode('ascii').rstrip()
            name = base + ('.' + ext if ext else '')
            clus = struct.unpack_from('<H', e, 26)[0]
            size = struct.unpack_from('<I', e, 28)[0]
            if e[11] & 0x10:
                walk(read_chain(clus), prefix + '/' + name)
            elif not e[11] & 0x08:
                files[prefix + '/' + name] = read_chain(clus)[:size] if size else b''

    walk(img[root_off:root_off + nroot * 32], '')
    used = sum(1 for c in range(2, nclus + 2) if nxt(c) != 0)
    return files, (nclus - used) * csize, nclus * csize


def case_fd(plan, resolved):
    print("case 7: 起動 FD = BOOT + MINIMAL (+ fd.only)")
    mk = open(IMAGE_MK, encoding='utf-8').read()
    check(not re.search(r'^FDD_MIN_CMDS\s*[:?]?=', mk, re.M) and '--fd-args' in mk
          and '/bin/$$cmd.bin' not in mk,
          "build/image.mk は FD の一覧を手で持たず mkpkg --fd-args から作る")
    fd = plan.get('fd') or {}
    rename = {r['guest']: r['fd'] for r in fd.get('rename') or []}
    only = {o['fd'] for o in fd.get('only') or []}
    check(fd.get('from') == ['boot', 'minimal'], f"FD の元は BOOT + MINIMAL ({fd.get('from')})")

    # 構成: FD の中身 (rename を戻し、only を除いた集合) = BOOT + MINIMAL
    src = {g: os.path.join(ROOT, h) for n, _, _, fs in resolved
           if n == 'boot' or n.startswith('minimal') for g, h in fs}
    plans = {}
    for label, _, loader in FD_IMAGES:
        files, probs = mkpkg.fd_plan(plan, ROOT, loader)
        for p in probs:
            print(f"       {p}")
        check(probs == [], f"FD {label}: fd_plan に問題が無い")
        back = {v: k for k, v in rename.items()}
        core = {back.get(g, g) for g, _ in files if g not in only}
        check(core == set(src),
              f"FD {label}: 中身 = BOOT + MINIMAL (FD にだけ {sorted(core - set(src))}、"
              f"MINIMAL にだけ {sorted(set(src) - core)})")
        check(sorted(g for g, _ in files if g in only) == sorted(only),
              f"FD {label}: FD だけの物は fd.only の {sorted(only)} だけ")
        check(files and files[0][0] == '/LOADER.BIN' and files[0][1] == loader,
              f"FD {label}: 先頭が LOADER.BIN ({loader})")
        plans[label] = files
    check(not any(g in ('/bin/timetest.bin', '/bin/pcmtest.bin') for g, _ in plans['2HD']),
          "FD に試験用の timetest / pcmtest が無い (DEBUG にある)")

    # 否定側
    base = [{'host': 'userland/shell.bin', 'guest': '/sys/shell.bin', 'tags': ['core']}]
    fp = fake_plan()
    fp['fd'] = {'from': ['boot', 'minimal'], 'rename': [], 'only': []}
    _, p = mkpkg.fd_plan(fp, ROOT, None, fake_manifest(base + [
        {'host': 'assets/filetypes', 'guest': '/etc/filetypes', 'tags': ['core']}]))
    check(any('8.3' in x for x in p), "否定: 8.3 に収まらない名前を報告する")
    fp['fd'] = {'from': ['boot', 'minimal'],
                'rename': [{'guest': '/nope', 'fd': '/NOPE', 'reason': 'x'}], 'only': []}
    _, p = mkpkg.fd_plan(fp, ROOT, None, fake_manifest(base))
    check(any('古い rename' in x for x in p), "否定: どれにも当たらない rename を報告する")
    fp['fd'] = {'from': ['boot', 'minimal'],
                'rename': [{'guest': '/sys/shell.bin', 'fd': '/sys/sh.bin'}], 'only': []}
    _, p = mkpkg.fd_plan(fp, ROOT, None, fake_manifest(base))
    check(any('理由' in x for x in p), "否定: 理由の無い rename を報告する")
    fp['fd'] = {'from': ['boot', 'minimal'], 'only': [], 'rename': [
        {'guest': '/sys/shell.bin', 'fd': '/sys/sh1.bin', 'reason': 'x'},
        {'guest': '/sys/shell.bin', 'fd': '/sys/sh2.bin', 'reason': 'x'}]}
    _, p = mkpkg.fd_plan(fp, ROOT, None, fake_manifest(base))
    check(any('rename' in x and '2 回' in x for x in p),
          "否定: 同じ guest への rename が 2 回なのを報告する")
    fp['fd'] = {'from': ['boot', 'minimal'], 'rename': [],
                'only': [{'fd': '/LOADER.BIN', 'host': '{loader}', 'reason': 'x'}]}
    _, p = mkpkg.fd_plan(fp, ROOT, None, fake_manifest(base))
    check(any('ローダ' in x for x in p), "否定: ローダの指定が無いのを報告する")
    fp['fd'] = {'from': ['boot', 'minimal'], 'rename': [], 'only': [
        {'fd': '/SYS/SHELL.BIN', 'host': 'x', 'reason': 'x'}]}
    _, p = mkpkg.fd_plan(fp, ROOT, None, fake_manifest(base))
    check(any('2 回' in x for x in p), "否定: FD のパスの重複 (大文字小文字を畳む) を報告する")

    # 実物のイメージ
    for label, img, _ in FD_IMAGES:
        if not os.path.isfile(img):
            check(False, f"{os.path.relpath(img, ROOT)} がある (先に make all)")
            continue
        got, free, total = fat12_files(img)
        want = {g.upper(): h for g, h in plans[label]}
        missing = sorted(set(want) - set(got))
        extra = sorted(set(got) - set(want))
        bad = sorted(g for g in set(want) & set(got)
                     if open(want[g], 'rb').read() != got[g])
        for g in missing[:10]:
            print(f"       missing {g}")
        for g in extra[:10]:
            print(f"       extra   {g}")
        for g in bad[:10]:
            print(f"       bytes   {g}")
        check(not missing and not extra and not bad,
              f"FD {label} の実物 = 構成 ({len(got)} ファイル、欠け {len(missing)}、"
              f"余分 {len(extra)}、違い {len(bad)})")
        print(f"  info FD {label}: 空き {free // 1024}KB / {total // 1024}KB")
        p = fd_free_problem(label, free)
        check(p is None, p or f"FD {label}: 空き {free // 1024}KB >= {FD_MIN_FREE_KB}KB")
    # 否定側: 見張りが閾値の 1KB 下を落とし、ちょうどは通す
    check(fd_free_problem('2HD', FD_MIN_FREE_KB * 1024 - 1024) is not None,
          f"否定: 空き {FD_MIN_FREE_KB - 1}KB は下限割れとして落とす")
    check(fd_free_problem('2HD', FD_MIN_FREE_KB * 1024) is None,
          f"空き {FD_MIN_FREE_KB}KB ちょうどは通す")


def fd_free_problem(label, free):
    """空き free (バイト) が下限を切っていれば理由の文、足りていれば None"""
    if free >= FD_MIN_FREE_KB * 1024:
        return None
    return (f"FD {label}: 空き {free // 1024}KB が下限 {FD_MIN_FREE_KB}KB を切った。"
            "次の増分で FD が作れなくなる — MINIMAL (core / base) に足した物を見直すか、"
            "FD から外す物を決めること (docs/08_build.md「起動 FD と MINIMAL」)")


# ---------------------------------------------------------------- 7b. 配布物の D88

def d88_to_raw(blob):
    """D88 を読み、(c, h, r) 順に並べたセクタ列を RAW として返す

    ヘッダ 0x2B0 (名前 17 + 予約 9 + 保護 1 + 種別 1 + 全長 4 + トラック表
    164 × 4)。各セクタは 16 バイトのヘッダ (C, H, R, N, 数, 密度, 削除, 状態,
    予約 5, データ長 2) + データ。トラックはシリンダ × 2 + ヘッド の順で読む。
    形が崩れていれば ValueError。
    """
    import struct
    if len(blob) < 0x2B0:
        raise ValueError('D88 のヘッダより短い')
    disk_size = struct.unpack_from('<I', blob, 0x1C)[0]
    if disk_size != len(blob):
        raise ValueError(f'ヘッダの全長 {disk_size} != 実物 {len(blob)}')
    offs = struct.unpack_from('<164I', blob, 0x20)
    out = bytearray()
    for t, off in enumerate(offs):
        if off == 0:
            continue
        p = off
        # 1 本目のセクタのヘッダが「このトラックのセクタ数」を持つ。その数だけ読む
        if p + 16 > len(blob):
            raise ValueError(f'トラック {t}: セクタのヘッダが範囲外')
        nsec = struct.unpack_from('<H', blob, p + 4)[0]
        secs = {}
        for _ in range(nsec):
            if p + 16 > len(blob):
                raise ValueError(f'トラック {t}: セクタのヘッダが範囲外')
            c, h, r, n, ns, _dens, _dele, _st = struct.unpack_from('<BBBBHBBB', blob, p)
            dlen = struct.unpack_from('<H', blob, p + 14)[0]
            if ns != nsec:
                raise ValueError(f'トラック {t} R{r}: セクタ数 {ns} != {nsec}')
            if (c, h) != (t // 2, t % 2):
                raise ValueError(f'トラック {t}: C/H = {c}/{h}')
            if dlen != 128 << n:
                raise ValueError(f'トラック {t} R{r}: データ長 {dlen} と N={n} が合わない')
            if r in secs:
                raise ValueError(f'トラック {t}: R{r} が 2 回')
            secs[r] = blob[p + 16:p + 16 + dlen]
            p += 16 + dlen
        # R は重複なく 1〜spt (並べ替えで壊れた R を隠さない)
        if sorted(secs) != list(range(1, nsec + 1)):
            raise ValueError(f'トラック {t}: R が 1〜{nsec} でない ({sorted(secs)})')
        for r in sorted(secs):
            out += secs[r]
    return bytes(out)


def case_d88():
    print("case 7b: 配布物の D88 = RAW")
    raw_path = FD_IMAGES[0][1]
    if not (os.path.isfile(D88) and os.path.isfile(raw_path)):
        check(False, "images/os32_boot.d88 と os32_boot.img がある (先に make all)")
        return
    blob = open(D88, 'rb').read()
    raw = open(raw_path, 'rb').read()
    try:
        got = d88_to_raw(blob)
    except ValueError as e:
        check(False, f"D88 が読める: {e}")
        return
    check(got == raw, f"D88 のセクタを並べると RAW と同じ ({len(got)} / {len(raw)} B)")
    # 否定側: データ部を 1 セクタ壊す / セクタのヘッダ (R) を壊す
    import struct
    off0 = struct.unpack_from('<I', blob, 0x20 + 4 * 10)[0]   # トラック 10 の先頭
    bad = bytearray(blob)
    for i in range(1024):
        bad[off0 + 16 + i] ^= 0xFF
    check(d88_to_raw(bytes(bad)) != raw, "否定: D88 の 1 セクタ (トラック 10 R1) のデータを壊すと不一致")
    def rejected(mut):
        try:
            return d88_to_raw(bytes(mut)) != raw
        except ValueError:
            return True

    bad = bytearray(blob)
    bad[off0 + 2] = 0x7F
    check(rejected(bad), "否定: D88 のセクタ番号 (R) を 0x7F に壊すと不一致")
    # R=1 を R=0 に: 並べ替えだけだと順序が変わらず通っていた (往復 2 の P2)
    bad = bytearray(blob)
    check(bad[off0 + 2] == 1, "トラック 10 の先頭セクタは R=1")
    bad[off0 + 2] = 0
    check(rejected(bad), "否定: R=1 を R=0 に壊すと断る (R は 1〜spt)")
    # R の重複 (2 本目を R=1 に)
    sec2 = off0 + 16 + (128 << blob[off0 + 3])
    bad = bytearray(blob)
    bad[sec2 + 2] = 1
    check(rejected(bad), "否定: R の重複を断る")
    bad = bytearray(blob[:-1024])
    try:
        d88_to_raw(bytes(bad))
        broken = False
    except ValueError:
        broken = True
    check(broken, "否定: 末尾 1 セクタ欠けた D88 は読めない (全長が合わない)")


# ---------------------------------------------------------------- 8. 短い名前は FD だけ

BOOT_FONT_HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "kstring.h"
/* FAT_NAME は fs/fatfs_vfs.c の VfsOps の名前 (Python が実物から読んで -D で渡す) */
static const char *g_fs_root = "ext2";   /* "/" のマウントの FS */
static const char *g_fs_sys = "";        /* "/sys" のマウント ("" = マウント点でない) */
static int g_long_ok, g_short_ok, g_short_tried;
int kcg_load_font(const char *path)
{
    if (strcmp(path, SYS_FONT_DEFAULT) == 0) return g_long_ok ? 0 : -1;
    if (strcmp(path, SYS_FONT_DEFAULT_83) == 0) {
        g_short_tried = 1;
        return g_short_ok ? 0 : -1;
    }
    return -9;
}
const char *vfs_fstype(const char *prefix)
{
    if (strcmp(prefix, "/") == 0) return g_fs_root;
    if (strcmp(prefix, "/sys") == 0) return g_fs_sys;
    return "";
}
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
u32 kstrlen(const char *s) { return (u32)strlen(s); }
char *kstrncpy(char *d, const char *s, u32 n)
{
    strncpy(d, s, n);
    if (n) d[n - 1] = '\0';
    return d;
}
#include "boot_font.c"
static int fails;
static void run(const char *root, const char *sys, int lo, int sh, int want_rc,
                int want_tried, const char *label)
{
    int rc;
    g_fs_root = root; g_fs_sys = sys;
    g_long_ok = lo; g_short_ok = sh; g_short_tried = 0;
    rc = boot_font_load();
    if ((rc == 0) != (want_rc == 0) || g_short_tried != want_tried) {
        printf("FAIL %s (rc=%d tried=%d)\n", label, rc, g_short_tried);
        fails++;
    } else {
        printf("ok %s\n", label);
    }
}
int main(void)
{
    run("ext2", "", 0, 1, -1, 0, "HDD: 正規名が無ければ失敗のまま、短い名前を読まない");
    run("ext2", "", 1, 1, 0, 0, "HDD: 正規名を読む");
    run(FAT_NAME, "", 0, 1, 0, 1, "FD: 正規名が無ければ短い名前を読む");
    run(FAT_NAME, "", 1, 1, 0, 0, "FD: 正規名があれば正規名");
    run(FAT_NAME, "", 0, 0, -1, 1, "FD: どちらも無ければ失敗");
    run(FAT_NAME, "ext2", 0, 1, -1, 0,
        "FD ルート + /sys に ext2 をマウント: 短い名前を読まない");
    run("ext2", FAT_NAME, 0, 1, 0, 1, "HDD ルート + /sys に FAT: 短い名前を読む");
    return fails ? 1 : 0;
}
"""


def real_fat_name():
    """fs/fatfs_vfs.c の VfsOps の名前 (vfs_fstype が返す文字列)"""
    src = open(os.path.join(ROOT, 'fs', 'fatfs_vfs.c'), encoding='utf-8').read()
    m = re.search(r'static\s+VfsOps\s+fatfs_ops\s*=\s*\{\s*"(\w+)"', src)
    return m.group(1) if m else None


def kernel_fd_root():
    """kernel/kernel.c の FD 起動のルート (デバイス名, FS 名)"""
    src = open(os.path.join(ROOT, 'kernel', 'kernel.c'), encoding='utf-8').read()
    m = re.search(r'BOOT_DRIVE_FDD[^{]*\{\s*root_dev\s*=\s*"(\w+)";\s*'
                  r'root_fs\s*=\s*"(\w+)";', src)
    return (m.group(1), m.group(2)) if m else (None, None)


def run_boot_font(src_text, fat_name):
    """boot_font.c (src_text) をハーネスで回す。戻り値 (rc, 出力)"""
    tmp = tempfile.mkdtemp(prefix='bootfont_')
    try:
        with open(os.path.join(tmp, 'boot_font.c'), 'w', encoding='utf-8') as f:
            f.write(src_text)
        h = os.path.join(tmp, 'h.c')
        with open(h, 'w', encoding='utf-8') as f:
            f.write(BOOT_FONT_HARNESS)
        exe = os.path.join(tmp, 'a.out')
        inc = ['-I' + tmp] + ['-I' + os.path.join(ROOT, d)
                              for d in ('kernel', 'include', 'drivers', 'fs', 'lib',
                                        'sdk/include', 'sdk/include/os32')]
        r = subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-D__cdecl=',
                            '-Wno-unused-function',   # 変異で遡りの関数が使われなくなる
                            '-DFAT_NAME="%s"' % fat_name, '-include',
                            os.path.join(ROOT, 'include', 'config.h'), *inc,
                            '-o', exe, h], capture_output=True, text=True)
        if r.returncode != 0:
            return None, r.stderr
        r = subprocess.run([exe], capture_output=True, text=True)
        return r.returncode, r.stdout
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def case_boot_font():
    print("case 8: 8.3 の短い名前へのフォールバックは LFN の無い FS (FAT) のときだけ")
    # 試験の側で "fat" / "fd0" を決め打ちしない — 実物と突き合わせる
    fat = real_fat_name()
    root_dev, root_fs = kernel_fd_root()
    check(fat is not None, f"fs/fatfs_vfs.c の VfsOps の名前が読める ({fat})")
    check(root_fs == fat, f"kernel.c の FD ルートの FS ({root_fs}) = FAT の VfsOps 名 ({fat})")
    cfg = open(os.path.join(ROOT, 'include', 'config.h'), encoding='utf-8').read()
    m = re.search(r'#define\s+SYS_FONT_83_FSTYPE\s+"(\w+)"', cfg)
    check(m is not None and m.group(1) == fat,
          f"config.h SYS_FONT_83_FSTYPE ({m and m.group(1)}) = FAT の VfsOps 名")
    filer = open(os.path.join(ROOT, 'userland', 'shell', 'cmd_filer.c'),
                 encoding='utf-8').read()
    p0 = re.search(r"#define\s+FL_FD_DEV_PREFIX0\s+'(.)'", filer)
    p1 = re.search(r"#define\s+FL_FD_DEV_PREFIX1\s+'(.)'", filer)
    check(root_dev is not None and p0 and p1 and root_dev[:2] == p0.group(1) + p1.group(1),
          f"filer の FD 判定 ({p0 and p0.group(1)}{p1 and p1.group(1)}…) = kernel.c の "
          f"FD ルート ({root_dev})")
    if not shutil.which('gcc') or fat is None:
        print("  SKIP gcc が無い / FAT の名前が読めない")
        return
    src = open(BOOT_FONT_C, encoding='utf-8').read()
    rc, out = run_boot_font(src, fat)
    for line in (out or '').splitlines():
        print(f"       {line}")
    check(rc == 0, "kernel/boot_font.c (実物): HDD は失敗のまま、FAT のマウントだけ短い名前")
    gate = ('        kstrcmp(boot_font_fstype_of(SYS_FONT_DEFAULT), '
            'SYS_FONT_83_FSTYPE) == 0) {')
    check(src.count(gate) == 1, "変異の目印がちょうど 1 か所")
    for mut, label in (
            (gate.replace('kstrcmp(', '(kstrcmp(').replace('== 0) {', '== 0 || 1)) {'),
             "FS を見ずに短い名前へ落ちる"),
            (gate.replace('boot_font_fstype_of(SYS_FONT_DEFAULT)', 'vfs_fstype("/")'),
             "対象のマウントではなくルートの FS を見る")):
        rc, _ = run_boot_font(src.replace(gate, mut), fat)
        check(rc not in (0, None), f"否定: {label}変異は RED")


# ---------------------------------------------------------------- 9. FD → install → HDD

INSTALL_C = os.path.join(ROOT, 'userland', 'system', 'install.c')
INSTALL_HARNESS = os.path.join(ROOT, 'tools', 'tests', 'install_fresh_host.c')


def install_table(src):
    """install.c の fd_renames {fd: hdd} と fd_only [fd]"""
    m = re.search(r'fd_renames\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    ren = dict(re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)"\s*\}', m.group(1))) if m else {}
    m = re.search(r'fd_only\[\]\s*=\s*\{(.*?)\n\};', src, re.S)
    only = re.findall(r'"([^"]+)"', m.group(1)) if m else []
    return ren, only


def run_install_fdset(install_src, fd_files):
    """install.c (install_src) を install_fresh_host.c の段 fdset で回す。
    fd_files = [(FD 上のパス, 大きさ)]。戻り値 {HDD のパス: 大きさ} か None"""
    tmp = tempfile.mkdtemp(prefix='fdinst_')
    try:
        sysdir = os.path.join(tmp, 'userland', 'system')
        hdir = os.path.join(tmp, 'tools', 'tests')
        os.makedirs(sysdir)
        os.makedirs(hdir)
        with open(os.path.join(sysdir, 'install.c'), 'w', encoding='utf-8') as f:
            f.write(install_src)
        shutil.copy(os.path.join(ROOT, 'userland', 'system', 'install_recover.inc'), sysdir)
        shutil.copy(INSTALL_HARNESS, hdir)
        exe = os.path.join(tmp, 'a.out')
        inc = ['-I' + os.path.join(ROOT, d) for d in
               ('include', 'sdk/include', 'sdk/include/os32', 'userland/lib',
                'userland/system', '')]
        # install.c が繋ぐ hd0 の共有部 (票 TASK_HDD_INSTALL 段 2、
        # build/programs.mk の INST_OBJ と同じ顔ぶれ)
        shared = [os.path.join(ROOT, f) for f in
                  ('userland/system/inst_hdd.c', 'userland/system/inst_disk.c',
                   'userland/shell/hdprep_plan.c', 'drivers/pc98pt.c',
                   'fs/ext2_layout.c')]
        r = subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function',
                            '-Wno-pointer-to-int-cast', '-D__cdecl=',
                            '-D__OS32_USERLAND__', '-O0', *inc,
                            os.path.join(hdir, 'install_fresh_host.c'), *shared,
                            '-o', exe],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr[-2000:])
            return None
        lst = os.path.join(tmp, 'fd.txt')
        out = os.path.join(tmp, 'hdd.txt')
        with open(lst, 'w') as f:
            for g, n in fd_files:
                f.write(f"{g} {n}\n")
        r = subprocess.run([exe, 'fdset', lst, out], capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr[-2000:])
            return None
        got = {}
        for line in open(out):
            g, n = line.split()
            got[g] = int(n)
        return got
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def case_install(plan, resolved):
    print("case 9: FD → install → HDD = MINIMAL")
    fd = plan.get('fd') or {}
    src = open(INSTALL_C, encoding='utf-8').read()
    ren, only = install_table(src)
    want_ren = {r['fd'].lower(): r['guest'] for r in fd.get('rename') or []}
    want_only = sorted(o['fd'].lower() for o in fd.get('only') or [])
    check(ren == want_ren, f"install.c の fd_renames = packages.yaml の fd.rename (逆) {ren}")
    check(sorted(only) == want_only, f"install.c の fd_only = fd.only {sorted(only)}")
    if not shutil.which('gcc'):
        print("  SKIP gcc が無い")
        return
    files, probs = mkpkg.fd_plan(plan, ROOT, FD_IMAGES[0][2])
    if probs or any(not os.path.isfile(os.path.join(ROOT, h)) for _, h in files):
        check(False, "FD の構成と成果物が揃っている (先に make all)")
        return
    fd_files = [(g, os.path.getsize(os.path.join(ROOT, h))) for g, h in files]
    minimal = {g: os.path.getsize(h) for n, _, _, fs in resolved
               if n.startswith('minimal') for g, h in fs}
    got = run_install_fdset(src, fd_files)
    check(got is not None, "実物の install.c が段 fdset で通る")
    if got is None:
        return
    miss = sorted(set(minimal) - set(got))
    extra = sorted(set(got) - set(minimal))
    size = sorted(g for g in set(minimal) & set(got) if minimal[g] != got[g])
    for g in miss[:10]:
        print(f"       missing {g}")
    for g in extra[:10]:
        print(f"       extra   {g}")
    check(not miss and not extra and not size,
          f"HDD の集合 = MINIMAL ({len(got)} 件、欠け {len(miss)}、余分 {len(extra)}、"
          f"大きさ違い {len(size)})")
    # 否定側: 逆変換を当てない (8.3 名のまま写す) と不一致
    mark = '        if (!str_eq_lower(src_path, fd_renames[i].fd)) continue;'
    check(src.count(mark) == 1, "変異の目印がちょうど 1 か所")
    bad = run_install_fdset(src.replace(mark, '        continue;'), fd_files)
    check(bad is not None and set(bad) != set(minimal),
          "否定: fd.rename を逆に当てない install は MINIMAL と違う集合になる")


def main():
    from check_artifacts import require_fresh
    artifacts = ['images/os32_boot.d88', 'images/os32_boot144.img',
                 'images/os32_install.iso'] + [os.path.relpath(p, ROOT)
                                               for p in pathlib.Path(ROOT, 'packages').glob('*.PKG')]
    require_fresh(ROOT, artifacts, ['build/packages.yaml', 'build/core.yaml',
        'userland/deploy.yaml', 'tools/mkpkg.py'],
        targets={p: 'packages' for p in artifacts if p.startswith('packages/')} |
                {'images/os32_install.iso': 'iso'})
    plan, resolved = case_real_plan()
    case_negative()
    case_split()
    fresh = case_media(plan, resolved)
    case_iso(plan, fresh)
    case_consumer(plan)
    case_fd(plan, resolved)
    case_d88()
    case_boot_font()
    case_install(plan, resolved)
    print()
    if fails:
        print(f"FAIL {len(fails)} 件")
        return 1
    print("PASS")
    return 0


if __name__ == '__main__':
    sys.exit(main())
