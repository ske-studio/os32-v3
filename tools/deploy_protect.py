#!/usr/bin/env python3
"""deploy_protect.py — 通常配備が /etc/settings.db* を壊さないための共通判定 (票 S0-D / D0)

契約の正典: docs/archive/settings/S0_FOUNDATION.md §5、手順は docs/archive/settings/TASK_S0.md §2。

設定レジストリ (`/etc/settings.db`) は**ゲストが書く**もので、ホストのビルド成果物では
ない。ところが配備ツールは「マニフェストにある物を書く」「HostDrv の中身をそのまま
NHD へ写す」「マニフェストに無い物を消す」の 3 系統があり、どこか 1 つでも settings.db
を掴むとユーザーの設定が消える。ここは**書く / 消す / 切り詰める直前**に 1 か所で止める
ための判定だけを置く。新しい配備 framework は作らない。

判定は 2 段 (Codex 往復 2 の 6):

  (1) 名前規則 — realpath の**前**に、字句正規化したゲスト名の親が /etc で
      basename が保護対象名なら真。実体が欠損していても (dangling symlink、
      `etc -> conf` のような別名) 「欠損は欠損のまま」守れる。
  (2) 実体規則 — realpath で解決し、root の外なら拒否、解決先の st_dev/st_ino が
      **存在する** <root>/etc/settings.db* のいずれかと一致すれば真 (hardlink /
      symlink の別名対策)。解決後のゲスト名にも (1) を当てる。

`stat` の失敗は **ENOENT (確定した不存在) だけ「保護対象ではない」**とする。初回の
`/etc/settings.tsv` のような新規ファイルを配備できるのはこのため。それ以外
(EACCES / EIO / ELOOP / 解決失敗) は保護側に倒し、ProtectError で**配備全体を失敗**
させる (往復 3 の 3)。判断できないまま書くほうが危ない。

`<root>/etc` 自体が symlink / 別マウント / 通常ファイルなら配備全体を拒否する。
さらに **配備ツリーに symlink が 1 つでもあれば拒否** する (`check_tree`、往復 3 の
PM 方針)。symlink を許すと「最終パス」の意味が組み合わせ爆発し (補完後の再補完、
解決後の祖先、中間リンクの削除、リンク越しの別名)、レビューの blocker はほぼ全部
その形だった。OS32 の ext2 に symlink を作る手段は無く、HostDrv は Windows の
フォルダなので、ツリーに symlink が現れること自体が「未対応の配置」。
bind mount の別名はツールでは検出しきれないので運用で禁止する。

標準ライブラリのみ。実配備・sudo・mount は一切行わない (判定と印字だけ)。
"""

import errno
import os
import stat
import sys

# 保護対象のベース名 (大文字小文字を区別しない)。
# WAL / SHM は OS32 が WAL を使う意味ではなく、安全側の巻き取り。
#
# `.bak` 以降はリカバリ (`install --recover-settings` / `--revert-settings`)
# が /etc に作る 7 名 (票 TASK_S3 §1b の 9 名から本体と `-journal` を除いた
# もの、S3-D / 往復 3 の B5)。`.bak` + `.bak-journal` は切替前の元の対の
# **唯一の写し**、`.recover-state` は phase の印で、通常配備が 1 つでも
# 掴むと「元へ戻す」経路そのものが消える。`.new*` / `.failed*` は他人の
# 生成物なので、配備が作ることも消すこともしない。
PROTECTED_BASENAMES = frozenset([
    'settings.db',
    'settings.db-journal',
    'settings.db-wal',
    'settings.db-shm',
    'settings.db.bak',
    'settings.db.bak-journal',
    'settings.db.failed',
    'settings.db.failed-journal',
    'settings.db.new',
    'settings.db.new-journal',
    'settings.db.recover-state',
])

# 保護対象ディレクトリ (ゲストの絶対パス、小文字)
PROTECTED_DIR = '/etc'

# 走査から外す**ルート直下の既知ディレクトリ**。
# ext2 を mkfs すると必ずルート直下に root 所有 mode 700 の `lost+found` が
# できる。配備ツールは非 root の Python で走査する (実コピーだけ sudo cp) ので
# 読めず、「ツリーを辿れない = 失敗」に当たっていた (実配備 1 回目、2026-09-13)。
# 配備対象でも保護対象でもないので、ここだけ走査から外す。
SKIP_ROOT_DIRS = frozenset(['lost+found'])


