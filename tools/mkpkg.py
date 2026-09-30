#!/usr/bin/env python3
# ======================================================================== #
#  mkpkg.py — OS32 パッケージ (.pkg) 作成ツール
#
#  使用法:
#    python3 tools/mkpkg.py --plan build/packages.yaml --output packages/
#        CD 媒体の全パッケージ。中身は配備マニフェスト (tools/deploy_manifests.py)
#        のタグから決める (make packages)
#    python3 tools/mkpkg.py --plan build/packages.yaml --check-plan
#        振り分けの検査だけ (ファイルは読まない)
#    python3 tools/mkpkg.py --plan build/packages.yaml --fd-args \
#        --fd-loader boot/loader_fat_new.bin
#        起動 FD の中身 (= BOOT + MINIMAL、packages.yaml の fd:) を
#        mkfat12 --tree の引数で出す (build/image.mk)
#    python3 tools/mkpkg.py --list packages/NORMAL.PKG
#        PKG の中身の一覧
#    python3 tools/mkpkg.py --defs defs.yaml --output out/
#        ファイル一覧を直に書いた定義から (試験用)
#    python3 tools/mkpkg.py --name edit --version 1 --lzss \
#        /usr/bin/edit=programs/edit.bin -o EDIT.PKG
#
#  PKGフォーマット:
#    [PKGヘッダ 32B] [ファイルテーブル] [終端 1B] [データ部 (LZSS or 無圧縮)]
# ======================================================================== #

import sys
import os
import struct
import glob
import argparse

# LZSS定数 (lib/lzss.c と同一)
N = 4096
F = 18
THRESHOLD = 2

def lzss_encode(in_data):
    """LZSS圧縮 (ハッシュチェイン高速版、lzss_pack.py互換出力)"""
    out_data = bytearray()
    out_data.extend(struct.pack('<I', len(in_data)))

    text_buf = bytearray(N + F - 1)
    for i in range(N):
        text_buf[i] = 0x20

    in_len = len(in_data)
    src_p = 0
    r = N - F

    flags = 0
    flag_pos = 0
    code_buf = bytearray(17)
    code_buf_ptr = 1

    # ハッシュチェイン (高速マッチング用)
    HASH_SIZE = 4096
    MAX_CHAIN = 64  # チェインの最大探索深度
    hash_head = [-1] * HASH_SIZE   # hash -> 最新のリングバッファ位置
    hash_prev = [-1] * N           # リングバッファ位置 -> 前の同ハッシュ位置

    def calc_hash(a, b, c):
        return ((a << 5) ^ (b << 3) ^ c) & (HASH_SIZE - 1)

    def insert_hash(pos, byte_val):
        """現在位置をハッシュテーブルに登録 (3バイト先読み不可の場合)"""
        # 3バイトハッシュが計算できない場合は挿入のみ
        pass

    while src_p < in_len:
        match_pos = 0
        match_len = 0

        # ハッシュチェインで候補を検索
        if src_p + 2 < in_len:
            h = calc_hash(in_data[src_p], in_data[src_p + 1], in_data[src_p + 2])
            chain_pos = hash_head[h]
            chain_count = 0

            while chain_pos >= 0 and chain_count < MAX_CHAIN:
                # リングバッファのどの距離にあるか確認
                dist = (r - chain_pos) % N
                if dist == 0 or dist > N:
                    break

                # マッチ長を計測
                l = 0
                while l < F and src_p + l < in_len and text_buf[(chain_pos + l) % N] == in_data[src_p + l]:
                    l += 1
                if l > match_len:
                    match_len = l
                    match_pos = chain_pos
                    if match_len == F:
                        break

                chain_pos = hash_prev[chain_pos]
                chain_count += 1

        if match_len <= THRESHOLD:
            match_len = 1
            flags |= (1 << flag_pos)
            code_buf[code_buf_ptr] = in_data[src_p]
            code_buf_ptr += 1
        else:
            code_buf[code_buf_ptr] = match_pos & 0xFF
            code_buf_ptr += 1
            code_buf[code_buf_ptr] = ((match_pos >> 4) & 0xF0) | (match_len - (THRESHOLD + 1))
            code_buf_ptr += 1

        flag_pos += 1
        if flag_pos == 8:
            code_buf[0] = flags
            out_data.extend(code_buf[:code_buf_ptr])
            flags = 0
            flag_pos = 0
            code_buf_ptr = 1

        for i in range(match_len):
            if src_p < in_len:
                text_buf[r] = in_data[src_p]
                # ハッシュテーブルに挿入 (3バイト先読み可能なら)
                if src_p + 2 < in_len:
                    ih = calc_hash(in_data[src_p], in_data[src_p + 1], in_data[src_p + 2])
                    hash_prev[r] = hash_head[ih]
                    hash_head[ih] = r
                r = (r + 1) % N
                src_p += 1

    if flag_pos > 0:
        code_buf[0] = flags
        out_data.extend(code_buf[:code_buf_ptr])

    return bytes(out_data)


