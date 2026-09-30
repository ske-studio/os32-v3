#!/usr/bin/env python3
"""
nhd_deploy.py — NHDイメージのext2パーティションをmountして操作する

build/nhd/os32.nhd (NHD_LOCAL) をループデバイスでマウントし、通常のファイル操作で
デプロイする。NP21/Wへの反映は deploy コマンドでそれをWindows側にコピーする。
NHD_LOCAL が無ければ Windows 側から自動で取り込む (NP21/W 停止中のみ可能)。

前提:
  sudoers に以下が設定済み (NOPASSWD):
    /usr/bin/mount, /usr/bin/umount, /usr/sbin/losetup,
    /usr/sbin/e2fsck, /usr/sbin/mkfs.ext2, /usr/sbin/mke2fs

使い方:
  python3 nhd_deploy.py sync [--tag TAG]   — deploy.yaml に基づくフルデプロイ
  python3 nhd_deploy.py mount              — ext2パーティションをマウント
  python3 nhd_deploy.py umount             — アンマウント
  python3 nhd_deploy.py copy <src> [...]   — ファイルをext2にコピー
  python3 nhd_deploy.py copy-all <dir>     — dirの全.binをコピー
  python3 nhd_deploy.py ls [path]          — ファイル一覧
  python3 nhd_deploy.py rm <file>          — ファイル削除
  python3 nhd_deploy.py deploy             — umount + NHDをNP21/Wにコピー
  python3 nhd_deploy.py format             — ext2を再フォーマット (データ全消去)
  python3 nhd_deploy.py init               — Windows側NHDを/tmpにコピー+フォーマット+マウント
  python3 nhd_deploy.py migrate-pt [--no-push] — 旧配置の区画表を標準配置へ + ローダ + カーネル
"""

import sys
import os
import errno
import hashlib
import json
import subprocess
import shutil
import re
import struct
import glob as globmod
import yaml

# 通常配備が /etc/settings.db* を作らない・上書きしない・消さないための共通判定
# (票 S0-D / D0)。書く・消す・切り詰める直前に 1 か所で止める。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deploy_protect as protect
# 区画表の読み書き (drivers/pc98pt.c のホスト側の写し。票 TASK_HDD_INSTALL 段 1-4)
import pc98pt

# === パス設定 ===
# 作業用 NHD (ループマウントして書き込む側)。以前は /tmp/os32.nhd だったが、
# /tmp は WSL 再起動で消え、消えた状態で make deploy-nhd を走らせると何も
# 書かずに成功扱いになっていた (2026-09-04)。リポジトリ内の build/nhd/ に置く
# (gitignore 済み、make clean の対象外)。NHD_LOCAL は PROJ_DIR の後で決める。