class ProtectError(Exception):
    """保護の判定そのものができなかった / 迂回が見つかった。配備を失敗させる。"""


class ProtectedPath(ProtectError):
    """その道に保護対象が居るので**作らない / 書かない**。

    ProtectError を継承しているので「保護の系が投げた」ことは 1 つの except で
    受けられるが、呼び出し側は**除外 (成功、ログだけ)** と「判定できない (失敗)」
    を区別すること (票 §6 往復 1 の B2 / B7)。
    """

    def __init__(self, guest, message=None):
        ProtectError.__init__(
            self, message or '保護対象なので作らない: {}'.format(guest))
        self.guest = guest


# ---------------------------------------------------------------- 字句正規化

def normalize_guest_path(guest_path, host_src=None):
    """ゲストパスを字句正規化して先頭 '/' 付きの絶対パスにする。

    - `'/etc/'` のようなディレクトリ指定は host_src の basename を補う。
    - `'.'` / 連続 `'/'` を畳み、`'..'` は 1 段戻す。root を越えるものは拒否。
    OS には一切触らない (symlink を辿らない = realpath より前に使える)。
    """
    if guest_path is None:
        raise ProtectError('ゲストパスが None')
    p = str(guest_path)
    # `guest: /etc/` のようなディレクトリ指定は、ソースがあれば basename を補う。
    # ソースが無い (mkdir の判定など) ならディレクトリそのものとして扱う。
    if (p.endswith('/') or p == '') and host_src is not None:
        p = p + os.path.basename(host_src)

    parts = []
    for seg in p.split('/'):
        if seg == '' or seg == '.':
            continue
        if seg == '..':
            if not parts:
                raise ProtectError(
                    'root の外へ出るゲストパス: {!r}'.format(guest_path))
            parts.pop()
            continue
        parts.append(seg)
    # `/bin/..` のように root へ戻り切るのは**正当**に '/' (往復 1 の B9)。
    # 拒否するのは root より上へ出る場合だけ (上の '..' の分岐)。
    if not parts:
        return '/'
    return '/' + '/'.join(parts)


def name_is_protected(guest_path):
    """字句正規化済みのゲストパスに**名前規則**だけを当てる (OS に触らない)。"""
    head, _, base = guest_path.rpartition('/')
    if not head:
        head = '/'
    return head.lower() == PROTECTED_DIR and base.lower() in PROTECTED_BASENAMES


# ------------------------------------------------------------ 最終パスの確定

def _inside(root_abs, path):
    return path == root_abs or path.startswith(root_abs + os.sep)


def resolve_dest(root, guest_path, host_src=None):
    """**実際に操作する最終ファイル名**をホスト側の絶対パスで確定する。

    root は配備先のツリー (マウントした NHD / HostDrv のルート)。
    root の外へ出る結果は ProtectError。

    字句の補完 (`guest: /etc/`) だけでは足りない: `cp SRC /etc` も
    `shutil.copy2(SRC, '/etc')` も、**宛先が既存ディレクトリなら中へ**書く。
    `--dest / --rename etc` に `settings.db` という名前のソースを渡すと、字句上の
    宛先 `/etc` は保護対象ではないのに実体は `/etc/settings.db` になる
    (往復 1 の B1)。ここで実ディレクトリを見て basename を補い、呼び出し側は
    **この戻り値だけ**を cp / copy2 に渡すこと (ディレクトリを渡さない)。
    """
    guest = normalize_guest_path(guest_path, host_src)
    root_abs = os.path.abspath(root)
    dest_abs = os.path.abspath(os.path.join(root_abs, guest.lstrip('/')))
    if not _inside(root_abs, dest_abs):
        raise ProtectError(
            '配備先 {!r} が root {!r} の外にある'.format(dest_abs, root_abs))
    # symlink 越しのディレクトリでも cp / copy2 は中へ書くので isdir で見る
    # (辿った先が root の外なら下の検査か is_protected の realpath が止める)。
    if host_src is not None and os.path.isdir(dest_abs):
        dest_abs = os.path.join(dest_abs, os.path.basename(host_src))
        if not _inside(root_abs, dest_abs):
            raise ProtectError(
                '配備先 {!r} が root {!r} の外にある'.format(dest_abs, root_abs))
    # 補完は 1 回まで。補完した先がまだディレクトリだと cp / copy2 がもう一度
    # basename を補う (往復 2 の 1)。ただし**保護対象に守られている**なら
    # (自身が保護対象名 / 祖先に保護対象が居る) 答えは出ている — そこは
    # 「判定できない失敗」ではなく「書かずに成功除外」にする。
    # 祖先を先に見る: `<root>/etc/settings.db/settings.db/` のように自身の親が
    # `/etc` でない形でも、祖先の `/etc/settings.db` が守っている (追加往復 1)。
    if host_src is not None and os.path.isdir(dest_abs):
        if protected_ancestor(root_abs, dest_abs) is not None:
            return dest_abs             # check_dest が True を返す = 除外
        if name_is_protected(guest_path_of(root_abs, dest_abs)):
            return dest_abs             # is_protected が True を返す = 除外
        raise ProtectError(
            '配備先 {!r} がディレクトリを指している '
            '(cp / copy2 が中へ書いてしまう)'.format(dest_abs))
    return dest_abs