# PKGヘッダ定数
PKG_MAGIC = b'PKG1'
PKG_HEADER_SIZE = 32
PKG_FLAG_LZSS = 0x01
PKG_TYPE_FILE = 0
PKG_TYPE_DIR  = 1
# 消費側 (userland/lib/rt/pkg.h) の上限。PKG_MAX_PATH は NUL 込み。
PKG_MAX_PATH = 128
PKG_MAX_ENTRIES = 128
# cdinst は展開先へ "/hd0" を前置する。前置込みで PKG_MAX_PATH に収める
# (票 TASK_VFS_FD_PATH 方針 v2 の 9: 格納パスは UTF-8 で 123 バイトまで)
PKG_INSTALL_PREFIX = '/hd0'
PKG_GUEST_PATH_MAX = PKG_MAX_PATH - 1 - len(PKG_INSTALL_PREFIX.encode('utf-8'))

# KAPI_VERSION を os32_kapi_shared.h から読み取る
def read_kapi_version(base_dir):
    """SDK の契約ヘッダから KAPI_VERSION を取得"""
    path = os.path.join(base_dir, 'sdk', 'include', 'os32', 'os32_kapi_shared.h')
    try:
        with open(path, 'r') as f:
            for line in f:
                if '#define' in line and 'KAPI_VERSION' in line:
                    parts = line.split()
                    if len(parts) >= 3:
                        return int(parts[2])
    except (IOError, ValueError):
        pass
    return 0


def guest_path_ok(path):
    """PKG に載せてよいゲストパスか: "/a/b" の形で、どの要素も空・"."・".." でない"""
    if not path.startswith('/') or path == '/':
        return False
    parts = path[1:].split('/')
    return all(p not in ('', '.', '..') for p in parts)


