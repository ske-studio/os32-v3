#!/usr/bin/env python3
"""
hostdrv_deploy.py — HostDrv共有ディレクトリへのデプロイスクリプト

NP21/W の HostDrv 機能を利用し、ビルド成果物をホスト共有ディレクトリ
(C:/os32 = WSL: /mnt/c/os32) に配置する。
ゲストOS32は /host マウントポイント経由で直接アクセスできる。

sudo 不要。NHDイメージ操作不要。プログラム変更時は NP21/W 再起動不要。
カーネル変更時のみ nhd_deploy.py でブート領域書き込み + 再起動が必要。

使い方:
  python3 hostdrv_deploy.py sync [--tag TAG] [--no-manifest]
                                               — deploy.yaml に基づくデプロイ
  python3 hostdrv_deploy.py diff               — ビルド成果物との差分表示
  python3 hostdrv_deploy.py clean              — HostDrvディレクトリをクリア
  python3 hostdrv_deploy.py ls [path]          — HostDrvディレクトリ一覧

配備の**世代の名札** (票 H4、docs/archive/shell/TASK_H4.md §2-1 / §2-2):
`.deploy/manifest.txt` に行指向の平文で「この配備元がどの版か」を書き残す。
ゲストの `hsync` がこれを読み、`--expect-build` で食い違いを断る。
**全件成功の後にだけ書き、1 件でも失敗したら既にある名札を消す。**
"""

import subprocess
import sys
import os
import datetime
import errno
import stat
import shutil
import glob as globmod
import yaml
import filecmp
import zlib

# 通常配備が /etc/settings.db* を作らない・上書きしない・消さないための共通判定
# (票 S0-D / D0)。HostDrv は hsync でそのまま NHD へ流れるので、ここに古い
# settings.db が置かれるだけで本体を潰す道ができる。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deploy_protect as protect

# === パス設定 ===

# プロジェクトルート (tools/ の親ディレクトリ)
PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 配備定義は層別 (build/core.yaml, userland/, apps/, game/)。実体は
# tools/deploy_manifests.py。以前はここが削除済みの tools/deploy.yaml を
# 単独で見ており、`make deploy` が無言で何もしない状態だった。
from deploy_manifests import DEPLOY_MANIFESTS, load_merged as _load_merged