def guest_path_of(root, dest):
    """root 配下のホストパスからゲストの絶対パスを逆算する。"""
    root_abs = os.path.abspath(root)
    dest_abs = os.path.abspath(dest)
    if dest_abs == root_abs:
        return '/'
    if not dest_abs.startswith(root_abs + os.sep):
        raise ProtectError(
            '{!r} が root {!r} の外にある'.format(dest_abs, root_abs))
    rel = dest_abs[len(root_abs) + 1:]
    return '/' + rel.replace(os.sep, '/')


# -------------------------------------------------------------------- 実体側

def _stat_or_none(path):
    """os.stat。ENOENT だけ None、他の失敗は ProtectError (保護側に倒す)。"""
    try:
        return os.stat(path)
    except OSError as exc:
        if exc.errno == errno.ENOENT:
            return None
        raise ProtectError(
            '{} を stat できない ({})'.format(path, exc.strerror))


def check_root_etc(root):
    """<root>/etc が素のディレクトリで root と同じマウントにあることを確かめる。

    symlink / 別マウント / 通常ファイルなら ProtectError = **配備全体を拒否**。
    別名の bind mount は検出できないので運用で禁止する (TASK_S0 §2)。

    **全操作の前提検査**。名前規則だけで答えが出る経路 (`rm /etc/settings.db`
    など) でも必ず先に通す — `/etc` がすり替わっていれば、その配備は
    「保護対象かどうか」以前に信用できない (往復 1 の B4)。
    """
    root_abs = os.path.abspath(root)
    real_root = os.path.realpath(root_abs)
    etc = os.path.join(real_root, 'etc')
    if os.path.islink(etc):
        raise ProtectError('{} が symlink。配備を中止する'.format(etc))
    st_etc = _stat_or_none(etc)
    if st_etc is None:
        return
    if not stat.S_ISDIR(st_etc.st_mode):
        raise ProtectError(
            '{} がディレクトリではない。配備を中止する'.format(etc))
    if os.path.realpath(etc) != etc:
        raise ProtectError('{} が字句と異なる実体を指す。配備を中止する'.format(etc))
    st_root = _stat_or_none(real_root)
    if st_root is None:
        raise ProtectError('{} が消えた'.format(real_root))
    if st_etc.st_dev != st_root.st_dev:
        raise ProtectError(
            '{} が root と別のマウント。配備を中止する'.format(etc))


def _protected_identities(root):
    """<root>/etc にある**存在する**保護対象の (st_dev, st_ino) 一覧。"""
    real_root = os.path.realpath(os.path.abspath(root))
    etc = os.path.join(real_root, 'etc')
    try:
        names = os.listdir(etc)
    except OSError as exc:
        if exc.errno == errno.ENOENT:
            return []                   # 確定した不存在だけ「無い」とみなす
        # ENOTDIR / EACCES / EIO — 判断できないまま書くほうが危ない (往復 1 の B4)
        raise ProtectError('{} を読めない ({})'.format(etc, exc.strerror))
    ids = []
    for name in names:
        if name.lower() not in PROTECTED_BASENAMES:
            continue
        st = _stat_or_none(os.path.join(etc, name))
        if st is not None:
            ids.append((st.st_dev, st.st_ino))
    return ids