def build_pkg(name, version, files, use_lzss, kapi_ver):
    """
    PKGファイルを構築する

    files: [(guest_path, host_path), ...] のリスト
    戻り値: bytes (PKGファイル全体)
    """
    # ゲストパスの検査 (票 TASK_EXT2_EMPTY_NAME)。cdinst は "/hd0" + パスで
    # 展開し、pkg.c は各 "/" で mkdir するので、先頭 "/" 無し ("/hd0bin/…" に
    # 化ける)・空の要素 ("//"、末尾 "/")・"." / ".." は媒体に妙な名前を作る。
    bad = [g for g, _ in files if not guest_path_ok(g)]
    if bad:
        for g in bad:
            print(f"ERROR: bad guest path {g!r} (package '{name}'): "
                  "must start with '/', no empty / '.' / '..' component, "
                  "no trailing '/'", file=sys.stderr)
        raise SystemExit(1)

    # ファイルデータ連結
    raw_data = bytearray()
    entries = []

    # 長さ (票 TASK_VFS_FD_PATH 方針 v2 の 9)。**UTF-8 のバイト長**で、展開先の
    # 前置 (/hd0) を含めて消費側の PKG_MAX_PATH (NUL 込み) に収まるか。以前は
    # 形だけを見て、255 バイトで黙って切り詰めていた。消費側 (pkg.c) は 128
    # 以上で読み位置がずれ、cdinst の前置で溢れた分は切り詰められて後の
    # ファイルが前を上書きした。
    long_paths = [g for g, _ in files
                  if len(g.encode('utf-8')) > PKG_GUEST_PATH_MAX]
    if long_paths:
        for g in long_paths:
            print(f"ERROR: guest path too long {g!r} (package '{name}'): "
                  f"{len(g.encode('utf-8'))} bytes in UTF-8, limit "
                  f"{PKG_GUEST_PATH_MAX} ({PKG_INSTALL_PREFIX!r} + path + NUL "
                  f"<= {PKG_MAX_PATH})", file=sys.stderr)
        raise SystemExit(1)

    # ディレクトリを自動収集
    dirs_seen = set()
    for guest_path, _ in files:
        parts = guest_path.split('/')
        for i in range(1, len(parts)):
            d = '/'.join(parts[:i])
            if d and d != '/' and d not in dirs_seen:
                dirs_seen.add(d)

    # ディレクトリエントリ
    sorted_dirs = sorted(dirs_seen)
    for d in sorted_dirs:
        entries.append((d, 0, PKG_TYPE_DIR))

    # ファイルエントリ
    # 登録したファイルの欠損は一律エラー。かつては warning で飛ばしていたが、
    # それだと「入っているはずのものが入っていない .PKG」が黙って出来る
    # (S0-T: /etc/settings.db がビルド順のずれで落ちても気付けない)。
    missing = [h for _, h in files if not os.path.isfile(h)]
    if missing:
        for host_path in missing:
            print(f"ERROR: {host_path} not found (package '{name}')",
                  file=sys.stderr)
        raise SystemExit(1)

    for guest_path, host_path in files:
        with open(host_path, 'rb') as f:
            data = f.read()
        entries.append((guest_path, len(data), PKG_TYPE_FILE))
        raw_data.extend(data)

    # 項目数 (ディレクトリ項目を含む) は消費側の表の大きさまで
    if len(entries) > PKG_MAX_ENTRIES:
        print(f"ERROR: too many entries in package '{name}': {len(entries)} "
              f"(files + directories), limit {PKG_MAX_ENTRIES}", file=sys.stderr)
        raise SystemExit(1)

    # LZSS圧縮
    orig_size = len(raw_data)
    if use_lzss and orig_size > 0:
        compressed = lzss_encode(raw_data)
        flags = PKG_FLAG_LZSS
        data_part = compressed
    else:
        flags = 0
        data_part = bytes(raw_data)

    # ファイルテーブル構築
    file_table = bytearray()
    entry_count = 0
    for path, size, ftype in entries:
        path_bytes = path.encode('utf-8')
        path_len = len(path_bytes)
        # 上で検査済み。切り詰めない (黙って別の名前にしない)
        assert 0 < path_len <= PKG_GUEST_PATH_MAX, path
        file_table.append(path_len)
        file_table.extend(path_bytes)
        file_table.extend(struct.pack('<I', size))
        file_table.append(ftype)
        entry_count += 1

    # 終端マーカー
    file_table.append(0x00)

    # PKGヘッダ (32バイト)
    name_bytes = name.encode('utf-8')[:8].ljust(8, b'\x00')
    header = bytearray(PKG_HEADER_SIZE)
    header[0:4] = PKG_MAGIC
    header[4:12] = name_bytes
    header[12] = version & 0xFF
    header[13] = flags & 0xFF
    struct.pack_into('<H', header, 14, kapi_ver & 0xFFFF)
    struct.pack_into('<H', header, 16, entry_count & 0xFFFF)
    struct.pack_into('<I', header, 18, orig_size)
    struct.pack_into('<I', header, 22, len(data_part))
    # header[26:32] = 予約 (0x00)

    return bytes(header) + bytes(file_table) + data_part


def merge_package_defs(yaml_paths):
    """層ごとのパッケージ定義をマージする

    同名パッケージの files: を読み込み順に連結する。メタデータ
    (type / version / lzss) は最初に現れた定義のものを使う。
    層をまたいで 1 つの .PKG を組むための仕組みで、たとえば MINIMAL は
    カーネルイメージ (core) と標準コマンド (userland) の両方を含む。
    """
    merged = {}
    absent = [p for p in yaml_paths if not os.path.isfile(p)]
    if absent:
        # 黙って無視すると「定義ごと落ちた .PKG」が静かに出来る。
        for path in absent:
            print(f"ERROR: package definition not found: {path}",
                  file=sys.stderr)
        raise SystemExit(1)
    for path in yaml_paths:
        for name, pdef in parse_simple_yaml(path).items():
            if name not in merged:
                merged[name] = dict(pdef)
                merged[name]['files'] = list(pdef.get('files', []))
            else:
                merged[name]['files'].extend(pdef.get('files', []))
    return merged


