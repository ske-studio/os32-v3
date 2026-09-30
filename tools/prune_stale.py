#!/usr/bin/env python3
"""配備先 (HostDrv / NHD) に残った、マニフェストに無い古いバイナリを一覧・削除する。

配備 (hostdrv_deploy.py / nhd_deploy.py sync) はマニフェストの内容を書くだけで、
マニフェストから外れたファイルを消さない。KAPI のレイアウトが変わると古い
バイナリは別関数へ飛んで rshell ごと沈黙する (CLAUDE.md の Known Gotchas) ので、
配備のたびに掃除する。

対象は **システム側ディレクトリ (/, /bin, /sbin, /usr/bin, /sys, /debug) 直下の
*.bin** だけ。/home /data /etc /tmp などのユーザデータには触れない。

使い方:
    python3 tools/prune_stale.py [hostdrv|nhd|both] [--delete]
      既定は both の一覧表示 (dry-run)。--delete で実際に消す。
      nhd は build/nhd/os32.nhd をマウントして操作する (sudo)。Windows 側へ
      反映するのは deploy-nhd の deploy 段 (NP21/W 停止中のみ)。
終了コード: 0 = 掃除済み or 何も無い、1 = 引数/環境エラー。
一覧の最後に必ず 1 行 `RESULT: ...` を出す (os32-cycle / emu_agent が拾う)。
"""

import errno
import os
import stat
import sys
import glob
import time
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ_DIR = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import deploy_manifests  # noqa: E402
# 通常配備の settings 保護 (票 S0-D / D0)。掃除も「消す直前」に同じ判定を通す。
import deploy_protect as protect  # noqa: E402

# 掃除してよいゲスト側ディレクトリ (末尾スラッシュ無し、'' = ルート直下)
PRUNE_DIRS = ('', 'bin', 'sbin', 'usr/bin', 'sys', 'debug')
EXT = '.bin'


def wanted_guest_paths():
    """マニフェスト (core + userland + apps + game) が置くゲストパスの集合"""
    merged = deploy_manifests.load_merged()
    if merged is None:
        return None
    want = set()
    for entry in merged['filesystem']['files']:
        host = entry['host']
        guest = entry['guest']
        pat = os.path.join(PROJ_DIR, host)
        hosts = glob.glob(pat) if any(c in host for c in '*?[') else [pat]
        for hp in hosts:
            base = os.path.basename(hp)
            want.add(guest + base if guest.endswith('/') else guest)
    return want


def find_stale(root, want):
    """root 配下の PRUNE_DIRS 直下にある *.bin でマニフェストに無いもの

    候補の判定は `os.lstat` で行う。`os.path.isdir` / `isfile` は EACCES / EIO を
    **False に丸める**ので、読めないディレクトリの中身が「候補 0 件」になって
    「掃除済み」と報告されていた (往復 3 の D5)。ENOENT (競合で消えた) だけ
    読み飛ばし、それ以外の OSError は上へ投げて非ゼロにする。
    symlink はツリー全体の前提検査 (check_tree) が既に拒否している。
    """
    stale = []
    for d in PRUNE_DIRS:
        dp = os.path.join(root, d) if d else root
        try:
            st = os.lstat(dp)
        except OSError as exc:
            if exc.errno == errno.ENOENT:
                continue
            raise
        if not stat.S_ISDIR(st.st_mode):
            continue
        for f in sorted(os.listdir(dp)):
            if not f.endswith(EXT):
                continue
            p = os.path.join(dp, f)
            try:
                st = os.lstat(p)
            except OSError as exc:
                if exc.errno == errno.ENOENT:
                    continue
                raise
            if not stat.S_ISREG(st.st_mode):
                continue
            gp = '/' + (d + '/' if d else '') + f
            if gp not in want:
                stale.append((gp, p))
    return stale


def show(label, stale):
    print("[{}] マニフェストに無い {} : {} 件".format(label, EXT, len(stale)))
    for gp, p in stale:
        try:
            st = os.stat(p)
            print("  {:<36} {:>9}  {}".format(
                gp, st.st_size, time.strftime('%Y-%m-%d', time.localtime(st.st_mtime))))
        except OSError:
            print("  {:<36} (stat 失敗)".format(gp))


def hostdrv_root():
    if os.environ.get('HOSTDRV_DIR'):
        return os.environ['HOSTDRV_DIR']
    env_path = os.path.join(PROJ_DIR, '.env')
    if os.path.isfile(env_path):
        with open(env_path) as f:
            for line in f:
                line = line.strip()
                if line.startswith('HOSTDRV_DIR='):
                    return line.split('=', 1)[1].strip().strip('"').strip("'")
    return '/mnt/c/os32'