def is_protected(root, dest):
    """dest (ホスト側の最終パス) が保護対象かを 2 段で判定する。

    作成 / 切り詰め / 削除 / rename の**直前**に、確定した最終パスで呼ぶこと。
    判定できない状況は ProtectError (= 配備を失敗させる)。
    """
    # --- (0) 前提検査: <root>/etc がすり替わっていないか。名前規則で即答できる
    #         経路 (rm /etc/settings.db 等) でも必ず通す (往復 1 の B4)。
    check_root_etc(root)

    guest = guest_path_of(root, dest)

    # --- (1) 名前規則: realpath より前。実体が無くても守る。
    if name_is_protected(guest):
        return True

    # --- (2) 実体規則
    real_root = os.path.realpath(os.path.abspath(root))
    try:
        real_dest = os.path.realpath(os.path.abspath(dest))
    except OSError as exc:                                  # pragma: no cover
        raise ProtectError('{} を解決できない ({})'.format(dest, exc))
    if real_dest != real_root and not real_dest.startswith(real_root + os.sep):
        raise ProtectError(
            '{} は解決すると root {} の外を指す'.format(dest, real_root))

    # 解決後のゲスト名にも名前規則を当てる (symlink 別名で保護対象を作る道を塞ぐ)。
    if name_is_protected(guest_path_of(real_root, real_dest)):
        return True

    st = _stat_or_none(dest)
    if st is None:
        return False                    # 確定した不存在 = 新規の通常ファイル
    ident = (st.st_dev, st.st_ino)
    return ident in _protected_identities(root)


def mkdir_chain(root, guest_dir):
    """`mkdir -p` / `os.makedirs` が作る**各祖先**を root から順に確定する。

    `-p` / `makedirs` は途中のディレクトリを黙って作るので、最終要素だけを見ても
    `/etc/settings.db/a` の `settings.db` がディレクトリとして生える
    (往復 1 の B2)。root から 1 段ずつ判定し、保護対象に当たったら
    ProtectedPath を投げる (= その道は作らない。除外なので呼び出し側は成功扱い、
    往復 1 の B7)。

    Returns: 作るべき絶対パスの一覧 (root 自身は含まない、上から順)。
    """
    check_root_etc(root)
    guest = normalize_guest_path(guest_dir)
    root_abs = os.path.abspath(root)
    chain = []
    cur = root_abs
    for seg in guest.strip('/').split('/'):
        if not seg:
            continue
        cur = os.path.join(cur, seg)
        if not _inside(root_abs, cur):
            raise ProtectError(
                'ディレクトリ {!r} が root {!r} の外にある'.format(cur, root_abs))
        if is_protected(root_abs, cur):
            raise ProtectedPath(guest_path_of(root_abs, cur))
        chain.append(cur)
    return chain


def protected_ancestor(root, dest):
    """dest の**祖先**に保護対象が居ればその guest パスを返す (居なければ None)。

    `/etc/settings.db` がディレクトリなら `/etc/settings.db/inner` へのコピーは
    保護対象の中身を作る。逆に `settings.db` が通常ファイルなら
    `/etc/settings.db/sub/file` の stat は ENOTDIR で落ちる — どちらも
    「最終パスだけ」を見ていると取りこぼすので、**stat より先に**祖先を見る
    (往復 2 の 2)。規則は mkdir_chain と同じ。

    dest 自身は含まない (それは is_protected の仕事)。
    """
    root_abs = os.path.abspath(root)
    guest = guest_path_of(root_abs, dest)
    segs = [s for s in guest.strip('/').split('/') if s]
    cur = root_abs
    for seg in segs[:-1]:
        cur = os.path.join(cur, seg)
        if not _inside(root_abs, cur):
            raise ProtectError(
                '祖先 {!r} が root {!r} の外にある'.format(cur, root_abs))
        if is_protected(root_abs, cur):
            return guest_path_of(root_abs, cur)
    return None


def skip_root_entries(root, dirpath, dirnames):
    """`dirnames` から**ルート直下の既知ディレクトリ** (SKIP_ROOT_DIRS) を外す。

    対象は「ルート直下」の「実ディレクトリ」だけ。symlink や通常ファイルなら
    **外さない** — 従来どおり判定させる (symlink は check_tree が拒否する)。
    それ以外の読めないディレクトリは契約どおり失敗のまま (判定不能は失敗)。

    Returns: 外した名前のリスト (呼び手のログ用)。
    """
    if os.path.abspath(dirpath) != os.path.abspath(root):
        return []
    removed = []
    for name in list(dirnames):
        if name not in SKIP_ROOT_DIRS:
            continue
        full = os.path.join(dirpath, name)
        if os.path.islink(full):
            continue                    # symlink は除外しない (拒否させる)
        try:
            st = os.lstat(full)
        except OSError:
            continue                    # 判定できないものは外さない
        if not stat.S_ISDIR(st.st_mode):
            continue
        dirnames.remove(name)
        removed.append(name)
    return removed