def build_from_yaml(yaml_paths, output_dir, base_dir):
    """パッケージ定義 (複数可) からパッケージを一括生成"""
    if isinstance(yaml_paths, str):
        yaml_paths = [yaml_paths]
    packages = merge_package_defs(yaml_paths)
    kapi_ver = read_kapi_version(base_dir)

    os.makedirs(output_dir, exist_ok=True)

    # 先に全パッケージのファイルを解決し、欠損があれば 1 つも書かずに落ちる
    # (途中まで書いた .PKG を残さない)。glob が 0 件なのも欠損として扱う
    # ("*.1 が 1 つも無い" は登録の意図が満たされていないため)。簡易 parser は
    # 変えず、展開結果だけを見る。
    resolved = []
    problems = []
    for pkg_name, pkg_def in packages.items():
        version = int(pkg_def.get('version', 1))
        use_lzss = pkg_def.get('lzss', True)
        files = []

        for fdef in pkg_def.get('files', []):
            host = fdef.get('host', '')
            guest = fdef.get('guest', '')

            # Glob展開
            if '*' in host:
                pattern = os.path.join(base_dir, host)
                matches = sorted(glob.glob(pattern))
                if not matches:
                    problems.append((pkg_name,
                                     f"{pattern} matched no files"))
                for match in matches:
                    fname = os.path.basename(match)
                    fguest = guest.rstrip('/') + '/' + fname
                    files.append((fguest, match))
            else:
                host_full = os.path.join(base_dir, host)
                if guest.endswith('/'):
                    guest = guest + os.path.basename(host)
                files.append((guest, host_full))

        if not files:
            print(f"  {pkg_name}: no files, skipping")
            continue
        resolved.append((pkg_name, version, use_lzss, files))

    problems += [(n, f"{h} not found") for n, _, _, fs in resolved
                 for _, h in fs if not os.path.isfile(h)]
    if problems:
        for pkg_name, reason in problems:
            print(f"ERROR: {reason} (package '{pkg_name}')", file=sys.stderr)
        raise SystemExit(1)

    write_packages(resolved, output_dir, base_dir)



# ======================================================================== #
#  CD 媒体の構成 (--plan)
#
#  中身は配備マニフェスト (build/core.yaml + userland/deploy.yaml) のタグで
#  決める。以前は package_defs.yaml に一覧を手で写していて、配備に足した物が
#  CD に入らなかった (gshell / libos32gui.shlib / 既定フォント …)。
# ======================================================================== #

# PKG のベース名の上限。ISO 9660 の 8.3 に収め、分割の連番 1 桁を足せるように
PKG_BASE_NAME_MAX = 7
# userland/tests/ 由来の行は DEBUG へ (試験バイナリが NORMAL に混ざらないように)
TEST_HOST_PREFIX = 'userland/tests/'
TEST_TAG = 'test'


def load_plan(plan_path):
    import yaml
    with open(plan_path, 'r', encoding='utf-8') as f:
        plan = yaml.safe_load(f) or {}
    if not isinstance(plan.get('packages'), dict):
        raise SystemExit(f"ERROR: {plan_path}: packages: が無い")
    return plan