# NP21W_DIR: 環境変数 → .env → .env.sample → デフォルト の順で解決
def _resolve_np21w_dir():
    """NP21W_DIRを.envから解決する"""
    if os.environ.get('NP21W_DIR'):
        return os.environ['NP21W_DIR']
    proj_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for env_file in ['.env', '.env.sample']:
        env_path = os.path.join(proj_dir, env_file)
        if os.path.isfile(env_path):
            with open(env_path, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('NP21W_DIR='):
                        return line.split('=', 1)[1].strip()
    return "/tmp/np21w"

NP21W_DIR = _resolve_np21w_dir()
NHD_REMOTE = os.path.join(NP21W_DIR, "os32.nhd")
MOUNT_POINT = "/tmp/os32"

# プロジェクトルート (tools/ の親ディレクトリ)
PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NHD_LOCAL = os.environ.get("OS32_NHD_LOCAL") or os.path.join(PROJ_DIR, "build", "nhd", "os32.nhd")
# 配備定義は所有する層ごとに分かれている。リストとマージ処理の実体は
# tools/deploy_manifests.py。二重に持つと食い違うので参照だけにすること。
# 各マニフェストは自層の成果物しか参照しない (make check-manifests で検査)。
from deploy_manifests import DEPLOY_MANIFESTS, load_merged as _load_merged

# === ext2パーティション オフセット ===
# NHDヘッダ(512B) + ブート領域(LBA 0-1631) = 1633セクタ
# シリンダ境界整列: 8H x 17SPT = 136sec/cyl, シリンダ12 = LBA 1632
# (-O0 SQLiteバイナリが最大LBA 1406まで使用するため拡張)
NHD_HEADER_SECTORS = 1
HDD_PARTITION_LBA = 1632
PARTITION_SKIP = NHD_HEADER_SECTORS + HDD_PARTITION_LBA  # 1633
PARTITION_OFFSET = PARTITION_SKIP * 512  # 836096 バイト (1633 * 512)


# === pull の来歴 (stamp) ===
# NHD 全体のコピー (deploy) はファイル単位の保護では守れない。稼働中のゲストが
# /etc/settings.db に書いた内容は remote 側にしかなく、古い local を上書きすると
# 丸ごと消える。そこで pull が「この local はこの remote から取った」来歴を残し、
# deploy はそれが崩れていないときだけ書く (票 S0-D / TASK_S0 §2、往復 2 の 8)。
STAMP_SUFFIX = '.pulled'


def stamp_path(local_path=None):
    return (local_path or NHD_LOCAL) + STAMP_SUFFIX


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def write_pull_stamp(local_path, remote_path):
    """pull が **全部成功した最後にだけ** 呼ぶ。remote の内容 hash まで残す。

    一時ファイルへ書いて fsync → rename の順に置く。途中で ENOSPC / EIO に
    なっても「半分書けた来歴」が残らない (往復 2 の 6)。失敗したら一時ファイルも
    既存の来歴も消して例外を投げる = pull 全体の失敗にする。
    """
    st = os.stat(remote_path)
    data = {
        'remote_path': os.path.abspath(remote_path),
        'remote_size': st.st_size,
        'remote_mtime': int(st.st_mtime),
        'remote_sha256': file_sha256(remote_path),
        'local_path': os.path.abspath(local_path),
    }
    final = stamp_path(local_path)
    tmp = final + '.tmp'
    try:
        with open(tmp, 'w') as f:
            json.dump(data, f, indent=1, sort_keys=True)
            f.flush()
            os.fsync(f.fileno())
        os.rename(tmp, final)
    except OSError:
        for path in (tmp, final):
            try:
                os.remove(path)
            except OSError as exc:
                if exc.errno != errno.ENOENT:
                    pass                # 消せなくても元の例外を優先する
        raise
    return data


def remove_pull_stamp(local_path=None):
    """失敗した pull は来歴を残さない (古い stamp も書きかけの tmp も消す)。"""
    for path in (stamp_path(local_path), stamp_path(local_path) + '.tmp'):
        try:
            os.remove(path)
        except OSError as exc:
            if exc.errno != errno.ENOENT:
                raise


def verify_pull_stamp(local_path=None, remote_path=None):
    """deploy が全体コピーしてよいかを判定する。

    Returns: (ok, reason)  reason は ok=False のときだけ意味がある。
    """
    local_path = local_path or NHD_LOCAL
    remote_path = remote_path or NHD_REMOTE
    sp = stamp_path(local_path)
    if not os.path.isfile(sp):
        return False, '来歴 {} が無い (pull していない / 消えた)'.format(sp)
    try:
        with open(sp) as f:
            data = json.load(f)
    except (ValueError, OSError) as exc:
        return False, '来歴 {} を読めない ({})'.format(sp, exc)
    # `[]` や `null` でも .get で落ちない (--force の判定まで進める、往復 2 の 7)
    if not isinstance(data, dict):
        return False, '来歴 {} が壊れている (オブジェクトではない)'.format(sp)
    if data.get('local_path') != os.path.abspath(local_path):
        return False, '来歴の local_path {!r} が今の {!r} と違う'.format(
            data.get('local_path'), os.path.abspath(local_path))
    if data.get('remote_path') != os.path.abspath(remote_path):
        return False, '来歴の remote_path {!r} が今の {!r} と違う'.format(
            data.get('remote_path'), os.path.abspath(remote_path))
    if not os.path.isfile(remote_path):
        return False, 'remote {} が無い'.format(remote_path)
    size = os.path.getsize(remote_path)
    if size != data.get('remote_size'):
        return False, 'remote のサイズが {} -> {} と変わっている'.format(
            data.get('remote_size'), size)
    # mtime を保ったまま中身だけ差し替えられても気づけるよう再ハッシュする。
    if file_sha256(remote_path) != data.get('remote_sha256'):
        return False, 'remote の内容が pull 時と違う (ゲストが書いた可能性)'
    return True, ''


def run_sync():
    """`sync` を実行して成否を返す。失敗を握り潰さない (票 S0-D、往復 2 の 9)。"""
    result = subprocess.run(['sync'], capture_output=True, text=True)
    if result.returncode != 0:
        print("Error: sync 失敗: {}".format((result.stderr or '').strip()),
              file=sys.stderr)
        return False
    return True


def guard_root(root=None):
    """サブコマンドの**入口**で 1 回だけ通す前提検査。

    対象が 0 件の `sync --tag` や stale の無い `prune --delete` では判定が 1 度も
    呼ばれないので、`<root>/etc` が symlink / 別マウント / 通常ファイルでも
    成功で終わっていた (往復 2 の 8)。エントリの有無に依らず必ず見る。
    """
    root = root if root is not None else MOUNT_POINT
    try:
        protect.check_tree(root)
    except protect.ProtectError as exc:
        print("Error: 配備の前提検査に失敗: {}".format(exc), file=sys.stderr)
        return False
    return True


def guard_dest(guest_path, host_src=None, root=None):
    """最終パスを確定して保護判定する (NHD 側の共通入口)。

    Returns: (dest_abs, state)
      state: 'ok' = 書いてよい / 'protected' = 除外 (失敗ではない)
             'error' = 判定できない (= 配備を失敗させる)
    """
    root = root if root is not None else MOUNT_POINT
    try:
        dest, protected = protect.check_dest(root, guest_path, host_src)
    except protect.ProtectError as exc:
        print("Error: 配備の保護判定に失敗: {}".format(exc), file=sys.stderr)
        return None, 'error'
    if protected:
        protect.protect_log(guest_path)
        return dest, 'protected'
    return dest, 'ok'


def ensure_dir(guest_dir, root=None):
    """ゲスト側ディレクトリを**各祖先まで判定してから**作る。

    `mkdir -p` は途中を黙って作るので、最終要素だけ見ても `/etc/settings.db/a`
    の `settings.db` がディレクトリとして生える (往復 1 の B2)。
    `mkdir` の失敗も握り潰さない (往復 1 の B6)。

    Returns: (dest_abs, state) — 'ok' / 'protected' (除外、失敗ではない) / 'error'
    """
    root = root if root is not None else MOUNT_POINT
    try:
        chain = protect.mkdir_chain(root, guest_dir)
    except protect.ProtectedPath as exc:
        protect.protect_log(exc.guest)
        return None, 'protected'
    except protect.ProtectError as exc:
        print("Error: 配備の保護判定に失敗: {}".format(exc), file=sys.stderr)
        return None, 'error'

    target = chain[-1] if chain else os.path.abspath(root)
    made = []
    for path in chain:
        if os.path.isdir(path):
            continue
        result = subprocess.run(['sudo', 'mkdir', '-p', '--', path],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print("Error: mkdir {} 失敗: {}".format(
                path, (result.stderr or '').strip()), file=sys.stderr)
            return None, 'error'
        made.append(path)
    for path in made:
        print("  mkdir {}".format(protect.guest_path_of(root, path)))
    return target, 'ok'


def is_mounted():
    """マウント済みかチェック"""
    result = subprocess.run(
        ['mountpoint', '-q', MOUNT_POINT],
        capture_output=True
    )
    return result.returncode == 0


def get_loop_device():
    """現在NHD_LOCALに紐づいているループデバイスを返す (なければNone)"""
    result = subprocess.run(
        ['losetup', '-j', NHD_LOCAL],
        capture_output=True, text=True
    )
    if result.returncode == 0 and result.stdout.strip():
        # "/dev/loop0: ..." のような出力
        return result.stdout.strip().split(':')[0]
    return None



def ensure_local_nhd():
    """NHD_LOCAL が無ければ Windows 側から取り込む (NP21/W 停止中のみ可能)。
    build/nhd/ は make clean の対象外だが、clone 直後や手で消した後は無い。"""
    if os.path.isfile(NHD_LOCAL):
        return True
    print("{} が無いので Windows 側 NHD から取り込みます".format(NHD_LOCAL))
    # 来歴は**入口で**消す。remote 欠損などの早期 return で古い stamp が残ると、
    # 中身の違う local で全体上書きが通ってしまう (往復 1 の B8)。
    remove_pull_stamp()
    if not os.path.isfile(NHD_REMOTE):
        print("Error: {} も見つかりません".format(NHD_REMOTE), file=sys.stderr)
        return False
    os.makedirs(os.path.dirname(NHD_LOCAL), exist_ok=True)
    try:
        shutil.copy2(NHD_REMOTE, NHD_LOCAL)
    except (OSError, PermissionError) as exc:
        print("Error: NHD を取り込めません: {}".format(exc), file=sys.stderr)
        print("  NP21/W がロックしている場合は先に kill してください",
              file=sys.stderr)
        print("  python3 tools/np21w_ctl.py stop   (プロセスが消えるまで待つ)",
              file=sys.stderr)
        remove_pull_stamp()
        return False
    write_pull_stamp(NHD_LOCAL, NHD_REMOTE)
    print("  取り込み完了 ({:.1f} MB)".format(os.path.getsize(NHD_LOCAL) / (1024 * 1024)))
    return True


# ensure_mounted_for_kernel() が立てる。立っている間の do_mount は、NHD を
# **取り込んだ後・マウントの前**に旧配置の門を通す (Codex ラリー 2 の 2)。
# 引数にしないのは、試験が do_mount / ensure_mounted を引数なしの贋物に
# 差し替えるため (tools/tests/test_deploy_protect.py ほか)。
_GUARD_NEXT_MOUNT = False


def do_mount():
    """ext2パーティションをマウント"""
    if is_mounted():
        print("既にマウント済みです: " + MOUNT_POINT)
        return True

    if not ensure_local_nhd():
        return False
    if _GUARD_NEXT_MOUNT and not legacy_pt_guard():
        return False

    # マウントポイント作成
    os.makedirs(MOUNT_POINT, exist_ok=True)

    # ループデバイス作成
    loop_dev = get_loop_device()
    if not loop_dev:
        result = subprocess.run(
            ['sudo', 'losetup', '-f', '--show',
             '--offset', str(PARTITION_OFFSET), NHD_LOCAL],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print("Error: losetup 失敗: " + result.stderr.strip(),
                  file=sys.stderr)
            return False
        loop_dev = result.stdout.strip()

    # マウント
    result = subprocess.run(
        ['sudo', 'mount', '-t', 'ext2', loop_dev, MOUNT_POINT],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("Error: mount 失敗: " + result.stderr.strip(), file=sys.stderr)
        # ループデバイスを解放
        subprocess.run(['sudo', 'losetup', '-d', loop_dev],
                       capture_output=True)
        return False



    print("マウント完了: {} -> {}".format(loop_dev, MOUNT_POINT))
    return True


def do_umount():
    """アンマウント"""
    if not is_mounted():
        print("マウントされていません")
        # ループデバイスが残っていたら解放
        loop_dev = get_loop_device()
        if loop_dev:
            subprocess.run(['sudo', 'losetup', '-d', loop_dev],
                           capture_output=True)
            print("ループデバイス {} を解放しました".format(loop_dev))
        return True

    # sync — 失敗をここで握り潰すと「書いたつもり」でイメージを配る
    if not run_sync():
        return False

    # アンマウント
    result = subprocess.run(
        ['sudo', 'umount', MOUNT_POINT],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("Error: umount 失敗: " + result.stderr.strip(),
              file=sys.stderr)
        return False

    # ループデバイス解放
    loop_dev = get_loop_device()
    if loop_dev:
        subprocess.run(['sudo', 'losetup', '-d', loop_dev],
                       capture_output=True)

    print("アンマウント完了")
    return True


def ensure_mounted():
    """マウントされていなければ自動マウントする"""
    if is_mounted():
        return True
    print("自動マウント中...")
    return do_mount()


def ensure_mounted_for_kernel():
    """v64 以降のカーネル・ユーザーランドを写す呼び手 (sync / sync-from-hostdrv) 用。

    既にマウント済みならその NHD を門に通し、未マウントなら do_mount が取り込み
    (ensure_local_nhd) の**後**・losetup の前に門を通す。migrate-pt の
    カーネルの写し (do_copy) は旧配置の NHD へ書くのが目的なので通常の
    ensure_mounted を使う。
    """
    global _GUARD_NEXT_MOUNT
    if is_mounted():
        return legacy_pt_guard() and ensure_mounted()
    _GUARD_NEXT_MOUNT = True
    try:
        return ensure_mounted()
    finally:
        _GUARD_NEXT_MOUNT = False


# === ディレクトリ構造定義 ===
SYS_DIRS = ['bin', 'sbin', 'usr', 'usr/bin', 'usr/man', 'data', 'etc',
            'home', 'home/user', 'tmp']


def do_mkdirs():
    """ext2上にシステムディレクトリを作成"""
    if not ensure_mounted():
        return False
    if not guard_root():
        return False
    for d in SYS_DIRS:
        target, state = ensure_dir('/' + d)
        if state == 'error':
            return False
    if not run_sync():
        return False
    print("Done! (system directories created)")
    return True


def do_copy(src_files, dest_dir='/', rename=None):
    """ファイルをマウント済みext2にコピー (sudo cp)

    dest_dir: コピー先ディレクトリ (例: '/bin', '/usr/bin')
    rename:   ファイル名を変更 (単一ファイルのみ有効)
    """
    if not ensure_mounted():
        return False
    if not guard_root():
        return False

    # コピー先ディレクトリの確保 (各祖先を判定、保護対象名は作らない)
    dest_base, state = ensure_dir(dest_dir)
    if state == 'error':
        return False
    if state == 'protected':
        # 除外は失敗ではない (往復 1 の B7)。何も書かずに成功で返る。
        print("Done! (0 files copied, 1 protected)")
        return True

    copied = 0
    failed = 0
    skipped = 0
    disp_dir = dest_dir if dest_dir.endswith('/') else dest_dir + '/'
    for src in src_files:
        if not os.path.isfile(src):
            print("Warning: {} not found, skipping".format(src))
            failed += 1
            continue
        if rename and len(src_files) == 1:
            dest_name = rename
        else:
            dest_name = os.path.basename(src)
        # CLI からも保護対象は書けない (ホストの道具で settings.db を書く道を作らない)
        dest_path, state = guard_dest(disp_dir + dest_name, host_src=src)
        if state == 'error':
            return False
        if state == 'protected':
            skipped += 1
            continue
        result = subprocess.run(
            ['sudo', 'cp', '--', os.path.abspath(src), dest_path],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print("Error copying {}: {}".format(dest_name, result.stderr.strip()),
                  file=sys.stderr)
            remove_partial(dest_path)
            failed += 1
            continue
        size = os.path.getsize(src)
        print("  {}{} ({} bytes)".format(disp_dir, dest_name, size))
        copied += 1

    # 進捗 (件数) と**全体の成否**を分ける。1 件成功・1 件失敗で
    # `Done! (1 files copied)` と出していた (追加往復 3)。
    tail = ", {} protected".format(skipped) if skipped else ""
    if failed:
        print("FAILED ({} copied, {} failed{})".format(copied, failed, tail),
              file=sys.stderr)
        return False
    if copied > 0:
        if not run_sync():
            return False
        print("Done! ({} files copied{})".format(copied, tail))
        return True
    if skipped:
        # 保護対象だけを指定された。除外は失敗ではない。
        print("Done! (0 files copied, {} protected)".format(skipped))
        return True
    print("Error: コピーするファイルがありません", file=sys.stderr)
    return False


def do_copy_all(src_dir, ext='.bin', dest_dir='/'):
    """ディレクトリ内の全ファイルをコピー"""
    pattern = os.path.join(src_dir, '*{}'.format(ext))
    files = sorted(globmod.glob(pattern))
    if not files:
        print("No {} files found in {}".format(ext, src_dir))
        return False
    print("=== Batch copy: {} files from {} to {} ===".format(
        len(files), src_dir, dest_dir))
    return do_copy(files, dest_dir=dest_dir)


def do_ls(path='/'):
    """ファイル一覧"""
    if not ensure_mounted():
        return False
    target = os.path.join(MOUNT_POINT, path.lstrip('/'))
    if not os.path.exists(target):
        print("Error: {} not found".format(path), file=sys.stderr)
        return False
    result = subprocess.run(
        ['ls', '-la', '--', target],
        capture_output=True, text=True
    )
    print(result.stdout)
    return result.returncode == 0


def do_rm(filename):
    """ファイル削除 (sudo rm)"""
    if not ensure_mounted():
        return False
    if not guard_root():
        return False
    target, state = guard_dest(filename)
    if state == 'error':
        return False
    if state == 'protected':
        # 除外は失敗ではない。ホストの道具で settings.db を消す道を作らない。
        return True
    if not os.path.exists(target):
        print("Error: {} not found".format(filename), file=sys.stderr)
        return False
    result = subprocess.run(['sudo', 'rm', '--', target],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print("Error: {} を消せなかった: {}".format(
            filename, (result.stderr or '').strip()), file=sys.stderr)
        return False
    if not run_sync():
        return False
    print("Removed: {}".format(filename))
    return True


def do_deploy(force=False):
    """アンマウント + NHDをNP21/Wにコピー

    全体コピーなのでファイル単位の保護では守れない。pull が残した来歴
    (`<local>.pulled`) が崩れていなければだけ書く (票 S0-D、往復 2 の 8)。
    """
    # まずアンマウント
    if is_mounted():
        if not do_umount():
            return False

    if not ensure_local_nhd():
        return False
    # 旧配置の区画表へ v64 以降のカーネルを送らない (Opus M2)。--force でも通さない。
    # push なので NHD として読めない像 (0 バイト・壊れたヘッダ) も送らない (Opus ラリー 3)
    if not legacy_pt_guard(push=True):
        return False

    ok, reason = verify_pull_stamp()
    if not ok:
        if not force:
            print("Error: NHD 全体の上書きを中止: {}".format(reason),
                  file=sys.stderr)
            print("  remote 側 (稼働中のゲストが書いた /etc/settings.db 等) を"
                  "古い local で潰す恐れがある。", file=sys.stderr)
            print("  'python3 tools/nhd_deploy.py pull' で取り直すか、"
                  "承知の上なら deploy --force。", file=sys.stderr)
            return False
        print("Warning: 来歴が崩れているが --force なので続行: {}".format(reason))

    print("NHDイメージをNP21/Wにコピー中...")
    print("  {} -> {}".format(NHD_LOCAL, NHD_REMOTE))

    try:
        shutil.copy2(NHD_LOCAL, NHD_REMOTE)
    except (OSError, PermissionError) as exc:
        print("Error: NHD をコピーできません: {}".format(exc), file=sys.stderr)
        print("  NP21/W がロックしている場合は先に kill してください",
              file=sys.stderr)
        print("  python3 tools/np21w_ctl.py stop   (プロセスが消えるまで待つ)",
              file=sys.stderr)
        return False

    # 書いた直後は remote == local。来歴を今の remote で取り直しておかないと、
    # 続けて deploy するだけで「remote が変わった」と誤検出する。ゲストを走らせ
    # れば remote は変わるので、検出の目的 (ゲストの書き込みを潰さない) は保たれる。
    write_pull_stamp(NHD_LOCAL, NHD_REMOTE)

    size_mb = os.path.getsize(NHD_LOCAL) / (1024 * 1024)
    print("Done! ({:.1f} MB copied)".format(size_mb))
    return True


def do_write_boot(loader_bin):
    """NHDのブート領域にローダーのみを書き込む (新方式)

    カーネル/SQLiteはext2のvmkernel.lz4として配置されるため、
    rawセクタ書き込みはローダー (LBA 2-17) のみ。
    """
    NHD_HEADER = 512
    SECTOR = 512
    LOADER_LBA = 2
    MAX_LOADER_SECTORS = 16  # 8KB

    loader_offset = NHD_HEADER + LOADER_LBA * SECTOR

    with open(loader_bin, 'rb') as f:
        loader_data = f.read()

    if len(loader_data) > MAX_LOADER_SECTORS * SECTOR:
        print("Error: ローダーが{}Bを超過 ({} bytes)".format(
            MAX_LOADER_SECTORS * SECTOR, len(loader_data)))
        return False

    # 8KBにパディング
    loader_data = loader_data.ljust(MAX_LOADER_SECTORS * SECTOR, b'\x00')

    # /tmp は再起動で消える。素の FileNotFoundError を投げると原因が
    # 分からないので、do_mount と同じ案内を出して False を返す。
    if not os.path.isfile(NHD_LOCAL):
        print("Error: {} が見つかりません".format(NHD_LOCAL), file=sys.stderr)
        print("  'python3 tools/nhd_deploy.py pull' で取り込めます",
              file=sys.stderr)
        print("  (make nhd-init はフォーマットを伴うので通常は pull を使う)",
              file=sys.stderr)
        return False

    with open(NHD_LOCAL, 'r+b') as nhd:
        nhd.seek(loader_offset)
        nhd.write(loader_data)

    print("  loader: {} bytes -> LBA {}-{}".format(
        len(loader_data), LOADER_LBA, LOADER_LBA + MAX_LOADER_SECTORS - 1))
    print("Done!")
    return True


def do_format():
    """ext2パーティションを再フォーマット (データ全消去)"""
    if is_mounted():
        print("マウント中のためアンマウントします...")
        if not do_umount():
            return False

    if not os.path.isfile(NHD_LOCAL):
        print("Error: {} が見つかりません".format(NHD_LOCAL), file=sys.stderr)
        return False

    # ループデバイス作成
    result = subprocess.run(
        ['sudo', 'losetup', '-f', '--show',
         '--offset', str(PARTITION_OFFSET), NHD_LOCAL],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("Error: losetup 失敗", file=sys.stderr)
        return False
    loop_dev = result.stdout.strip()

    print("ext2をフォーマット中... ({})".format(loop_dev))
    result = subprocess.run(
        ['sudo', 'mkfs.ext2', '-b', '1024', '-I', '128',
         '-L', 'OS32_HDD', '-F', loop_dev],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("Error: mkfs.ext2 失敗: " + result.stderr.strip(),
              file=sys.stderr)
    else:
        print("フォーマット完了!")
        print(result.stdout)

    # ループデバイス解放
    subprocess.run(['sudo', 'losetup', '-d', loop_dev], capture_output=True)
    return result.returncode == 0


def update_partition_table(nhd_path):
    """区画表 (LBA 1) に OS32 の項目を **PC-98 標準配置**で書く (票 TASK_HDD_INSTALL 段 1-4)。

    開始 = HDD_PARTITION_LBA (1632)、長さ = そこから NHD の終わりまでのシリンダ。
    幾何は NHD ヘッダの値 (NP21/W は IDE の IDENTIFY も BIOS もこの値で見せる)。
    配置と規則の正典は drivers/pc98pt.h、Python 側は tools/pc98pt.py。
    2026-09-23 までの版は開始を +6/+7/+8-9 に書く独自配置だった (F10)。
    """
    with open(nhd_path, 'r+b') as f:
        geom = pc98pt.nhd_geometry(f.read(512))
        hs = geom['header_size']
        cyl = geom['heads'] * geom['spt']
        start = HDD_PARTITION_LBA
        length = ((geom['total'] - start) // cyl) * cyl
        sector = bytearray(pc98pt.SECTOR_SIZE)
        sector[0:pc98pt.ENTRY_SIZE] = pc98pt.make_os32(
            start, length, geom['heads'], geom['spt'])
        f.seek(hs + pc98pt.PT_LBA * pc98pt.SECTOR_SIZE)
        f.write(sector)

    print("パーティションテーブル更新 (標準配置): 開始 LBA {} 長さ {} (H={} S={})".format(
        start, length, geom['heads'], geom['spt']))
    return start, length


# === 区画表の移行 (旧配置 → 標準配置) ===
# 票 TASK_HDD_INSTALL §1-v3 N3。新しいカーネル・ローダは**標準配置しか読まない**
# (旧配置の互換は持たない)。旧配置の NHD を新しいカーネルで起動するには、
# 区画表・第二段ローダ (LBA 2〜17)・/boot/vmkernel.lz4 の 3 つを**同時に**
# 入れ替える必要がある。`make deploy-kernel` はカーネルしか替えないので移行しない。

EXT2_MAGIC = 0xEF53
LOADER_LBA = 2
LOADER_MAX_SECTORS = 16   # 8KB (IPL が LBA 2 から 16 セクタを読む)
# ローダが読める圧縮カーネルの上限 (boot/boot_defs.h の MAX_IMAGE_SIZE = 508KiB)
KERNEL_MAX_BYTES = 508 * 1024
# 移行を必要とするカーネルの KAPI 版 (v64 から標準配置しか読まない)
PT_STANDARD_KAPI = 64


class MigrateError(Exception):
    """移行を断る理由 (NHD には何も書いていない)。"""


def _ext2_blocks_at(img, hs, start):
    """区画の先頭に ext2 のスーパーブロックがあれば 512B セクタ換算の大きさ、無ければ None。

    OS32 の ext2 は 1KiB ブロック専用 (fs/ext2_fmt.c・ローダの ext2_mini)。
    s_log_block_size が 0 でない FS は容量を正しく数えられない上に OS32 が
    読めないので、ここで MigrateError にする (Codex C2)。
    """
    img.seek(hs + start * 512 + 1024)
    sb = img.read(1024)
    if len(sb) < 1024 or struct.unpack_from('<H', sb, 56)[0] != EXT2_MAGIC:
        return None
    log = struct.unpack_from('<I', sb, 24)[0]
    if log != 0:
        raise MigrateError("ext2 のブロック長が {}B (OS32 は 1KiB だけを扱う)"
                           .format(1024 << min(log, 16)))
    return struct.unpack_from('<I', sb, 4)[0] * 2


def plan_migrate_pt(img):
    """開いた NHD を調べて移行の計画を返す (書かない)。

    戻り値 dict: state = 'legacy' (移行する) / 'standard' (もう標準配置)、
    geom、idx、start、length、new_sector (legacy のときだけ)。
    断るときは MigrateError。条件:
      - 空でない項目が**ちょうど 1 つ**で sid = 0xE2 (OS32)
      - 旧配置で読んだ範囲がディスクの中・シリンダ境界
      - その先頭に ext2 があり、ファイルシステムが区画に収まる
    """
    img.seek(0)
    geom = pc98pt.nhd_geometry(img.read(512))
    hs = geom['header_size']
    img.seek(hs + pc98pt.PT_LBA * 512)
    sector = img.read(512)
    if len(sector) != 512:
        raise MigrateError("LBA 1 を読めない")

    used = pc98pt.used_entries(sector)
    if len(used) != 1:
        raise MigrateError("区画項目が {} 個 (ちょうど 1 つの OS32 区画だけを移行する)"
                           .format(len(used)))
    idx = used[0]
    ent = pc98pt.entry_at(sector, idx)
    if ent[pc98pt.OFF_SID] != pc98pt.SID_OS32:
        raise MigrateError("項目 {} の sid が 0x{:02X} (OS32 = 0xE2 ではない)"
                           .format(idx, ent[pc98pt.OFF_SID]))

    heads, spt, total = geom['heads'], geom['spt'], geom['total']

    # もう標準配置か (標準で読めて、その先頭に ext2 がある)
    try:
        s_std, l_std = pc98pt.entry_range(ent, heads, spt, total)
        fs_sect = _ext2_blocks_at(img, hs, s_std)
        if fs_sect is not None and fs_sect <= l_std:
            return {'state': 'standard', 'geom': geom, 'idx': idx,
                    'start': s_std, 'length': l_std}
    except pc98pt.PtError:
        pass

    try:
        start, length = pc98pt.legacy_entry_range(ent, heads, spt, total)
    except pc98pt.PtError as exc:
        raise MigrateError("旧配置としても読めない: {}".format(exc))

    fs_sect = _ext2_blocks_at(img, hs, start)
    if fs_sect is None:
        raise MigrateError("旧配置の開始 LBA {} に ext2 が無い".format(start))
    if fs_sect > length:
        raise MigrateError("ext2 ({} セクタ) が区画 ({} セクタ) より大きい"
                           .format(fs_sect, length))

    try:
        new_ent = pc98pt.make_os32(start, length, heads, spt)
    except pc98pt.PtError as exc:
        raise MigrateError("標準配置で表せない: {}".format(exc))
    new_sector = bytearray(sector)
    new_sector[idx * 32:(idx + 1) * 32] = new_ent
    # 書いたものを標準の読み手で読み戻して同じ範囲になること
    found = pc98pt.find_os32(bytes(new_sector), heads, spt, total)
    if found is None or found[1:] != (start, length):
        raise MigrateError("変換後の読み戻しが一致しない: {}".format(found))
    return {'state': 'legacy', 'geom': geom, 'idx': idx, 'start': start,
            'length': length, 'new_sector': bytes(new_sector)}


def migrate_pt_raw(nhd_path, loader_data):
    """区画表とローダだけを書き換える (カーネルは呼び手)。書く前に全部検査する。

    戻り値 plan。state が 'standard' なら何も書かない。
    """
    if len(loader_data) > LOADER_MAX_SECTORS * 512:
        raise MigrateError("ローダが {}B を超える ({} bytes)".format(
            LOADER_MAX_SECTORS * 512, len(loader_data)))
    with open(nhd_path, 'r+b') as img:
        plan = plan_migrate_pt(img)
        if plan['state'] != 'legacy':
            return plan
        hs = plan['geom']['header_size']
        # ローダ → 区画表の順 (区画表が最後。途中で落ちたら旧配置のまま残る)
        img.seek(hs + LOADER_LBA * 512)
        img.write(loader_data.ljust(LOADER_MAX_SECTORS * 512, b'\x00'))
        img.seek(hs + pc98pt.PT_LBA * 512)
        img.write(plan['new_sector'])
        img.flush()
        os.fsync(img.fileno())
    # 読み戻し
    with open(nhd_path, 'rb') as img:
        img.seek(hs + pc98pt.PT_LBA * 512)
        if img.read(512) != plan['new_sector']:
            raise MigrateError("区画表の読み戻しが一致しない")
        img.seek(hs + LOADER_LBA * 512)
        if img.read(len(loader_data)) != loader_data:
            raise MigrateError("ローダの読み戻しが一致しない")
    return plan


def migrate_preflight(nhd_path, loader_bin, kernel_file, push, stamp_check=None):
    """migrate-pt の**全部の検査**。どの書き込みよりも前に呼ぶ (Codex C1 / Opus m1)。

    戻り値 (plan, loader_data)。断るときは MigrateError (NHD には何も書いていない)。
    stamp_check は push するときの来歴の検査 (既定 verify_pull_stamp)。
    """
    for path, what in ((loader_bin, 'ローダ'), (kernel_file, 'カーネル')):
        if not os.path.isfile(path):
            raise MigrateError("{} {} が無い (make all を先に)".format(what, path))
    try:
        with open(loader_bin, 'rb') as f:
            loader_data = f.read()
        ksize = os.path.getsize(kernel_file)
    except OSError as exc:
        raise MigrateError("ローダ / カーネルを読めない ({}: {})".format(type(exc).__name__, exc))
    if not loader_data or len(loader_data) > LOADER_MAX_SECTORS * 512:
        raise MigrateError("ローダが空か {}B を超える ({} bytes)".format(
            LOADER_MAX_SECTORS * 512, len(loader_data)))
    if ksize == 0 or ksize > KERNEL_MAX_BYTES:
        raise MigrateError("カーネルが空か {}B を超える ({} bytes)".format(
            KERNEL_MAX_BYTES, ksize))
    try:
        with open(nhd_path, 'rb') as img:
            plan = plan_migrate_pt(img)
    except (pc98pt.PtError, OSError) as exc:
        # 読めない NHD も「断る」側へ (例外で落ちずに、何も書かずに止まる)
        raise MigrateError("{}: {}".format(type(exc).__name__, exc))
    if plan['state'] == 'legacy' and plan['start'] != HDD_PARTITION_LBA:
        # ホスト側のマウントは PARTITION_OFFSET (LBA 1632) 固定。違う位置の
        # ext2 にカーネルを置く手段が無いので断る。
        raise MigrateError("区画の開始 LBA {} が {} でない (このツールはマウントできない)"
                           .format(plan['start'], HDD_PARTITION_LBA))
    if plan['state'] == 'legacy' and push:
        ok, reason = (stamp_check or verify_pull_stamp)()
        if not ok:
            raise MigrateError("push できない (NHD 全体の上書きを断る来歴): {}".format(reason))
    return plan, loader_data


def do_migrate_pt(loader_bin, kernel_file, push=True):
    """旧配置の NHD を標準配置へ移し、ローダとカーネルを同時に入れ替える (N3)。

    [D1] NP21/W を**止めてから**実行する (push は NP21/W の NHD を上書きする)。
    順序: **全部の検査** (migrate_preflight) → カーネルを ext2 の /boot へ →
    ローダ → 区画表 (最後) → push。検査で断ったら NHD は 1 バイトも変わらない。
    書き始めた後に落ちたら push しない (ローカルの作業用 NHD だけが途中の状態)。
    """
    if is_mounted() and not do_umount():
        return False
    if not ensure_local_nhd():
        return False
    try:
        plan, loader_data = migrate_preflight(NHD_LOCAL, loader_bin, kernel_file, push)
    except MigrateError as exc:
        print("Error: 移行を断る: {} (何も書いていない)".format(exc), file=sys.stderr)
        return False
    if plan['state'] == 'standard':
        print("区画表は既に標準配置 (開始 LBA {} 長さ {})。移行は不要 — "
              "カーネルは deploy-kernel、ローダは deploy-boot で".format(
                  plan['start'], plan['length']))
        return True
    print("移行: 旧配置 → 標準配置 (開始 LBA {} 長さ {}、H={} S={})".format(
        plan['start'], plan['length'], plan['geom']['heads'], plan['geom']['spt']))

    # 1. カーネル (ext2 の /boot/vmkernel.lz4)
    if not do_copy([kernel_file], dest_dir='/boot', rename='vmkernel.lz4'):
        return False
    if not do_umount():
        return False
    # 2-3. ローダ → 区画表
    try:
        migrate_pt_raw(NHD_LOCAL, loader_data)
    except (MigrateError, pc98pt.PtError, OSError) as exc:
        print("Error: 区画表 / ローダの書き換えに失敗: {} (push しない)".format(exc),
              file=sys.stderr)
        return False
    print("  loader: {} bytes -> LBA {}-{}".format(
        len(loader_data), LOADER_LBA, LOADER_LBA + LOADER_MAX_SECTORS - 1))
    print("  partition table: 標準配置で書いて読み戻し一致")
    if not push:
        print("完了 (--no-push: NP21/W 側へはまだ送っていない)")
        return True
    return do_deploy()


def tree_kapi_version():
    """このツリーのカーネルの KAPI 版 (sdk/include/os32/os32_kapi_shared.h)。読めなければ None。"""
    path = os.path.join(PROJ_DIR, 'sdk', 'include', 'os32', 'os32_kapi_shared.h')
    try:
        with open(path, encoding='utf-8') as f:
            for line in f:
                m = re.match(r'#define\s+KAPI_VERSION\s+(\d+)', line)
                if m:
                    return int(m.group(1))
    except OSError:
        return None
    return None


def classify_pt_layout(img):
    """NHD の区画表が**カーネルから見て**どう読めるか (書かない)。

    「旧配置があるか」の判定。「自動で移行できるか」(plan_migrate_pt) とは分ける
    (Codex ラリー 2 の 1)。カーネル (ext2_find_partition) と同じく、sid 0xE2 の
    **最初の**項目だけを見る。戻り値 (状態, 理由):
      'standard' … 標準配置で読める (v64 のカーネルが区画を見つける)
      'legacy'   … 標準配置では読めず、旧配置 (v63 まで) なら読める
      'broken'   … OS32 の項目はあるが、どちらでも範囲にならない
      'none'     … OS32 の項目が無い (v64 のカーネルは hd0 を ext2 としてマウントしない)
      'not_nhd'  … NHD のヘッダが無い・読めない (区画の位置を決められない)
    """
    # 'not_nhd' は**ヘッダが NHD でない**ときだけ。読み取りの失敗 (OSError) は
    # ここで握らず呼び手へ投げる — 門は「読めない」を通さない (Codex ラリー 3)
    img.seek(0)
    try:
        geom = pc98pt.nhd_geometry(img.read(512))
    except (pc98pt.PtError, struct.error) as exc:
        return 'not_nhd', str(exc)
    img.seek(geom['header_size'] + pc98pt.PT_LBA * 512)
    sector = img.read(512)
    if len(sector) != 512:
        # ヘッダは NHD なのに LBA 1 が無い (切り詰められた像) — 「NHD でない」ではない
        return 'broken', "LBA 1 を読めない (像が短い)"
    heads, spt, total = geom['heads'], geom['spt'], geom['total']
    for i in range(pc98pt.MAX_ENTRIES):
        ent = pc98pt.entry_at(sector, i)
        if ent[pc98pt.OFF_SID] != pc98pt.SID_OS32:
            continue
        try:
            pc98pt.entry_range(ent, heads, spt, total)
            return 'standard', "項目 {}".format(i)
        except pc98pt.PtError:
            pass
        try:
            pc98pt.legacy_entry_range(ent, heads, spt, total)
            return 'legacy', "項目 {} は旧配置".format(i)
        except pc98pt.PtError as exc:
            return 'broken', "項目 {} は標準でも旧配置でも読めない: {}".format(i, exc)
    return 'none', "sid 0xE2 (OS32) の項目が無い"


def legacy_pt_guard(nhd_path=None, kapi=None, push=False):
    """v64 以降のカーネルを、そのカーネルが区画を見つけられない NHD へ配らない (Opus M2)。

    戻り値 True = 配ってよい。v64 のカーネルとローダは標準配置しか読まないので、
    標準配置で読める ('standard') ときだけ通す。
      'legacy'  … 断る。自動で移行できるか (plan_migrate_pt) も調べて、できなければ
                  その理由を出す (移行できないからといって通さない — Codex ラリー 2 の 1)
      'broken' / 'none' … 断る。v64 のカーネルは / も /hd0 もマウントできない
      'not_nhd' / ファイルが無い … 通す (警告を出す)。区画の位置を決められない
                  = ホストの NHD として扱えない物で、呼び手は NHD が要る処理
                  (マウント・写し) の手前でもう一度この門を通る (取り込みの後)
    push=True (do_deploy: NP21/W の NHD を丸ごと上書きする経路) は 'not_nhd' も断る
    — 0 バイトやヘッダの壊れた NHD_LOCAL で NP21/W の NHD を潰さない (Opus ラリー 3 の 1)。
    **通すのはこの 2 つだけ**。開けない・読めない (OSError ほか) は断る。分類が
    legacy と決まった後の移行の可否の調べで出た例外は、種類を問わず「移行できない
    理由」として出して断る (Codex ラリー 3)。
    """
    path = nhd_path or NHD_LOCAL
    kapi = tree_kapi_version() if kapi is None else kapi
    if not os.path.isfile(path):
        return True
    auto = None
    read_error = None
    try:
        with open(path, 'rb') as img:
            state, why = classify_pt_layout(img)
            if state == 'legacy':
                try:
                    plan_migrate_pt(img)
                except Exception as exc:  # noqa: BLE001 — 移行できない理由として出す
                    auto = "{}: {}".format(type(exc).__name__, exc)
                    if isinstance(exc, OSError):
                        read_error = auto
    except Exception as exc:  # noqa: BLE001 — 開けない・読めないは通さない (版を問わず)
        print("Error: {} の区画表を読めない ({}: {})。配置を確かめられないので配らない"
              .format(path, type(exc).__name__, exc), file=sys.stderr)
        return False
    # 移行の可否を調べる途中の**読み取りの失敗**は、下の版の分岐 (v63 以下は旧配置を
    # 通す) より前に断る。旧配置を通すのは「読めた」ときだけ (Codex 確認の minor)
    if read_error is not None:
        print("Error: {} を読む途中で失敗した ({})。配置を確かめ切れないので配らない"
              .format(path, read_error), file=sys.stderr)
        return False
    if state == 'not_nhd':
        if push:
            print("Error: {} は NHD として読めない ({})。NP21/W の NHD を上書きしない"
                  .format(path, why), file=sys.stderr)
            return False
        print("Warning: {} は NHD として読めない ({})。区画表の配置は検査していない"
              .format(path, why), file=sys.stderr)
        return True
    if kapi is None or kapi < PT_STANDARD_KAPI:
        return True
    if state == 'standard':
        return True
    if state == 'legacy':
        print("Error: {} の区画表は旧配置 (v63 まで、{})。KAPI v{} のカーネルは標準配置しか"
              "読まないので、このまま配ると / がマウントできない。".format(path, why, kapi),
              file=sys.stderr)
        if auto is None:
            print("  先に 'make nhd-migrate-pt' (区画表・ローダ・カーネルを同時に移す) を。"
                  "docs/08_build.md §8-4", file=sys.stderr)
        else:
            print("  自動の移行 (migrate-pt) もできない: {}。区画表を手で直すか "
                  "'make nhd-init' で作り直す ([D2])".format(auto), file=sys.stderr)
        return False
    print("Error: {} の区画表を KAPI v{} のカーネルは読めない ({})。配らない"
          .format(path, kapi, why), file=sys.stderr)
    return False


def do_pull():
    """Windows側NHDを/tmpに取り込む (フォーマットしない)

    /tmp は再起動で消えるため NHD_LOCAL は頻繁に失われる。init は
    フォーマットを伴いゲスト側で作られたデータ (home/db/save) を消すので、
    作業を再開したいだけのときはこちらを使う。
    """
    # 来歴は**入口で**消す。早期 return (マウント中 / remote 欠損) や
    # コピー後の do_mount() 失敗で古い stamp が残ると、中身の違う local で
    # 全体上書きが通ってしまう (往復 1 の B8)。
    remove_pull_stamp()

    if is_mounted():
        print("マウント中です。先に umount してください。", file=sys.stderr)
        return False

    if not os.path.isfile(NHD_REMOTE):
        print("Error: {} が見つかりません".format(NHD_REMOTE), file=sys.stderr)
        return False

    print("NHDイメージを取り込み中 (フォーマットなし)...")
    print("  {} -> {}".format(NHD_REMOTE, NHD_LOCAL))
    os.makedirs(os.path.dirname(NHD_LOCAL), exist_ok=True)
    try:
        shutil.copy2(NHD_REMOTE, NHD_LOCAL)
    except (OSError, PermissionError) as exc:
        print("Error: NHD を取り込めません: {}".format(exc), file=sys.stderr)
        print("  NP21/W がロックしている場合は先に kill してください",
              file=sys.stderr)
        print("  python3 tools/np21w_ctl.py stop   (プロセスが消えるまで待つ)",
              file=sys.stderr)
        return False

    size_mb = os.path.getsize(NHD_LOCAL) / (1024 * 1024)
    print("  コピー済み ({:.1f} MB)".format(size_mb))
    if not do_mount():
        return False
    # 全部成功した**最後**にだけ来歴を書き、そこで初めて「完了」と言う
    # (mount / stamp で落ちた後に「完了!」が残っていた、追加往復 3)。
    try:
        write_pull_stamp(NHD_LOCAL, NHD_REMOTE)
    except OSError as exc:
        print("Error: 来歴を書けません: {}".format(exc), file=sys.stderr)
        return False
    print("完了! ({:.1f} MB)".format(size_mb))
    return True


def do_init():
    """Windows側NHDを/tmpにコピー + パーティション更新 + フォーマット + マウント"""
    if is_mounted():
        print("既にマウント済みです。先にumountしてください。")
        return False

    if os.path.isfile(NHD_LOCAL):
        print("{} は既に存在します。上書きします...".format(NHD_LOCAL))

    if not os.path.isfile(NHD_REMOTE):
        print("Error: {} が見つかりません".format(NHD_REMOTE),
              file=sys.stderr)
        return False

    print("NHDイメージをコピー中...")
    print("  {} -> {}".format(NHD_REMOTE, NHD_LOCAL))

    os.makedirs(os.path.dirname(NHD_LOCAL), exist_ok=True)
    try:
        shutil.copy2(NHD_REMOTE, NHD_LOCAL)
    except PermissionError:
        print("Error: NP21/Wがファイルをロックしています",
              file=sys.stderr)
        return False

    size_mb = os.path.getsize(NHD_LOCAL) / (1024 * 1024)
    print("コピー完了! ({:.1f} MB)".format(size_mb))

    # パーティションテーブル更新 (CHS開始位置をシリンダ8に)
    print("")
    update_partition_table(NHD_LOCAL)

    # フォーマット
    print("")
    if not do_format():
        return False

    # マウント
    print("")
    return do_mount()


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
            # ゲストパスがディレクトリ ('/bin/' 等) ならファイル名を追加
            if guest.endswith('/'):
                g = guest + basename
            else:
                g = guest
            results.append((fpath, g))
    else:
        fpath = os.path.join(PROJ_DIR, host_pattern)
        if os.path.isfile(fpath):
            results.append((fpath, guest))
        else:
            print("  Warning: {} not found".format(host_pattern))

    return results


def remove_partial(dest_file):
    """コピーに失敗した宛先を消す。

    `cp` は書き込み前に宛先を切り詰めるので、失敗すると**壊れた中身の
    ファイルが残る**。残すとゲストは「存在するが壊れた成果物」を掴み、
    起動しない理由が見えなくなる (2026-09-10 の /boot/vmkernel.lz4 が
    446,464 B に切り詰められた件、POLICY_DEBUG §4-29)。
    消しておけば NOT FOUND で失敗が見える。消せなくても報告だけして進む
    (失敗は呼び出し側が total_failed で拾う)。

    保護対象 (/etc/settings.db*) には触れない。そもそも書いていないので
    「壊れた宛先」も無い。"""
    try:
        if protect.is_protected(MOUNT_POINT, dest_file):
            protect.protect_log(dest_file)
            return
    except protect.ProtectError as exc:
        print("  Warning: {} の保護判定に失敗したので消さない: {}".format(
            dest_file, exc))
        return
    result = subprocess.run(['sudo', 'rm', '-f', '--', dest_file],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print("  Warning: 壊れた {} を消せなかった: {}".format(
            dest_file, result.stderr.strip()))


def do_sync(tag_filter=None):
    """deploy.yaml に基づくフルデプロイ

    1. ブートローダー書き込み (write-boot)
    2. ディレクトリ構造作成
    3. 全ファイルコピー
    """
    if not ensure_local_nhd():
        return False
    if not legacy_pt_guard():
        return False
    cfg = load_deploy_yaml()
    if cfg is None:
        return False

    print("=" * 55)
    print("  OS32 フルデプロイ (deploy.yaml)")  # guard_root は mount 後 (Phase 2)
    if tag_filter:
        print("  タグフィルタ: {}".format(tag_filter))
    print("=" * 55)

    # === Phase 1: ブート領域 (ローダーのみrawセクタ書き込み) ===
    if not tag_filter:
        boot = cfg.get('boot', {})
        loader_path = os.path.join(PROJ_DIR, boot.get('loader', ''))

        if loader_path and os.path.isfile(loader_path):
            print("\n[boot] ローダー書き込み")
            if not do_write_boot(loader_path):
                return False
        else:
            print("Warning: ローダー {} が見つかりません".format(loader_path))

    # === Phase 2: ext2 マウント + ディレクトリ作成 ===
    fs = cfg.get('filesystem', {})

    if not ensure_mounted_for_kernel():
        return False
    # 対象 0 件の `sync --tag` でも <root>/etc の異常で止める (往復 2 の 8)
    if not guard_root():
        return False

    if not tag_filter:
        dirs = fs.get('directories', [])
        print("\n[dirs] ディレクトリ構造作成")
        for d in dirs:
            target, state = ensure_dir(d)
            if state == 'error':
                return False

    # === Phase 3: ファイルコピー ===
    files = fs.get('files', [])
    total_copied = 0
    total_size = 0
    total_failed = 0
    total_protected = 0

    for entry in files:
        entry_tags = entry.get('tags', [])

        # タグフィルタ
        if tag_filter and tag_filter not in entry_tags:
            continue

        pairs = resolve_files_from_entry(entry)
        if not pairs:
            continue

        tag_label = entry_tags[0] if entry_tags else 'other'
        print("\n[{}] {} -> {}".format(tag_label, entry['host'], entry['guest']))

        for host_abs, guest_path in pairs:
            # 実コピー直前に最終パスで保護判定する (manifest 直指定 / glob / tag
            # のどれで来ても同じ 1 か所を通る)
            dest_file, state = guard_dest(guest_path, host_src=host_abs)
            if state == 'error':
                return False
            if state == 'protected':
                total_protected += 1
                continue

            # ゲスト側のディレクトリを確保 (各祖先まで判定してから作る)。
            # 親は**確定した最終パス**から取る (guest_path の字句ではない)。
            try:
                parent = protect.guest_path_of(
                    MOUNT_POINT, os.path.dirname(dest_file))
            except protect.ProtectError as exc:
                print("Error: 配備の保護判定に失敗: {}".format(exc),
                      file=sys.stderr)
                return False
            dest_dir_abs, dstate = ensure_dir(parent)
            if dstate == 'error':
                return False
            if dstate == 'protected':
                total_protected += 1
                continue

            # ファイルコピー (確定した**ファイルパス**だけを cp に渡す)
            result = subprocess.run(
                ['sudo', 'cp', '--', os.path.abspath(host_abs), dest_file],
                capture_output=True, text=True
            )
            if result.returncode != 0:
                print("  Error: {} -> {}: {}".format(
                    os.path.basename(host_abs), guest_path,
                    result.stderr.strip()))
                total_failed += 1
                remove_partial(dest_file)
                continue

            size = os.path.getsize(host_abs)
            total_size += size
            total_copied += 1
            print("  {} ({} bytes)".format(guest_path, size))

    # sync — 失敗を握り潰すと「書いたつもり」で deploy へ進む
    if not run_sync():
        return False

    print("\n" + "=" * 55)
    if total_failed:
        print("  失敗! {} ファイルをコピーできなかった "
              "({} ファイル {:,} bytes は成功)".format(
                  total_failed, total_copied, total_size))
        print("  配備は完了していない。ゲストの成果物は古いままか消えている。")
        print("=" * 55)
        return False
    print("  完了! {} ファイル ({:,} bytes){}".format(
        total_copied, total_size,
        "、{} 件は保護対象として除外".format(total_protected)
        if total_protected else ""))
    print("=" * 55)
    return True


def do_sync_from_hostdrv():
    """HostDrvディレクトリ (C:\\os32) の内容をNHDのext2パーティションに同期

    deploy.yaml を参照せず、HostDrvディレクトリの全ファイルを再帰的にコピーする。
    これにより HostDrv が唯一のソースとなり、管理漏れを防止する。
    """
    hostdrv_dir = os.environ.get('HOSTDRV_DIR', '/mnt/c/os32')

    if not os.path.isdir(hostdrv_dir):
        print("Error: HostDrvディレクトリが見つかりません: {}".format(hostdrv_dir),
              file=sys.stderr)
        return False

    # 旧配置の NHD へ v64 以降のカーネルを配らない (Opus M2)。NHD が無ければここは
    # 通り、ensure_mounted_for_kernel が**取り込んだ後・マウントの前**にもう一度見る
    if not legacy_pt_guard():
        return False
    if not ensure_mounted_for_kernel():
        return False
    # 宛先 (NHD) だけでなく **source の HostDrv ツリー**も検査する。
    # symlink を辿った先から写せば宛先の判定を素通りできる。
    if not guard_root() or not guard_root(hostdrv_dir):
        return False

    print("\n" + "=" * 55)
    print("  HostDrv -> NHD ext2 同期")
    print("  {} -> {}".format(hostdrv_dir, MOUNT_POINT))
    print("=" * 55)

    total_copied = 0
    total_size = 0
    total_failed = 0
    total_protected = 0

    # os.walk は既定で読めないディレクトリを黙って飛ばす。HostDrv の一部が
    # 読めないまま「完了」と言わせない (往復 1 の B6)。
    walk_errors = []

    for dirpath, dirnames, filenames in protect.walk_root(
            hostdrv_dir, onerror=walk_errors.append):
        # HostDrvルートからの相対パス
        rel_dir = os.path.relpath(dirpath, hostdrv_dir)
        if rel_dir == '.':
            rel_dir = ''

        # 子ディレクトリのうち保護対象は **降りる前に** dirnames から外す。
        # 到着してから判定していると、HostDrv の etc/settings.db/ が読めない
        # ときに os.walk の scandir が先に失敗して CLI ごと落ちていた
        # (往復 2 の 9)。除外は失敗ではないのでログだけ出す。
        keep_dirs = []
        for child in dirnames:
            child_guest = ('/' + rel_dir.replace(os.sep, '/') + '/' + child
                           if rel_dir else '/' + child)
            _unused, cstate = guard_dest(child_guest)
            if cstate == 'error':
                return False
            if cstate == 'protected':
                total_protected += 1
                continue
            keep_dirs.append(child)
        dirnames[:] = keep_dirs

        # NHD側のディレクトリを確保。HostDrv に etc/settings.db/ のような
        # 残骸ディレクトリがあっても作らない (往復 3 の 4、各祖先は B2)。
        dest_dir, dstate = ensure_dir(
            '/' + rel_dir.replace(os.sep, '/') if rel_dir else '/')
        if dstate == 'error':
            return False
        if dstate == 'protected':
            total_protected += 1
            dirnames[:] = []
            continue

        for fname in sorted(filenames):
            src_path = os.path.join(dirpath, fname)
            if rel_dir:
                guest_path = '/' + rel_dir.replace(os.sep, '/') + '/' + fname
            else:
                guest_path = '/' + fname
            # HostDrv に古い etc/settings.db が残っていても NHD の本体を触らない
            dest_path, state = guard_dest(guest_path, host_src=src_path)
            if state == 'error':
                return False
            if state == 'protected':
                total_protected += 1
                continue

            result = subprocess.run(
                ['sudo', 'cp', '--', os.path.abspath(src_path), dest_path],
                capture_output=True, text=True
            )
            if result.returncode != 0:
                print("  Error: {} -> {}: {}".format(
                    fname, guest_path, result.stderr.strip()))
                total_failed += 1
                remove_partial(dest_path)
                continue

            size = os.path.getsize(src_path)
            total_size += size
            total_copied += 1
            print("  {} ({} bytes)".format(guest_path, size))

    if not run_sync():
        return False

    print("\n" + "=" * 55)
    if walk_errors:
        for exc in walk_errors:
            print("  Error: HostDrv を辿れなかった: {}".format(exc),
                  file=sys.stderr)
        print("  配備は完了していない (HostDrv の一部を読めていない)。")
        print("=" * 55)
        return False
    if total_failed:
        print("  失敗! {} ファイルをコピーできなかった "
              "({} ファイル {:,} bytes は成功)".format(
                  total_failed, total_copied, total_size))
        print("=" * 55)
        return False
    print("  完了! {} ファイル ({:,} bytes){}".format(
        total_copied, total_size,
        "、{} 件は保護対象として除外".format(total_protected)
        if total_protected else ""))
    print("=" * 55)
    return True


def resolve_guest_path(host_file):
    """マニフェストからホストファイルに対応するゲストパスを解決する

    照合はプロジェクト相対のフルパスで行う。以前は glob パターンの
    ベース名 (``*.bin``) だけを見ていたため、どの .bin も最初に現れた
    ``*.bin`` エントリに吸い込まれ、apps/ のアプリが /bin/ に解決されていた。

    優先順:
      1. type: file の host 完全一致
      2. type: glob のパターン一致 (フルパス)
      3. 互換のためのベース名一致

    Returns: ゲストパス文字列 (見つからなければ None)
    """
    import fnmatch

    cfg = load_deploy_yaml()
    if cfg is None:
        return None

    relpath = os.path.relpath(os.path.abspath(host_file), PROJ_DIR)
    relpath = relpath.replace(os.sep, '/')
    basename = os.path.basename(host_file)
    files = (cfg.get('filesystem') or {}).get('files') or []

    def _guest_for(entry):
        guest = entry['guest']
        if basename in (entry.get('exclude') or []):
            return None
        return guest + basename if guest.endswith('/') else guest

    # 1. 完全一致
    for entry in files:
        if entry.get('type', 'file') != 'glob' and entry['host'] == relpath:
            g = _guest_for(entry)
            if g:
                return g

    # 2. glob パターン (フルパスで照合)
    for entry in files:
        if entry.get('type', 'file') == 'glob' and \
                fnmatch.fnmatch(relpath, entry['host']):
            g = _guest_for(entry)
            if g:
                return g

    # 3. ベース名一致 (旧挙動の互換)
    for entry in files:
        if os.path.basename(entry['host']) == basename:
            g = _guest_for(entry)
            if g:
                return g

    return None


def do_push(local_path, remote_name=None, resolve=False):
    """廃止 (2026-09-09)。ホットデプロイの窓を撤去した。

    物理末尾の 256KB 予約は CPL=3 スタック帯と同じ範囲で、8MB 構成では
    アプリと必ず衝突していた。配送は HostDrv に一本化:
      make deploy       (ホスト -> C:/os32)
      ゲストで hsync    (/host → / 、既定で sys は除く)
    経緯: docs/tasks/hotdeploy/DESIGN.md
    """
    del local_path, remote_name, resolve
    print("push は廃止されました (2026-09-09)。", file=sys.stderr)
    print("  make deploy → ゲストで hsync を使ってください。", file=sys.stderr)
    return False



def main():
    """各サブコマンドの戻り値をそのまま終了コードにする (票 S0-D、往復 2 の 9)。

    False を返した操作は失敗。以前は copy / rm / sync の失敗が exit 0 のまま
    後続 (NHD deploy) へ進み、古い成果物を配っていた。
    """
    if len(sys.argv) < 2:
        print("NHD ext2 Deploy Tool (mount版)")
        print("")
        print("使い方: {} <command>".format(sys.argv[0]))
        print("")
        print("  sync [--tag TAG]       — deploy.yaml に基づくフルデプロイ")
        print("  push [--resolve] <file> [guest] — シリアル経由ホットデプロイ (再起動不要)")
        print("  mount                  — ext2パーティションをマウント")
        print("  umount                 — アンマウント")
        print("  copy [--dest DIR] [--rename NAME] <src> [...] — ファイルをext2にコピー")
        print("  copy-all [--dest DIR] <dir> [ext]   — dirの全ファイルを一括コピー")
        print("  setup-dirs             — システムディレクトリを作成")
        print("  ls [path]              — ファイル一覧")
        print("  rm <file>              — ファイル削除")
        print("  deploy [--force]       — umount + NHDをNP21/Wにコピー")
        print("  pull                   — NP21/W側NHDを取り込む (来歴を記録)")
        print("  format                 — ext2を再フォーマット (全消去)")
        print("  init                   — Windows側NHDをコピー+フォーマット+マウント")
        print("  migrate-pt [--no-push] [--loader BIN] [--kernel LZ4]")
        print("                         — 旧配置の区画表を標準配置へ + ローダ + カーネルを同時に (NP21/W 停止中)")
        print("")
        print("パス:")
        print("  NHDローカル:  {}".format(NHD_LOCAL))
        print("  NHD NP21/W:   {}".format(NHD_REMOTE))
        print("  マウント:     {}".format(MOUNT_POINT))
        print("  来歴:         {}".format(stamp_path()))
        print("  deploy defs:  {}".format(", ".join(DEPLOY_MANIFESTS)))
        return True

    cmd = sys.argv[1]

    if cmd == 'mount':
        return do_mount()

    elif cmd == 'umount':
        return do_umount()

    elif cmd == 'setup-dirs':
        return do_mkdirs()

    elif cmd == 'copy':
        # --dest DIR と --rename NAME オプションをパース
        dest_dir = '/'
        rename = None
        src_files = []
        i = 2
        while i < len(sys.argv):
            if sys.argv[i] == '--dest' and i + 1 < len(sys.argv):
                dest_dir = sys.argv[i + 1]
                i += 2
            elif sys.argv[i] == '--rename' and i + 1 < len(sys.argv):
                rename = sys.argv[i + 1]
                i += 2
            else:
                src_files.append(sys.argv[i])
                i += 1
        if not src_files:
            print("Usage: copy [--dest DIR] [--rename NAME] <src_file> [...]")
            return False
        return do_copy(src_files, dest_dir=dest_dir, rename=rename)

    elif cmd == 'copy-all':
        # --dest DIR オプションをパース
        dest_dir = '/'
        args = []
        i = 2
        while i < len(sys.argv):
            if sys.argv[i] == '--dest' and i + 1 < len(sys.argv):
                dest_dir = sys.argv[i + 1]
                i += 2
            else:
                args.append(sys.argv[i])
                i += 1
        if not args:
            print("Usage: copy-all [--dest DIR] <dir> [extension]")
            return False
        src_dir = args[0]
        ext = args[1] if len(args) > 1 else '.bin'
        return do_copy_all(src_dir, ext, dest_dir=dest_dir)

    elif cmd == 'ls':
        path = sys.argv[2] if len(sys.argv) > 2 else '/'
        return do_ls(path)

    elif cmd == 'rm':
        if len(sys.argv) < 3:
            print("Usage: rm <file>")
            return False
        return do_rm(sys.argv[2])

    elif cmd == 'deploy':
        return do_deploy(force='--force' in sys.argv[2:])

    elif cmd == 'write-boot':
        if len(sys.argv) < 3:
            print("Usage: write-boot <loader.bin>")
            return False
        loader = sys.argv[2]
        if not os.path.isfile(loader):
            print("Error: {} not found".format(loader))
            return False
        return do_write_boot(loader)

    elif cmd == 'format':
        return do_format()

    elif cmd == 'pull':
        return do_pull()

    elif cmd == 'init':
        return do_init()

    elif cmd == 'migrate-pt':
        loader = os.path.join(PROJ_DIR, 'boot', 'loader_hdd.bin')
        kernel = os.path.join(PROJ_DIR, 'build', 'out', 'vmkernel.lz4')
        push = True
        i = 2
        while i < len(sys.argv):
            if sys.argv[i] == '--no-push':
                push = False
                i += 1
            elif sys.argv[i] == '--loader' and i + 1 < len(sys.argv):
                loader = sys.argv[i + 1]
                i += 2
            elif sys.argv[i] == '--kernel' and i + 1 < len(sys.argv):
                kernel = sys.argv[i + 1]
                i += 2
            else:
                print("Usage: migrate-pt [--no-push] [--loader BIN] [--kernel LZ4]")
                return False
        return do_migrate_pt(loader, kernel, push=push)

    elif cmd == 'sync':
        # --tag TAG オプションをパース
        tag_filter = None
        i = 2
        while i < len(sys.argv):
            if sys.argv[i] == '--tag' and i + 1 < len(sys.argv):
                tag_filter = sys.argv[i + 1]
                i += 2
            else:
                i += 1
        return do_sync(tag_filter=tag_filter)

    elif cmd == 'sync-from-hostdrv':
        return do_sync_from_hostdrv()

    elif cmd == 'push':
        # --resolve オプションをパース
        resolve = False
        args = []
        i = 2
        while i < len(sys.argv):
            if sys.argv[i] == '--resolve':
                resolve = True
                i += 1
            else:
                args.append(sys.argv[i])
                i += 1
        if not args:
            print("Usage: push [--resolve] <local_file> [guest_name]")
            return False
        local_file = args[0]
        guest_name = args[1] if len(args) > 1 else None
        return do_push(local_file, remote_name=guest_name, resolve=resolve)

    print("Unknown command: {}".format(cmd), file=sys.stderr)
    return False


if __name__ == '__main__':
    sys.exit(0 if main() is not False else 1)