def walk_root(root, onerror=None):
    """配備ツリーを走査する共通の入口 (`os.walk` の薄い包み)。

    ルート直下の SKIP_ROOT_DIRS を降りる前に外すので、`lost+found` で
    止まらない。走査する側は全部これを通すこと。
    """
    root_abs = os.path.abspath(root)
    for dirpath, dirnames, filenames in os.walk(root_abs, followlinks=False,
                                                onerror=onerror):
        skip_root_entries(root_abs, dirpath, dirnames)
        yield dirpath, dirnames, filenames


def check_tree(root):
    """配備ツリー全体に **symlink が 1 つも無い**ことを確かめる (前提検査)。

    symlink を許すと「最終パス」の意味が組み合わせ爆発する: 補完後の再補完、
    解決後の祖先、中間リンクの削除、リンク越しの別名 — 往復 1〜3 の blocker は
    ほとんどがこの形だった。OS32 の ext2 に symlink を作る手段は無く、HostDrv は
    Windows のフォルダなので、**配備ツリーに symlink が現れること自体が
    「未対応の配置」**。見つけたら配備全体を拒否する (PM 方針、往復 3)。

    走査から外すのは 2 つだけ: ルート直下の `lost+found` (ext2 が必ず作る
    root 所有 mode 700。配備対象でも保護対象でもない) と、保護対象名の
    ディレクトリの内部 (配備が 1 バイトも書かない場所)。それ以外の読めない
    ディレクトリは契約どおり失敗にする (判定不能は失敗)。

    `<root>/etc` 単体の検査 (check_root_etc) より重いので、判定ごとではなく
    **サブコマンドの入口で 1 回**だけ呼ぶこと。
    """
    check_root_etc(root)
    root_abs = os.path.abspath(root)
    if os.path.islink(root_abs):
        raise ProtectError(
            '配備先 {} 自体が symlink。配備を中止する'.format(root_abs))

    errors = []
    for dirpath, dirnames, filenames in walk_root(root_abs,
                                                  onerror=errors.append):
        if errors:
            break
        for name in list(dirnames) + list(filenames):
            full = os.path.join(dirpath, name)
            if os.path.islink(full):
                raise ProtectError(
                    '配備ツリーに symlink がある: {} '
                    '(未対応の配置。配備を中止する)'.format(full))
        # 保護対象名のディレクトリには**降りない**。配備は中へ 1 バイトも
        # 書かないので、中に何があろうと最終パスの意味には効かない。降りると
        # 読めない残骸 (`etc/settings.db/` の chmod 000) だけで配備全体が
        # 止まってしまう (往復 2 の 9 と噛み合わない)。
        dirnames[:] = [
            d for d in dirnames
            if not name_is_protected(
                guest_path_of(root_abs, os.path.join(dirpath, d)))]
    if errors:
        raise ProtectError(
            '配備ツリーを辿れない: {}'.format(errors[0]))


def protect_log(path, stream=None):
    """除外を明示する 1 行。失敗ではないので stdout に出す。"""
    print('protected: {} (skipped)'.format(path),
          file=stream if stream is not None else sys.stdout)


def check_dest(root, guest_path, host_src=None):
    """最終パスの確定と保護判定をまとめて行う (書き込み経路の唯一の入口)。

    順序が肝: 前提検査 → 最終パスの確定 → **祖先** → 最終パス自身。
    祖先を stat より先に見ないと、`/etc/settings.db/sub/file` のように
    途中が通常ファイルの宛先が ENOTDIR で「判定できない」失敗になる
    (往復 2 の 2)。

    Returns: (dest_abs, protected)  — ProtectError は呼び出し側で失敗にすること。
    """
    check_root_etc(root)
    dest = resolve_dest(root, guest_path, host_src)
    anc = protected_ancestor(root, dest)
    if anc is not None:
        return dest, True               # 除外 (失敗ではない)
    return dest, is_protected(root, dest)


if __name__ == '__main__':                                  # pragma: no cover
    if len(sys.argv) != 3:
        print('usage: deploy_protect.py <root> <guest_path>', file=sys.stderr)
        sys.exit(2)
    try:
        d, prot = check_dest(sys.argv[1], sys.argv[2])
    except ProtectError as e:
        print('ProtectError: {}'.format(e), file=sys.stderr)
        sys.exit(2)
    print('{}\t{}'.format('PROTECTED' if prot else 'writable', d))
    sys.exit(0)