def plan_packages(plan, base_dir, manifest=None):
    """配備マニフェストを振り分けて [(name, meta, [(guest, host_rel), ...])] を返す

    戻り値: (packages, problems)。problems は人が読む文字列の list で、
    空でなければ媒体を作ってはいけない。ファイルの有無は見ない
    (--check-plan はビルド前でも回せるように)。
    manifest を渡すとそれを使う (試験用。load_merged() と同じ形)。
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import deploy_manifests as dm

    problems = []
    if manifest is None:
        manifest = dm.load_merged(dm.CORE_MANIFEST_RELPATHS)
        if manifest is None:
            return [], ['配備マニフェストが読めない']

    pkgs = []           # 定義順
    tag_to_pkg = {}
    for name, pdef in plan['packages'].items():
        pdef = pdef or {}
        if len(name) > PKG_BASE_NAME_MAX:
            problems.append(f"パッケージ名 {name!r} が {PKG_BASE_NAME_MAX} 文字を超える "
                            "(ISO 9660 の 8.3 と分割の連番)")
        files = []
        for fdef in pdef.get('files') or []:
            files.append((fdef['guest'], fdef['host']))
        for t in pdef.get('tags') or []:
            if t in tag_to_pkg:
                problems.append(f"タグ {t!r} が 2 つのパッケージ "
                                f"({tag_to_pkg[t]}, {name}) にある")
            tag_to_pkg[t] = name
        pkgs.append((name, pdef, files))
    by_name = {n: f for n, _, f in pkgs}

    exclude = {}
    for ex in plan.get('exclude') or []:
        g = ex.get('guest', '')
        if not (ex.get('reason') or '').strip():
            problems.append(f"除外 {g!r} に理由 (reason:) が無い")
        exclude[g] = False

    for e in manifest['filesystem']['files']:
        where = f"{e.get('_manifest', '?')}: {e['host']}"
        tags = e.get('tags') or []
        if e['host'].startswith(TEST_HOST_PREFIX) and TEST_TAG not in tags:
            problems.append(f"{where}: {TEST_HOST_PREFIX} 由来なのにタグが "
                            f"{tags} ({TEST_TAG!r} にすること)")
        targets = sorted({tag_to_pkg[t] for t in tags if t in tag_to_pkg})
        unknown = [t for t in tags if t not in tag_to_pkg]
        pairs = dm.resolve_entry(e, base_dir)
        live = []
        for host, guest in pairs:
            if guest in exclude:
                exclude[guest] = True
            else:
                live.append((host, guest))
        if not live:
            continue
        if unknown:
            problems.append(f"{where}: どのパッケージにも当たらないタグ {unknown}")
        if len(targets) != 1:
            problems.append(f"{where}: タグ {tags} が当たるパッケージが "
                            f"{len(targets)} 個 ({targets}) — ちょうど 1 つにすること")
            continue
        by_name[targets[0]].extend((g, h) for h, g in live)

    for g, used in exclude.items():
        if not used:
            problems.append(f"除外 {g!r} が配備マニフェストのどれにも当たらない (古い除外)")

    seen = {}
    for name, _, files in pkgs:
        for g, h in files:
            if g in seen:
                problems.append(f"ゲストパス {g} が 2 回 ({seen[g]} と {name})")
            seen[g] = name

    # BOOT のローダは配備マニフェストの boot: と同じ物であること
    loader = (manifest.get('boot') or {}).get('loader')
    for name, pdef, files in pkgs:
        if pdef.get('type') == 'boot' and loader and \
                loader not in [h for _, h in files]:
            problems.append(f"{name}: 配備マニフェストの boot.loader {loader} を含まない")
    return pkgs, problems


def entry_dirs(guest_paths):
    """build_pkg が作るディレクトリ項目の集合"""
    dirs = set()
    for g in guest_paths:
        parts = g.split('/')
        for i in range(2, len(parts)):
            dirs.add('/'.join(parts[:i]))
    return dirs


def split_files(name, files, limit=PKG_MAX_ENTRIES):
    """項目数 (ファイル + ディレクトリ) が limit 以下になるよう順に切る

    戻り値: [(name, files), (name2, files), ...]。1 つ目だけ連番なし。
    """
    chunks = []
    cur = []
    for f in files:
        trial = cur + [f]
        if cur and len(trial) + len(entry_dirs(g for g, _ in trial)) > limit:
            chunks.append(cur)
            cur = [f]
        else:
            cur = trial
    if cur:
        chunks.append(cur)
    return [(name if i == 0 else f"{name}{i + 1}", c)
            for i, c in enumerate(chunks)]


def expand_plan(plan, base_dir, manifest=None):
    """plan_packages + 分割。[(pkg_name, version, lzss, [(guest, host_abs)])] と problems"""
    pkgs, problems = plan_packages(plan, base_dir, manifest)
    out = []
    for name, pdef, files in pkgs:
        if not files:
            problems.append(f"{name}: 中身が 0 件")
            continue
        files = [(g, os.path.join(base_dir, h)) for g, h in files]
        version = int(pdef.get('version', 1))
        use_lzss = bool(pdef.get('lzss', False))
        parts = split_files(name, files)
        if pdef.get('type') == 'boot' and len(parts) > 1:
            problems.append(f"{name}: boot 型は分割できない")
        if len(parts) > 9:
            problems.append(f"{name}: {len(parts)} 分割 (連番は 1 桁まで)")
        for pname, pfiles in parts:
            out.append((pname, version, use_lzss, pfiles))
    return out, problems


# ======================================================================== #
#  起動 FD (build/image.mk) — 中身は CD の BOOT + MINIMAL と同じ集合
# ======================================================================== #
# FD の中身を手で並べると CD の MINIMAL とずれる (2026-09-24 まで image.mk の
# FDD_MIN_CMDS が別の一覧だった)。packages.yaml の fd: が「どのパッケージを
# 載せるか (from)」「FAT に置けないので名前を変える物 (rename)」「FD だけの
# 起動用の物 (only)」を決め、それ以外は足しも引きもしない。

FD_LOADER_TOKEN = '{loader}'
# FatFs (FF_USE_LFN 0、CP437) の短い名前に使える文字 (英小文字は大文字に畳む)
_FAT83_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-~!#$%&'(){}@^`")


def fat83_ok(path):
    """全成分が 8.3 に収まるか (mkfat12 は収まらない名前を黙って切り詰める)"""
    parts = path.strip('/').split('/')
    for p in parts:
        base, dot, ext = p.partition('.')
        if not (1 <= len(base) <= 8) or len(ext) > 3 or '.' in ext \
                or (dot and not ext):
            return False
        if any(c.upper() not in _FAT83_CHARS for c in base + ext):
            return False
    return True


def fd_plan(plan, base_dir, loader=None, manifest=None):
    """FD に載せる [(fd_path, host_rel)] と problems (ファイルの有無は見ない)

    並びは only (定義順) → from の各パッケージ (定義順)。LOADER.BIN を先頭に
    置くのは IPL (boot/boot_fat*.asm) が読む回数を小さく保つため。
    """
    problems = []
    fd = plan.get('fd')
    if not isinstance(fd, dict):
        return [], ['packages.yaml に fd: が無い']
    pkgs, probs = plan_packages(plan, base_dir, manifest)
    problems += probs
    by_name = {n: f for n, _, f in pkgs}

    rename = {}
    for r in fd.get('rename') or []:
        if not (r.get('reason') or '').strip():
            problems.append(f"fd.rename {r.get('guest')!r} に理由 (reason:) が無い")
        if r['guest'] in rename:
            problems.append(f"fd.rename {r['guest']!r} が 2 回 ({rename[r['guest']]} と {r['fd']})")
        rename[r['guest']] = r['fd']
    used = set()

    out = []
    for o in fd.get('only') or []:
        if not (o.get('reason') or '').strip():
            problems.append(f"fd.only {o.get('fd')!r} に理由 (reason:) が無い")
        host = o['host']
        if host == FD_LOADER_TOKEN:
            if not loader:
                problems.append(f"fd.only {o['fd']}: ローダ ({FD_LOADER_TOKEN}) の指定が無い")
                continue
            host = loader
        out.append((o['fd'], host))
    for name in fd.get('from') or []:
        if name not in by_name:
            problems.append(f"fd.from {name!r} が packages: に無い")
            continue
        for g, h in by_name[name]:
            if g in rename:
                used.add(g)
                g = rename[g]
            out.append((g, h))
    for g in rename:
        if g not in used:
            problems.append(f"fd.rename {g!r} が fd.from のどれにも無い (古い rename)")

    seen = {}
    for g, _ in out:
        key = g.upper()
        if key in seen:
            problems.append(f"FD のパス {g} が 2 回 ({seen[key]})")
        seen[key] = g
        if not fat83_ok(g):
            problems.append(f"FD のパス {g} が 8.3 に収まらない (FAT は LFN なし。"
                            "fd.rename で短い名前を決めること)")
    return out, problems


def build_from_plan(plan_path, output_dir, base_dir):
    plan = load_plan(plan_path)
    resolved, problems = expand_plan(plan, base_dir)
    problems += [f"{h} not found (package '{n}')" for n, _, _, fs in resolved
                 for _, h in fs if not os.path.isfile(h)]
    if problems:
        for p in problems:
            print(f"ERROR: {p}", file=sys.stderr)
        raise SystemExit(1)
    write_packages(resolved, output_dir, base_dir, prune=True)


def write_packages(resolved, output_dir, base_dir, prune=False):
    """解決済みのパッケージを書く。prune なら出力先の他の *.PKG を消す
    (ISO は出力先のディレクトリを丸ごと焼くので、名前の変わった古い PKG が
    媒体に残らないように)。"""
    kapi_ver = read_kapi_version(base_dir)
    os.makedirs(output_dir, exist_ok=True)
    written = set()
    for pkg_name, version, use_lzss, files in resolved:
        out_name = pkg_name.upper() + '.PKG'
        out_path = os.path.join(output_dir, out_name)
        pkg_data = build_pkg(pkg_name, version, files, use_lzss, kapi_ver)
        with open(out_path, 'wb') as f:
            f.write(pkg_data)
        written.add(out_name)
        nent = len(files) + len(entry_dirs(g for g, _ in files))
        ratio = ''
        if use_lzss:
            orig = sum(os.path.getsize(h) for _, h in files)
            if orig > 0:
                ratio = f' ({len(pkg_data)*100//orig}%)'
        print(f"  {out_name}: {len(files)} files, {nent} entries, "
              f"{len(pkg_data)} bytes{ratio}")
    if prune:
        for old in sorted(os.listdir(output_dir)):
            if old.upper().endswith('.PKG') and old not in written:
                os.remove(os.path.join(output_dir, old))
                print(f"  removed stale {old}")


# ======================================================================== #
#  読み手 (ホストでの検査用。消費側 userland/lib/rt/pkg.c と同じ解釈)
# ======================================================================== #

def lzss_decode(data):
    orig = struct.unpack_from('<I', data, 0)[0]
    out = bytearray()
    text_buf = bytearray(b' ' * (N + F - 1))
    r = N - F
    p = 4
    while len(out) < orig and p < len(data):
        flags = data[p]
        p += 1
        for bit in range(8):
            if len(out) >= orig or p >= len(data):
                break
            if flags & (1 << bit):
                c = data[p]
                p += 1
                out.append(c)
                text_buf[r] = c
                r = (r + 1) % N
            else:
                i = data[p] | ((data[p + 1] & 0xF0) << 4)
                j = (data[p + 1] & 0x0F) + THRESHOLD
                p += 2
                for k in range(j + 1):
                    c = text_buf[(i + k) % N]
                    out.append(c)
                    text_buf[r] = c
                    r = (r + 1) % N
    return bytes(out[:orig])


def read_pkg(path):
    """PKG を読む。戻り値: (header dict, [(path, type, bytes or None)])"""
    with open(path, 'rb') as f:
        blob = f.read()
    if blob[0:4] != PKG_MAGIC:
        raise ValueError(f"{path}: bad magic")
    hdr = {
        'name': blob[4:12].rstrip(b'\x00').decode('utf-8'),
        'version': blob[12],
        'flags': blob[13],
        'kapi_ver': struct.unpack_from('<H', blob, 14)[0],
        'entry_count': struct.unpack_from('<H', blob, 16)[0],
        'orig_size': struct.unpack_from('<I', blob, 18)[0],
        'comp_size': struct.unpack_from('<I', blob, 22)[0],
    }
    p = PKG_HEADER_SIZE
    table = []
    while True:
        n = blob[p]
        p += 1
        if n == 0:
            break
        name = blob[p:p + n].decode('utf-8')
        p += n
        size = struct.unpack_from('<I', blob, p)[0]
        ftype = blob[p + 4]
        p += 5
        table.append((name, size, ftype))
    if len(table) != hdr['entry_count']:
        raise ValueError(f"{path}: entry_count {hdr['entry_count']} != {len(table)}")
    data = blob[p:p + hdr['comp_size']]
    if len(data) != hdr['comp_size']:
        raise ValueError(f"{path}: truncated data")
    if hdr['flags'] & PKG_FLAG_LZSS:
        data = lzss_decode(data)
    if len(data) != hdr['orig_size']:
        raise ValueError(f"{path}: orig_size {hdr['orig_size']} != {len(data)}")
    entries = []
    off = 0
    for name, size, ftype in table:
        if ftype == PKG_TYPE_FILE:
            entries.append((name, ftype, data[off:off + size]))
            off += size
        else:
            entries.append((name, ftype, None))
    if off != len(data):
        raise ValueError(f"{path}: data length {len(data)} != sum of files {off}")
    return hdr, entries


def parse_simple_yaml(path):
    """
    簡易YAMLパーサー (PyYAML不要)
    対応構造:
      pkg_name:
        key: value
        files:
          - host: path
            guest: path
    """
    packages = {}
    current_pkg = None
    current_file = None
    in_files = False

    with open(path, 'r') as f:
        for line in f:
            stripped = line.rstrip('\n')
            if not stripped or stripped.lstrip().startswith('#'):
                continue

            indent = len(stripped) - len(stripped.lstrip())
            content = stripped.strip()

            # トップレベル (indent 0): パッケージ名
            if indent == 0 and content.endswith(':'):
                current_pkg = content[:-1].strip()
                packages[current_pkg] = {'files': []}
                current_file = None
                in_files = False

            # パッケージ属性 (indent 2)
            elif indent == 2 and current_pkg:
                if content == 'files:':
                    in_files = True
                    current_file = None
                elif ':' in content:
                    k, v = content.split(':', 1)
                    k = k.strip()
                    v = v.strip().strip('"').strip("'")
                    if v.lower() == 'true':
                        v = True
                    elif v.lower() == 'false':
                        v = False
                    packages[current_pkg][k] = v
                    in_files = False

            # ファイルリスト (indent 4): "- host: ..."
            elif indent == 4 and current_pkg and in_files:
                if content.startswith('- host:'):
                    host_val = content[len('- host:'):].strip().strip('"').strip("'")
                    current_file = {'host': host_val, 'guest': ''}
                    packages[current_pkg]['files'].append(current_file)

            # ファイル属性 (indent 6): "guest: ..."
            elif indent == 6 and current_file is not None:
                if content.startswith('guest:'):
                    current_file['guest'] = content[len('guest:'):].strip().strip('"').strip("'")

    return packages


def main():
    parser = argparse.ArgumentParser(description='OS32 Package (.pkg) Builder')
    parser.add_argument('--plan',
                        help='CD 媒体の構成 (build/packages.yaml)。中身は配備マニフェストのタグから決める')
    parser.add_argument('--check-plan', action='store_true',
                        help='--plan の振り分けだけ検査する (ファイルは読まない)')
    parser.add_argument('--fd-args', action='store_true',
                        help='--plan の fd: から起動 FD の mkfat12 --tree 引数 (fd=host …) を出す')
    parser.add_argument('--fd-loader',
                        help='--fd-args の LOADER.BIN のもと (2HD / 1.44MB で違う)')
    parser.add_argument('--list', nargs='+', metavar='PKG',
                        help='PKG の中身を一覧する')
    parser.add_argument('--defs', action='append', default=None,
                        help='Package definitions YAML file (層ごとに複数指定できる。同名パッケージはマージされる)')
    parser.add_argument('--output', '-o', default='packages/',
                        help='Output directory or file')
    parser.add_argument('--name', help='Package name (single pkg mode)')
    parser.add_argument('--version', type=int, default=1,
                        help='Package version')
    parser.add_argument('--lzss', action='store_true',
                        help='Enable LZSS compression')
    parser.add_argument('--base', default='.',
                        help='Base directory for host paths')
    parser.add_argument('files', nargs='*',
                        help='guest=host file mappings (single pkg mode)')
    args = parser.parse_args()

    if args.list:
        for path in args.list:
            hdr, entries = read_pkg(path)
            nfile = sum(1 for _, t, _ in entries if t == PKG_TYPE_FILE)
            print(f"{path}: name={hdr['name']} v{hdr['version']} "
                  f"flags=0x{hdr['flags']:02x} kapi={hdr['kapi_ver']} "
                  f"entries={hdr['entry_count']} files={nfile} "
                  f"orig={hdr['orig_size']} comp={hdr['comp_size']}")
            for name, t, data in entries:
                if t == PKG_TYPE_FILE:
                    print(f"  {len(data):>9}  {name}")
                else:
                    print(f"  {'<dir>':>9}  {name}")
    elif args.plan and args.fd_args:
        files, problems = fd_plan(load_plan(args.plan), args.base, args.fd_loader)
        problems += [f"{h} not found (FD {g})" for g, h in files
                     if not os.path.isfile(os.path.join(args.base, h))]
        if problems:
            for p in problems:
                print(f"ERROR: {p}", file=sys.stderr)
            sys.exit(1)
        print(' '.join(f"{g}={h}" for g, h in files))
    elif args.plan and args.check_plan:
        resolved, problems = expand_plan(load_plan(args.plan), args.base)
        for p in problems:
            print(f"ERROR: {p}", file=sys.stderr)
        for n, _, _, fs in resolved:
            nent = len(fs) + len(entry_dirs(g for g, _ in fs))
            print(f"  {n.upper()}.PKG: {len(fs)} files, {nent} entries")
        sys.exit(1 if problems else 0)
    elif args.plan:
        print(f"Building packages from {args.plan} (+ deploy manifests)...")
        build_from_plan(args.plan, args.output, args.base)
    elif args.defs:
        print("Building packages from {}...".format(", ".join(args.defs)))
        build_from_yaml(args.defs, args.output, args.base)
    elif args.name and args.files:
        kapi_ver = read_kapi_version(args.base)
        files = []
        for mapping in args.files:
            guest, host = mapping.split('=', 1)
            files.append((guest, host))
        pkg_data = build_pkg(args.name, args.version, files,
                             args.lzss, kapi_ver)
        with open(args.output, 'wb') as f:
            f.write(pkg_data)
        print(f"Created {args.output}: {len(pkg_data)} bytes")
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()