def _resolve_hostdrv_dir():
    """HOSTDRV_DIRを .env から解決する"""
    if os.environ.get('HOSTDRV_DIR'):
        return os.environ['HOSTDRV_DIR']
    for env_file in ['.env', '.env.sample']:
        env_path = os.path.join(PROJ_DIR, env_file)
        if os.path.isfile(env_path):
            with open(env_path, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('HOSTDRV_DIR='):
                        return line.split('=', 1)[1].strip()
    return "/mnt/c/os32"


HOSTDRV_DIR = _resolve_hostdrv_dir()


# === 配備の名札 (票 H4 §2-1 / §2-2) ===
#
# 形式は**行指向の平文** (ユーザー決裁 D1 2026-09-16)。ゲストに JSON パーサが
# 無いので、`key=value` の 4 行 + `---` + 1 行 1 ファイルにする。
#
#   format=2
#   build=9742a6b+dirty
#   generated=2026-09-16T21:45:19Z
#   kapi=1208
#   kapi_version=63
#   count=198
#   ---
#
# format=2 (票 TASK_KAPI_DATA_FIELDS、KAPI v63): `kapi=` は配備物の KernelAPI
# データ欄のオフセット (10 進、OS32X ヘッダ v3 の kapi_data_off)、
# `kapi_version=` は配備物を作った KAPI 版 (sdk/kapi.json)。ゲストの hsync は
# 配置がカーネルと違う / 版がカーネルより新しい / 欠けている名札を既定で断る。
# `kapi=` は**配備した OS32X バイナリのヘッダから**取る: v3 でないもの・値の
# 食い違うものが 1 つでもあれば名札を書かない (= 配備失敗。古い成果物の
# 混入を「確かめた」ことにしない)。
#   bin/cat.bin 16428 3b7f2a10 1789520013
#
# ファイルの行は **パス / サイズ / CRC-32 (8 桁 16 進、小文字) / mtime (Unix 秒)**
# で、区切りは空白 1 つ。パスは HOSTDRV_DIR からの相対で、絶対パスと `..` は
# 書かない。CRC は lib/crc32_core.inc と同じ CRC-32 (= zlib.crc32)。
MANIFEST_DIR = '.deploy'
MANIFEST_NAME = 'manifest.txt'
MANIFEST_FORMAT = '2'
MANIFEST_SEP = '---'
MANIFEST_HEAD_FMT = 'format=%s\nbuild=%s\ngenerated=%s\nkapi=%d\nkapi_version=%d\ncount=%d\n' \
                    + MANIFEST_SEP + '\n'
# `build` の値は読む側 (hsync の HS_MAN_PATH_CAP) に収まる必要がある。
MANIFEST_BUILD_CAP = 63


def manifest_path():
    return os.path.join(HOSTDRV_DIR, MANIFEST_DIR, MANIFEST_NAME)


def manifest_tmp_path():
    return manifest_path() + '.tmp'


def remove_manifest(why):
    """既にある名札 (と書きかけの一時ファイル) を消す。

    **古い名札が残ると「配備済み」と誤読される** (票 H4 §2-2)。消せなかった
    ことは握り潰さず表示する。戻り値 True = 名札が残っていない。
    """
    ok = True
    removed = False
    for path in (manifest_path(), manifest_tmp_path()):
        try:
            os.remove(path)
            removed = True
        except OSError as exc:
            if exc.errno != errno.ENOENT:
                print("Error: 名札 {} を消せない: {}".format(path, exc),
                      file=sys.stderr)
                ok = False
    if removed:
        print("  名札を削除: {} ({})".format(
            MANIFEST_DIR + '/' + MANIFEST_NAME, why))
    return ok


def sync_failed():
    """配備が失敗したときの共通の後始末 (票 H4 §2-2)。

    **1 件でも失敗したら既にある名札を消す。** 呼び手はこの戻り値をそのまま
    返す (失敗は握り潰さない — 既存の `state == 'error'` の扱いは変えない)。
    """
    remove_manifest("配備が失敗した")
    return False


def _git(args):
    """PROJ_DIR で git を回す。使えなければ None (配備は失敗させない)。"""
    try:
        out = subprocess.run(['git'] + args, cwd=PROJ_DIR,
                             capture_output=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    data = out.stdout
    if data is None:
        return ''
    if isinstance(data, bytes):
        return data.decode('utf-8', 'replace')
    return data


def build_id():
    """版の名札 `<短い SHA>` (+ 作業ツリーが汚れていれば `+dirty`)。

    **これは「同じか違うか」を見るための名札で、順序を表さない** (票 H4 §2-1)。
    日時や SHA の大小で新旧を決めないこと。git が無い / 使えないときは
    `unknown` を返す — 名札が無いより「確かめられない版」と言うほうが正直。
    """
    sha = _git(['rev-parse', '--short', 'HEAD'])
    if sha is None:
        return 'unknown'
    sha = sha.strip()
    if not sha or len(sha) > MANIFEST_BUILD_CAP or any(c.isspace() for c in sha):
        return 'unknown'
    dirty = _git(['status', '--porcelain'])
    if dirty is None:
        return sha
    if dirty.strip():
        sha += '+dirty'
    return sha if len(sha) <= MANIFEST_BUILD_CAP else 'unknown'


def manifest_path_ok(rel):
    """名札に書けるパスか (票 H4 §2-1)。

    空白を含むパスは**配備対象に無い**ことをここで固定する: 区切りが空白 1 つ
    なので、含まれると読む側が別の意味に取る。絶対パスと `..` も書かない。
    """
    if not rel or rel.startswith('/'):
        return False
    if '\\' in rel:
        return False
    if any(c.isspace() for c in rel):
        return False
    parts = rel.split('/')
    if '' in parts or '.' in parts or '..' in parts:
        return False
    return True


def manifest_line(rel, dest):
    """1 ファイル分の行を作る。読めなければ OSError をそのまま投げる。"""
    crc = 0
    with open(dest, 'rb') as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            crc = zlib.crc32(chunk, crc)
    st = os.stat(dest)
    return "%s %d %08x %d" % (rel, st.st_size, crc & 0xFFFFFFFF,
                              int(st.st_mtime))


# OS32X ヘッダ (sdk/os32x_hdr.py、mkos32x.py / mkshlib.py と共通)。
# SDK の場所はこのスクリプトから決める (試験は PROJ_DIR を差し替えるので)。
SDK_SRC_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'sdk')
sys.path.insert(0, SDK_SRC_DIR)
import os32x_hdr  # noqa: E402


def kapi_json_layout():
    """sdk/kapi.json の (データ欄のオフセット, 版)。"""
    import json
    with open(os.path.join(SDK_SRC_DIR, 'kapi.json'), encoding='utf-8') as f:
        kj = json.load(f)
    return 8 + 4 * int(kj['func_capacity']), int(kj['version'])


def deployed_kapi_layout(deployed):
    """配備した OS32X バイナリのヘッダから KAPI データ欄の配置を 1 つに決める。

    戻り値 (kapi_data_off, None) か (None, 理由)。OS32X でないファイル
    (データ・スクリプト) は見ない。OS32X が 1 本も無ければ sdk/kapi.json の値。
    v3 でないもの・値が食い違うものがあれば理由を返す (名札を書かない)。
    """
    want, _ver = kapi_json_layout()
    seen = {}
    for guest_path, dest in deployed:
        try:
            with open(dest, 'rb') as f:
                blob = f.read(os32x_hdr.OS32X_HDR_V3_SIZE)
        except OSError as exc:
            return None, "{} を読めない: {}".format(guest_path, exc)
        if len(blob) < 4 or int.from_bytes(blob[:4], 'little') != os32x_hdr.OS32X_MAGIC:
            continue
        try:
            h = os32x_hdr.parse_header(blob)
        except os32x_hdr.HeaderError:
            return None, "{} の OS32X ヘッダが短い".format(guest_path)
        off = h.get('kapi_data_off')
        if h['version'] < os32x_hdr.OS32X_HDR_VERSION or off is None:
            return None, ("{} はヘッダ v{} (KAPI 配置を持たない古い成果物) — "
                          "make clean で作り直す".format(guest_path, h['version']))
        seen.setdefault(off, guest_path)
    if not seen:
        return want, None
    if len(seen) != 1:
        return None, "配備物の KAPI 配置が食い違う: {}".format(
            ", ".join("0x%X (%s)" % (o, p) for o, p in sorted(seen.items())))
    off = next(iter(seen))
    if off != want:
        return None, ("配備物の KAPI 配置 0x{:X} が sdk/kapi.json (0x{:X}) と違う — "
                      "作り直す".format(off, want))
    return off, None


def write_manifest_file(deployed):
    """名札を書く。**全件成功の後にだけ呼ぶこと** (票 H4 §2-2)。

    deployed = [(guest_path, dest_abs)]。コピーしたものも「同一でスキップした」
    ものも**配備元に在る**ので、どちらも載せる。

    書き方は **一時ファイル + 置き換え**。中途半端な名札を読ませない。
    ただし §2-3-1 のとおり、**コピー群と名札の更新は原子的ではない** —
    守れるのは名札そのものの完全性だけ。
    """
    lines = []
    seen = set()
    try:
        for guest_path, dest in deployed:
            rel = guest_path.lstrip('/')
            if not manifest_path_ok(rel):
                print("Error: 名札に書けないパス (空白 / 絶対 / '..'): {}"
                      .format(guest_path), file=sys.stderr)
                return False
            if rel in seen:
                print("Error: 配備先が重複している: {}".format(guest_path),
                      file=sys.stderr)
                return False
            seen.add(rel)
            lines.append(manifest_line(rel, dest))
    except OSError as exc:
        print("Error: 名札の行を作れない: {}".format(exc), file=sys.stderr)
        return False

    # KAPI の配置 (票 TASK_KAPI_DATA_FIELDS)。確かめられなければ名札を書かない。
    kapi_off, why = deployed_kapi_layout(deployed)
    if kapi_off is None:
        print("Error: 名札に KAPI の配置を書けない: {}".format(why), file=sys.stderr)
        return False
    _off, kapi_ver = kapi_json_layout()

    # 並びを決めておく (同じ配備なら同じ名札になる = 差分が読める)
    lines.sort()
    build = build_id()
    generated = datetime.datetime.now(
        datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    head = MANIFEST_HEAD_FMT % (MANIFEST_FORMAT, build, generated,
                                kapi_off, kapi_ver, len(lines))
    text = head + ''.join(line + '\n' for line in lines)

    tmp = manifest_tmp_path()
    try:
        os.makedirs(os.path.dirname(manifest_path()), exist_ok=True)
        with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, manifest_path())
    except OSError as exc:
        print("Error: 名札を書けない: {}".format(exc), file=sys.stderr)
        try:
            os.remove(tmp)
        except OSError:
            pass
        return False

    print("  名札: {}/{} build={} kapi={}/v{} count={}".format(
        MANIFEST_DIR, MANIFEST_NAME, build, kapi_off, kapi_ver, len(lines)))
    return True


def guard_dest(guest_path, host_src=None):
    """最終パスを確定して保護判定する (HostDrv 側の共通入口)。

    Returns: (dest_abs, state)  state = 'ok' / 'protected' / 'error'
    内容比較 (同一ならコピーしない) より**前**に呼ぶこと。比較のために
    保護対象を開く必要もない。
    """
    try:
        dest, protected = protect.check_dest(HOSTDRV_DIR, guest_path, host_src)
    except protect.ProtectError as exc:
        print("Error: 配備の保護判定に失敗: {}".format(exc), file=sys.stderr)
        return None, 'error'
    if protected:
        protect.protect_log(guest_path)
        return dest, 'protected'
    return dest, 'ok'


def guard_root():
    """サブコマンドの**入口**で 1 回だけ通す前提検査。

    対象が 0 件の `sync --tag` では判定が 1 度も呼ばれず、`<root>/etc` が
    symlink / 別マウント / 通常ファイルでも成功で終わっていた (往復 2 の 8)。
    """
    try:
        protect.check_tree(HOSTDRV_DIR)
    except protect.ProtectError as exc:
        print("Error: 配備の前提検査に失敗: {}".format(exc), file=sys.stderr)
        return False
    return True


def ensure_dir(guest_dir):
    """ゲスト側ディレクトリを**各祖先まで判定してから**作る。

    `os.makedirs` は途中を黙って作るので、最終要素だけ見ても
    `/etc/settings.db/a` の `settings.db` がディレクトリとして生える
    (往復 1 の B2)。makedirs の失敗も握り潰さない (往復 1 の B6)。

    Returns: (dest_abs, state) — 'ok' / 'protected' (除外、失敗ではない) / 'error'
    """
    try:
        chain = protect.mkdir_chain(HOSTDRV_DIR, guest_dir)
    except protect.ProtectedPath as exc:
        protect.protect_log(exc.guest)
        return None, 'protected'
    except protect.ProtectError as exc:
        print("Error: 配備の保護判定に失敗: {}".format(exc), file=sys.stderr)
        return None, 'error'

    target = chain[-1] if chain else os.path.abspath(HOSTDRV_DIR)
    for path in chain:
        if os.path.isdir(path):
            continue
        try:
            os.mkdir(path)
        except OSError as exc:
            print("Error: mkdir {} 失敗: {}".format(path, exc), file=sys.stderr)
            return None, 'error'
        print("  mkdir {}".format(protect.guest_path_of(HOSTDRV_DIR, path)))
    return target, 'ok'


def load_deploy_yaml():
    """層ごとの配備定義をマージして返す (tools/deploy_manifests.py に委譲)"""
    return _load_merged()


def resolve_files_from_entry(entry):
    """deploy.yaml の files エントリ1件からホストパスとゲストパスのペアを生成

    Returns: list of (host_abs_path, guest_path)
    """
    host_pattern = entry['host']
    guest = entry['guest']
    entry_type = entry.get('type', 'file')
    exclude = entry.get('exclude', [])

    results = []

    if entry_type == 'glob':
        pattern = os.path.join(PROJ_DIR, host_pattern)
        matched = sorted(globmod.glob(pattern))
        for fpath in matched:
            basename = os.path.basename(fpath)
            if basename in exclude:
                continue
            if not os.path.isfile(fpath):
                continue
            if guest.endswith('/'):
                g = guest + basename
            else:
                g = guest
            results.append((fpath, g))
    else:
        fpath = os.path.join(PROJ_DIR, host_pattern)
        if os.path.isfile(fpath):
            g = guest + os.path.basename(fpath) if guest.endswith('/') else guest
            results.append((fpath, g))
        else:
            print("  Warning: {} not found".format(host_pattern))

    return results


def do_sync(tag_filter=None, write_manifest=True):
    """deploy.yaml に基づきファイルをHostDrvディレクトリにコピー

    write_manifest=False (`--no-manifest`) のときは名札を書かない。そのときも
    **古い名札は消す** — 「新しいファイル + 古い名札」を残すと
    `hsync --expect-build <古い ID>` が一致と判定し、H4 が防ぐはずの事故が
    そのまま裏返って起きる。抑止するのは「書くこと」であって「嘘を残すこと」
    ではない。

    タグで絞った部分配備 (`--tag`) も同じ理由で名札を書かず、古い名札を消す。
    名札は**配備元全体の世代**を表すもので、一部だけ入れ替えた配備元を
    1 つの版として名乗らせない。
    """
    cfg = load_deploy_yaml()
    if cfg is None:
        return False

    if not os.path.isdir(HOSTDRV_DIR):
        print("HostDrvディレクトリを作成: {}".format(HOSTDRV_DIR))
        os.makedirs(HOSTDRV_DIR, exist_ok=True)

    if not guard_root():
        return sync_failed()

    print("=" * 55)
    print("  OS32 HostDrv デプロイ")
    print("  {} 層のマニフェスト -> {}".format(len(DEPLOY_MANIFESTS), HOSTDRV_DIR))
    if tag_filter:
        print("  タグフィルタ: {}".format(tag_filter))
    print("=" * 55)

    fs = cfg.get('filesystem', {})

    # ディレクトリ構造作成
    if not tag_filter:
        dirs = fs.get('directories', [])
        for d in dirs:
            target, state = ensure_dir(d)
            if state == 'error':
                return sync_failed()

    # ファイルコピー
    files = fs.get('files', [])
    total_copied = 0
    total_skipped = 0
    total_protected = 0
    total_size = 0
    # 名札に載せる配備先 (票 H4 §2-2)。コピーしたものと「同一でスキップした」
    # ものの両方を集める — どちらも**配備元に在る**ので名札に載る。
    deployed = []

    for entry in files:
        entry_tags = entry.get('tags', [])

        if tag_filter and tag_filter not in entry_tags:
            continue

        pairs = resolve_files_from_entry(entry)
        if not pairs:
            continue

        tag_label = entry_tags[0] if entry_tags else 'other'

        for host_abs, guest_path in pairs:
            # 実コピー直前に最終パスで保護判定する。内容比較より前なので、
            # 保護対象は比較のためにすら開かない。
            dest_file, state = guard_dest(guest_path, host_src=host_abs)
            if state == 'error':
                return sync_failed()
            if state == 'protected':
                total_protected += 1
                continue

            # ゲスト側のディレクトリを確保 (各祖先まで判定してから作る)。
            # 親は**確定した最終パス**から取る (guest_path の字句ではない)。
            try:
                parent = protect.guest_path_of(
                    HOSTDRV_DIR, os.path.dirname(dest_file))
            except protect.ProtectError as exc:
                print("Error: 配備の保護判定に失敗: {}".format(exc),
                      file=sys.stderr)
                return sync_failed()
            dest_dir, dstate = ensure_dir(parent)
            if dstate == 'error':
                return sync_failed()
            if dstate == 'protected':
                total_protected += 1
                continue

            # 同一ファイルならスキップ (サイズ+内容比較)
            if os.path.isfile(dest_file):
                if filecmp.cmp(host_abs, dest_file, shallow=False):
                    total_skipped += 1
                    deployed.append((guest_path, dest_file))
                    continue

            # コピー
            shutil.copy2(host_abs, dest_file)
            size = os.path.getsize(host_abs)
            total_size += size
            total_copied += 1
            deployed.append((guest_path, dest_file))
            print("  [{}] {} ({} bytes)".format(
                tag_label, protect.guest_path_of(HOSTDRV_DIR, dest_file), size))

    print("")
    print("=" * 55)
    print("  完了! {} ファイル更新 ({:,} bytes), {} スキップ{}".format(
        total_copied, total_size, total_skipped,
        "、{} 件は保護対象として除外".format(total_protected)
        if total_protected else ""))

    # ---- 名札 (票 H4 §2-2) --------------------------------------------
    # **ここまで来たのは全件成功したときだけ。** 途中の失敗は sync_failed()
    # を通って戻っており、そこで既にある名札を消してある。
    if not write_manifest:
        remove_manifest("--no-manifest")
    elif tag_filter:
        remove_manifest("--tag の部分配備 (全体の世代を表せない)")
    elif not write_manifest_file(deployed):
        remove_manifest("名札を書けなかった")
        print("=" * 55)
        return False
    print("=" * 55)
    return True


def do_diff(tag_filter=None):
    """ビルド成果物とHostDrvディレクトリの差分を表示"""
    cfg = load_deploy_yaml()
    if cfg is None:
        return

    fs = cfg.get('filesystem', {})
    files = fs.get('files', [])

    changed = 0
    missing = 0
    same = 0

    for entry in files:
        entry_tags = entry.get('tags', [])
        if tag_filter and tag_filter not in entry_tags:
            continue

        pairs = resolve_files_from_entry(entry)
        for host_abs, guest_path in pairs:
            dest_file = os.path.join(HOSTDRV_DIR, guest_path.lstrip('/'))

            if not os.path.isfile(dest_file):
                print("  [NEW]     {}".format(guest_path))
                missing += 1
            elif not filecmp.cmp(host_abs, dest_file, shallow=False):
                src_size = os.path.getsize(host_abs)
                dst_size = os.path.getsize(dest_file)
                print("  [CHANGED] {} ({} -> {} bytes)".format(
                    guest_path, dst_size, src_size))
                changed += 1
            else:
                same += 1

    print("")
    print("変更: {}, 新規: {}, 同一: {}".format(changed, missing, same))


def _clean_tree(path):
    """path の中身を消す。保護対象を 1 つでも抱えていたら True を返す。

    走査は **top-down**。`os.walk(topdown=False)` は保護対象名のディレクトリ
    (`etc/settings.db/` の残骸) の中身を先に消してしまうし、symlink 分岐を
    保護判定より前に置くと `etc/settings.db -> どこか` を無判定で unlink する
    (往復 1 の B3)。判定 → symlink → ディレクトリ → ファイル の順で見る。

    失敗 (EACCES / EIO / ENOSPC) は OSError のまま上へ投げる。「保護対象を
    抱えているから消せなかった」と混同しない (往復 1 の B6)。
    """
    keep = False
    names = sorted(os.listdir(path))
    # ルート直下の `lost+found` (ext2 が作る root 所有) には降りない。
    # check_tree と同じ規則を使う (HostDrv には普通は無いが揃えておく)。
    protect.skip_root_entries(HOSTDRV_DIR, path, names)
    for name in names:
        full = os.path.join(path, name)
        # 1) symlink は入口の check_tree で拒否済み。競合などで現れたら
        #    「未対応の配置」として中止する (中間リンクを無判定で外さない)。
        if os.path.islink(full):
            raise protect.ProtectError(
                '配備ツリーに symlink がある: {} (配備を中止する)'.format(full))
        # 2) 保護判定が最初 (ディレクトリでも消す前に必ず見る)
        if protect.is_protected(HOSTDRV_DIR, full):
            protect.protect_log(protect.guest_path_of(HOSTDRV_DIR, full))
            keep = True
            continue
        # 3) ディレクトリは降りてから、空になったときだけ rmdir
        if os.path.isdir(full):
            if _clean_tree(full):
                keep = True
            else:
                os.rmdir(full)
            continue
        os.remove(full)
    return keep


def do_clean():
    """HostDrvディレクトリの中身を削除する (保護対象と、それを含む祖先は残す)

    以前は `shutil.rmtree(root/etc)` で /etc ごと消していた。rmtree は判定の
    余地なく木を落とすので、settings.db の保護をどこに書いても効かない。
    エントリごとに消し、保護対象とその祖先ディレクトリだけ残す (往復 3 の 4)。
    """
    # `os.path.isdir` は EACCES / EIO を False に丸めるので、読めないだけの
    # HostDrv を「存在しません」と言って**成功で終えて**いた (追加往復 2)。
    # ENOENT (確定した不存在) だけ「何もしない = 成功」。
    try:
        st = os.lstat(HOSTDRV_DIR)
    except OSError as exc:
        if exc.errno == errno.ENOENT:
            print("HostDrvディレクトリが存在しません: {}".format(HOSTDRV_DIR))
            return True
        print("Error: {} を stat できない: {}".format(HOSTDRV_DIR, exc),
              file=sys.stderr)
        return False
    if not stat.S_ISDIR(st.st_mode):
        print("Error: {} がディレクトリではない".format(HOSTDRV_DIR),
              file=sys.stderr)
        return False

    try:
        # <root>/etc がすり替わっている / ツリーに symlink があれば clean も
        # 拒否する (往復 1 の B3、往復 3 の PM 方針)。
        protect.check_tree(HOSTDRV_DIR)
        kept = _clean_tree(HOSTDRV_DIR)
    except protect.ProtectError as exc:
        print("Error: 保護判定に失敗したので clean を中止: {}".format(exc),
              file=sys.stderr)
        return False
    except OSError as exc:
        print("Error: clean に失敗: {}".format(exc), file=sys.stderr)
        return False

    if kept:
        print("クリア完了 (保護対象とその祖先は残した): {}".format(HOSTDRV_DIR))
    else:
        print("クリア完了: {}".format(HOSTDRV_DIR))
    return True


def do_ls(path='/'):
    """HostDrvディレクトリの一覧表示"""
    target = os.path.join(HOSTDRV_DIR, path.lstrip('/'))
    if not os.path.exists(target):
        print("Error: {} not found".format(path), file=sys.stderr)
        return

    if os.path.isfile(target):
        size = os.path.getsize(target)
        print("{} ({} bytes)".format(path, size))
        return

    for item in sorted(os.listdir(target)):
        full = os.path.join(target, item)
        if os.path.isdir(full):
            print("  {}/".format(item))
        else:
            size = os.path.getsize(full)
            print("  {} ({} bytes)".format(item, size))


def main():
    if len(sys.argv) < 2:
        print("HostDrv Deploy Tool")
        print("")
        print("使い方: {} <command>".format(sys.argv[0]))
        print("")
        print("  sync [--tag TAG] [--no-manifest]")
        print("                    — 層別マニフェストに基づくデプロイ")
        print("                      (--no-manifest で .deploy/manifest.txt を書かない)")
        print("  diff [--tag TAG]  — ビルド成果物との差分表示")
        print("  clean             — HostDrvディレクトリをクリア")
        print("  ls [path]         — ファイル一覧")
        print("")
        print("パス:")
        print("  HostDrv:     {}".format(HOSTDRV_DIR))
        print("  マニフェスト: {}".format(", ".join(DEPLOY_MANIFESTS)))
        print("  Project:     {}".format(PROJ_DIR))
        return

    cmd = sys.argv[1]

    if cmd == 'sync':
        tag_filter = None
        want_manifest = True
        i = 2
        while i < len(sys.argv):
            if sys.argv[i] == '--tag' and i + 1 < len(sys.argv):
                tag_filter = sys.argv[i + 1]
                i += 2
            elif sys.argv[i] == '--no-manifest':
                # 票 H4 §2-2: 調査時に名札を書かせない
                want_manifest = False
                i += 1
            else:
                i += 1
        if not do_sync(tag_filter=tag_filter, write_manifest=want_manifest):
            sys.exit(1)

    elif cmd == 'diff':
        tag_filter = None
        i = 2
        while i < len(sys.argv):
            if sys.argv[i] == '--tag' and i + 1 < len(sys.argv):
                tag_filter = sys.argv[i + 1]
                i += 2
            else:
                i += 1
        do_diff(tag_filter=tag_filter)

    elif cmd == 'clean':
        if not do_clean():
            sys.exit(1)

    elif cmd == 'ls':
        path = sys.argv[2] if len(sys.argv) > 2 else '/'
        do_ls(path)

    else:
        print("Unknown command: {}".format(cmd), file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