def is_protected(root, path):
    """消す直前の保護判定。判定できなければ例外を上に投げて失敗にする。

    自身だけでなく**祖先**も見る (`/etc/settings.db/` がディレクトリのとき
    その中の残骸を消さない、往復 3 の D1 後半)。
    """
    anc = protect.protected_ancestor(root, path)
    if anc is not None:
        protect.protect_log(anc)
        return True
    if protect.is_protected(root, path):
        protect.protect_log(protect.guest_path_of(root, path))
        return True
    return False


def prune_hostdrv(want, delete):
    root = hostdrv_root()
    if not os.path.isdir(root):
        print("Error: HOSTDRV_DIR {} が無い".format(root), file=sys.stderr)
        return None
    # stale が 0 件でも <root>/etc の異常で止める (往復 2 の 8)
    try:
        protect.check_tree(root)
    except protect.ProtectError as exc:
        print("Error: 配備の前提検査に失敗: {}".format(exc), file=sys.stderr)
        return None
    try:
        stale = find_stale(root, want)
    except OSError as exc:
        print("Error: 候補を集められない: {}".format(exc), file=sys.stderr)
        return None
    show('hostdrv ' + root, stale)
    if not delete:
        return len(stale)
    removed = 0
    for gp, p in stale:
        try:
            if is_protected(root, p):
                continue          # 除外は失敗ではない。件数にも数えない。
            os.remove(p)
        except protect.ProtectError as exc:
            print("Error: 保護判定に失敗: {}".format(exc), file=sys.stderr)
            return None
        except OSError as exc:
            print("Error: {} を消せなかった: {}".format(gp, exc),
                  file=sys.stderr)
            return None
        print("  removed {}".format(gp))
        removed += 1
    return removed


def prune_nhd(want, delete):
    import nhd_deploy  # sudo mount まわりを流用
    if not nhd_deploy.ensure_local_nhd():
        return None
    if not nhd_deploy.ensure_mounted():
        print("Error: NHD をマウントできない", file=sys.stderr)
        return None
    root = nhd_deploy.MOUNT_POINT
    try:
        protect.check_tree(root)
    except protect.ProtectError as exc:
        print("Error: 配備の前提検査に失敗: {}".format(exc), file=sys.stderr)
        return None
    try:
        stale = find_stale(root, want)
    except OSError as exc:
        print("Error: 候補を集められない: {}".format(exc), file=sys.stderr)
        return None
    show('nhd ' + nhd_deploy.NHD_LOCAL, stale)
    if not delete:
        return len(stale)
    removed = 0
    for gp, p in stale:
        try:
            if is_protected(root, p):
                continue          # 除外は失敗ではない。件数にも数えない。
        except protect.ProtectError as exc:
            print("Error: 保護判定に失敗: {}".format(exc), file=sys.stderr)
            return None
        result = subprocess.run(['sudo', 'rm', '-f', '--', p],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print("Error: {} を消せなかった: {}".format(
                gp, (result.stderr or '').strip()), file=sys.stderr)
            return None
        print("  removed {}".format(gp))
        removed += 1
    result = subprocess.run(['sync'], capture_output=True, text=True)
    if result.returncode != 0:
        print("Error: sync 失敗: {}".format((result.stderr or '').strip()),
              file=sys.stderr)
        return None
    print("  (Windows 側への反映は deploy-nhd の deploy 段。NP21/W 停止中に行うこと)")
    return removed


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    delete = '--delete' in sys.argv[1:]
    target = args[0] if args else 'both'
    if target not in ('hostdrv', 'nhd', 'both'):
        print(__doc__)
        return 1
    want = wanted_guest_paths()
    if want is None:
        print("RESULT: FAIL 配備マニフェストが読めない")
        return 1
    counts = {}
    if target in ('hostdrv', 'both'):
        counts['hostdrv'] = prune_hostdrv(want, delete)
    if target in ('nhd', 'both'):
        counts['nhd'] = prune_nhd(want, delete)
    if any(v is None for v in counts.values()):
        print("RESULT: FAIL " + ", ".join(
            "{}={}".format(k, 'error' if v is None else v) for k, v in counts.items()))
        return 1
    verb = '削除' if delete else '検出 (未削除、--delete で消す)'
    print("RESULT: OK " + ", ".join("{}={}".format(k, v) for k, v in counts.items())
          + " 件を" + verb)
    return 0


if __name__ == '__main__':
    sys.exit(main())
