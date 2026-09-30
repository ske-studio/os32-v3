#!/usr/bin/env python3
"""Narrow NP21/W byte transformation and OFFLINE preparation only.

No emulator/credential imports, shell, network, process control or live writer.
--apply requires --output: writes only a new private offline bundle, never the
input snapshot. Live apply is blocked until a trustworthy exit verifier and
local emu_agent orchestration are integrated. See os32-emu-config/SKILL.md.
"""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import uuid

# Proven by docs/archive/gui_v11/TASK_H3_cirrus.md §0 and TASKS.md 2026-09-06
# (WAB OFF recheck). Other WAB fields/board IDs have no approved values here.
#
# USEPEGCP is the PEGC gate, proven by np21w-src src/win9x/ini.cpp:687
# (PFVAL("USEPEGCP", PFTYPE_BOOL, &np2cfg.usepegcplane), same s_IniItems[]
# table and [NekoProject21] section as USEGD5430) and src/io/pegc.c:375
# (pegc.enable = np2cfg.usepegcplane), which mem/memvga.c reads on every
# PEGC VRAM path. pc_model is NOT the gate: OS32's pegc_probe() reads BIOS
# work area 0x045C bit6 / 0x0597 bit2, and both are already set with
# pc_model=VX (measured on the guest 2026-09-09).
#
# All fields are required even when changing one, so a partial or unknown
# backend configuration fails closed. NP21/W's initsave writes the whole
# s_IniItems[] table, so an ini it produced always carries them.
#
# EXMEMORY は ini では `ExMemory` と混在ケースで書かれる。transform はキーを
# 大文字化して照合し、書き換えるのは値だけなので ini 側の綴りは保たれる
# (既存の 3 キーはたまたま全大文字だったのでこの区別が要らなかった)。
# MB 単位の拡張メモリ (win9x/ini.cpp:477, PFTYPE_UINT16。
# SUPPORT_LARGE_MEMORY 無効時は UINT8、どちらも MEMORY_MAXSIZE でクランプ)。
# ブートローダが 1MB から 512KB 刻みで実測するので (boot/loader_fat.asm:248)、
# ゲストの総容量はこの値で決まる。値は 2 つだけ通す:
#   16 = 現行構成 (ゲスト 15360KB = 15MB。実測 2026-09-09)
#    7 = ゲスト 8MB。memory_boot の legacy フォールバックを通す
#    8 = ゲスト 9MB。PEGC (640x480) の 300KB 予約がアプリ帯の外に出る最小構成
# 8MB は CUI の最低動作環境であって GUI の最低要件ではない
# (INSTALL.md / docs/02_memory.md / tasks/gui/DESIGN.md)。
ALLOWED = {'USEGD5430': ('true', 'false'), 'GD5430TYPE': ('91',),
           'USEPEGCP': ('true', 'false'), 'EXMEMORY': ('7', '8', '16', '33', '129')}

# パス値を持つキーは固定値の白リストでは表せないので別表にする (票 S3I2-T)。
#   HDD1FILE  : IDE 第1スロットのイメージ。値は NP21W_DIR 直下の *.nhd の
#               「名前」で受け取り、Windows 絶対パスへ展開して書く。
#   FDD1FILE  : FDD の装着。SVFDFILE=true の構成では装着が ini に保存され
#   FDD2FILE    次回も再装着されるので、HDD ブートでは空 (装着解除) にする。
#               変更後の値は空だけを許す。変更前の値は非空の CP932 パスでも
#               よく、内容を検査せずそのまま空へ差し替える。
# transform() の「変更キーがあれば全キーの存在を要求」は ALLOWED と
# ALLOWED_PATHS で別々に適用する。固定値キーだけを触る live 操作が、この新しい
# キーの存在に依存しないため (逆も同じ)。
ALLOWED_PATHS = {'HDD1FILE': '.nhd', 'FDD1FILE': '', 'FDD2FILE': ''}
IMAGE_NAME = re.compile(r'[A-Za-z0-9_.-]+')
WINDOWS_PATH_UNITS = 240  # np21w_ini_live.path_key と同じ保守的な上限
SECTION = b'nekoproject21'
LIMIT = 4 * 1024 * 1024


class IniError(ValueError):
    """A safe, content-free diagnostic suitable for operator output."""


def windows_path(path):
    """ASCII only, absolute, no device/traversal components. Content-free errors.

    `;` と `#` は ini の注釈開始と区別できないので、**書く側でも**拒否する。
    これを許すと自分で作った ini を同じ変更で読み直せなくなる (往復 1 の B4)。
    末尾の空白やドットを含む成分もここで落ちる (往復 1 の B3)。
    """
    if (not isinstance(path, str) or not path.isascii() or
            len(path) >= WINDOWS_PATH_UNITS or
            not re.fullmatch(r'[A-Za-z]:\\[^"<>|?*:;#\x00-\x1f]+', path) or
            any(p in ('', '.', '..') or p.endswith((' ', '.')) for p in path[3:].split('\\'))):
        raise IniError('unsupported absolute Windows path')
    if any(re.fullmatch(r'(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?', part, re.I)
           for part in path[3:].split('\\')):
        raise IniError('reserved Windows device path')
    return path


def image_name(name, extension):
    """NP21W_DIR 直下に置ける名前だけを通す (パス成分や隠し名は不可)。"""
    if (not isinstance(name, str) or not extension or not name.endswith(extension) or
            len(name) <= len(extension) or not IMAGE_NAME.fullmatch(name) or
            name.startswith('.') or '..' in name):
        raise IniError('unsupported image name')
    return name


def _wslpath(option, path):
    """1 回の純粋なパス変換。**終端の改行だけ**を取り除き、空白は保持する
    (往復 1 の B3: .strip() は末尾空白のあるディレクトリを別の対象に変えた)。"""
    try:
        done = subprocess.run(['wslpath', option, path], stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, timeout=20, check=True)
        text = done.stdout.decode('ascii')
    except (OSError, ValueError, UnicodeError, subprocess.SubprocessError):
        raise IniError('path conversion unavailable') from None
    if text.endswith('\r\n'):
        text = text[:-2]
    elif text.endswith('\n'):
        text = text[:-1]
    if not text or '\n' in text or '\r' in text:
        raise IniError('unsupported path conversion output')
    return text


def np21w_directory():
    """(WSL path, Windows path) of NP21W_DIR. Environment only: never reads .env,
    credentials or any env-loading helper [D3]. wslpath is a pure path query.

    変換前後が同じ対象を指すことを `wslpath -u` で往復させて確かめる。
    """
    directory = os.environ.get('NP21W_DIR')
    if (not isinstance(directory, str) or not directory.startswith('/') or
            '\0' in directory or '\n' in directory or '\r' in directory):
        raise IniError('explicit absolute NP21W_DIR required for path fields')
    windows = windows_path(_wslpath('-w', directory))
    back = _wslpath('-u', windows)
    if back.rstrip('/') != directory.rstrip('/'):
        raise IniError('NP21W_DIR does not round-trip through wslpath')
    return directory, windows


def resolve_image(name, extension):
    """NP21W_DIR 直下の <name> を (ホスト側パス, Windows 絶対パス) へ解決する。

    **環境変数を読むのはここだけ**。呼び手は返った解決済みパスを承認計画に
    束縛し、実行時に環境から別の対象へ再展開しない (往復 1 の B2)。
    通常ファイルであることまで見る (同 B5: `.nhd` という名前のディレクトリ)。
    """
    image_name(name, extension)
    directory, windows = np21w_directory()
    host = os.path.join(directory, name)
    if os.path.islink(host) or not os.path.isfile(host):
        raise IniError('image is not a regular file under NP21W_DIR')
    return host, windows_path(windows + '\\' + name)


def _path_value(key, value):
    """書き込む literal を返す。HDD1FILE は**解決済みの絶対パス**で受ける
    (名前からの展開は resolve_image ひとつだけ = 計画時か CLI の入口)。
    ここは純粋な構文検査で、ホスト側の存在は見ない (見る側は resolve_image)。"""
    extension = ALLOWED_PATHS[key]
    if not extension:
        if value != '':
            raise IniError('only detach (empty value) is supported for this field')
        return ''
    path = windows_path(value)
    image_name(path.rsplit('\\', 1)[-1], extension)
    return path


def _changes(changes):
    """Validate both tables and return {KEY: literal value written to the ini}."""
    if not isinstance(changes, dict) or not changes:
        raise IniError('nonempty allowlisted changes required')
    resolved = {}
    for key, value in changes.items():
        if key in ALLOWED:
            if value not in ALLOWED[key]:
                raise IniError('unsupported backend field or value')
            resolved[key] = value
        elif key in ALLOWED_PATHS:
            resolved[key] = _path_value(key, value)
        else:
            raise IniError('unsupported backend field or value')
    return resolved


def transform(raw, changes):
    """Pure bytes -> (bytes, changed-field lines); never decode opaque content.

    ASCII-compatible input only (including CP932 and UTF-8 BOM). Preserve line
    terminators, spacing, comments and all unrelated bytes. Reject UTF-16/32,
    NUL and ambiguous target section/fields. Require both proven fields even
    when changing one, so a partial/unknown backend configuration fails closed.
    """
    resolved = _changes(changes)
    want_fixed = any(key in ALLOWED for key in changes)
    want_path = any(key in ALLOWED_PATHS for key in changes)
    if not isinstance(raw, bytes) or len(raw) > LIMIT or b'\0' in raw:
        raise IniError('unsupported snapshot bytes or size')
    if raw.startswith((b'\xff\xfe', b'\xfe\xff')):
        raise IniError('unsupported encoding')
    parts = re.split(b'(\r\n|\r|\n)', raw)
    section = None
    sections = 0
    found = {}
    for i in range(0, len(parts), 2):
        line = parts[i]
        bom = b'\xef\xbb\xbf' if i == 0 and line.startswith(b'\xef\xbb\xbf') else b''
        body = line[len(bom):]
        stripped = body.strip(b' \t')
        if not stripped or stripped.startswith((b';', b'#')):
            continue
        if stripped.startswith(b'['):
            match = re.fullmatch(rb'\[([^\]\r\n]+)\][ \t]*(?:[;#].*)?', stripped)
            if not match:
                raise IniError('malformed section')
            section = match[1].strip().lower()
            if section == SECTION:
                sections += 1
            continue
        if section != SECTION:
            continue
        match = re.fullmatch(rb'([ \t]*)([A-Za-z0-9_]+)([ \t]*=[ \t]*)([^;#]*)([;#].*)?', body)
        if not match:
            raise IniError('malformed target section assignment')
        key = match[2].decode('ascii').upper()
        fixed = want_fixed and key in ALLOWED
        path = want_path and key in ALLOWED_PATHS
        if not fixed and not path:
            continue
        if key in found:
            raise IniError('duplicate backend field')
        token = match[4].rstrip(b' \t')
        if fixed and token not in tuple(v.encode('ascii') for v in ALLOWED[key]):
            raise IniError('unsupported existing backend value')
        if path and match[5] is not None:
            # A path value is opaque CP932, so a ';'/'#' tail cannot be told
            # apart from a comment. Refuse instead of guessing (fail closed).
            raise IniError('unsupported separator in path field')
        start = len(bom) + match.start(4)
        found[key] = (i, start, start + len(token), token)
    required = (set(ALLOWED) if want_fixed else set()) | (set(ALLOWED_PATHS) if want_path else set())
    if sections != 1 or set(found) != required:
        raise IniError('missing or duplicate target section/backend fields')
    diff = []
    for key in list(ALLOWED) + list(ALLOWED_PATHS):
        if key not in changes:
            continue
        i, start, end, old = found[key]
        new = resolved[key].encode('ascii')
        if old == new:
            continue
        parts[i] = parts[i][:start] + new + parts[i][end:]
        if key in ALLOWED:
            diff.append(f"{key}: {old.decode('ascii')} -> {resolved[key]}")
        else:
            # Never print the previous opaque path; the new one is operator-chosen.
            diff.append('%s: %s -> %s' % (key, 'set' if old else 'empty',
                                          resolved[key] or 'empty'))
    candidate = b''.join(parts)
    if len(candidate) > LIMIT:
        raise IniError('result exceeds snapshot limit')
    return candidate, diff


@contextmanager
def _directory(path):
    """Anchor every component with directory FDs; reject all symlinks."""
    path = Path(path)
    if '..' in path.parts:
        raise IniError('parent traversal is not supported')
    path = path.absolute()
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in path.parts[1:]:
            try:
                nxt = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            except OSError as exc:
                raise IniError('directory unavailable or symlink') from exc
            os.close(fd)
            fd = nxt
        yield fd
    finally:
        os.close(fd)


def _signature(info):
    return [info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns]


def _read(fd, name):
    try:
        handle = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fd)
    except OSError as exc:
        raise IniError('file unavailable or symlink') from exc
    before = os.fstat(handle)
    if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
        os.close(handle)
        raise IniError('single-link regular file required')
    with os.fdopen(handle, 'rb') as stream:
        data = stream.read(LIMIT + 1)
        after = os.fstat(stream.fileno())
    if len(data) > LIMIT or _signature(before) != _signature(after):
        raise IniError('snapshot oversized or changed during read')
    return data, _signature(after)


def read_snapshot(path):
    path = Path(path)
    with _directory(path.parent) as fd:
        return _read(fd, path.name)[0]


def _new_file(fd, name, data):
    handle = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=fd)
    with os.fdopen(handle, 'wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def _atomic(fd, name, data, expected=None):
    """Offline-only writer. Caller holds bundle lock; compare before replace."""
    temp = '.pending-' + uuid.uuid4().hex
    _new_file(fd, temp, data)
    try:
        if expected is not None:
            if _read(fd, name) != expected:
                raise IniError('intervening modification; refusing replacement')
        else:
            try:
                os.stat(name, dir_fd=fd, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise IniError('offline destination already exists')
        os.replace(temp, name, src_dir_fd=fd, dst_dir_fd=fd)
        os.fsync(fd)
        actual, signature = _read(fd, name)
        if actual != data:
            raise IniError('readback verification failed; retain backup')
        return signature
    finally:
        try:
            os.unlink(temp, dir_fd=fd)
        except FileNotFoundError:
            pass


def _hash(data):
    return hashlib.sha256(data).hexdigest()


def prepare(snapshot, output, changes):
    """Create a unique offline bundle; NEVER write the supplied snapshot.

    Failure leaves any created backup in place for diagnosis. original.bin is
    fsynced and verified before prepared.bin is atomically published. Receipt
    is published last; incomplete bundles cannot be restored by this tool.
    """
    original = read_snapshot(snapshot)
    candidate, _ = transform(original, changes)
    name = 'np21w-offline-' + uuid.uuid4().hex
    with _directory(output) as parent:
        os.mkdir(name, 0o700, dir_fd=parent)
        fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX)
            _new_file(fd, 'original.bin', original)
            os.fsync(fd)
            if _read(fd, 'original.bin')[0] != original:
                raise IniError('backup readback failed')
            signature = _atomic(fd, 'prepared.bin', candidate)
            receipt = {'format': 1, 'changes': changes, 'original': _hash(original),
                       'prepared': _hash(candidate), 'signature': signature}
            _atomic(fd, 'receipt.json', json.dumps(receipt, sort_keys=True).encode('ascii'))
            os.fsync(parent)
        finally:
            os.close(fd)
    return Path(output).absolute() / name


def restore(bundle, apply=False):
    """Restore ONLY prepared.bin in an offline bundle; default is field diff.

    Validate hashes, exact prepared file identity/timestamps and pure transform
    provenance. No receipt-specified paths or shell commands are accepted.
    This is not a live restore API or a stopped-emulator assertion.
    """
    with _directory(bundle) as fd:
        fcntl.flock(fd, fcntl.LOCK_EX)
        try:
            receipt = json.loads(_read(fd, 'receipt.json')[0])
            if set(receipt) != {'format', 'changes', 'original', 'prepared', 'signature'} or receipt['format'] != 1:
                raise IniError('invalid offline receipt')
            original, _ = _read(fd, 'original.bin')
            current, signature = _read(fd, 'prepared.bin')
            expected, diff = transform(original, receipt['changes'])
            if (current != expected or _hash(original) != receipt['original'] or
                    _hash(current) != receipt['prepared'] or signature != receipt['signature']):
                raise IniError('intervening modification; refusing restore')
        except (KeyError, TypeError, ValueError, UnicodeError) as exc:
            raise IniError('invalid or modified offline bundle; refusing restore') from exc
        reverse = []
        for line in diff:
            key, values = line.split(': ')
            old, new = values.split(' -> ')
            reverse.append(f'{key}: {new} -> {old}')
        if apply:
            _atomic(fd, 'prepared.bin', original, (current, signature))
        return reverse


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prep = sub.add_parser('prepare', help='dry-run an offline snapshot; never writes input')
    prep.add_argument('snapshot')
    prep.add_argument('--set', action='append', required=True, dest='settings', metavar='KEY=VALUE')
    prep.add_argument('--output', help='existing parent directory for a NEW offline bundle')
    prep.add_argument('--apply', action='store_true', help='create offline bundle (requires --output)')
    rest = sub.add_parser('restore', help='restore prepared.bin inside an offline bundle only')
    rest.add_argument('bundle')
    rest.add_argument('--apply', action='store_true')
    args = parser.parse_args(argv)
    try:
        if args.command == 'restore':
            diff = restore(args.bundle, args.apply)
        else:
            if args.apply and not args.output:
                raise IniError('live apply blocked: trustworthy stopped-emulator verifier unavailable; use --output for offline preparation')
            changes = {}
            for setting in args.settings:
                key, sep, value = setting.partition('=')
                if not sep or key in changes:
                    raise IniError('invalid or repeated assignment')
                extension = ALLOWED_PATHS.get(key)
                if extension:
                    # CLI が受けるのは NP21W_DIR 直下の**名前だけ**。絶対パスを
                    # そのまま通すと存在・通常ファイル・配置先の検査を迂回できた
                    # (往復 2 の blocker)。展開点は resolve_image() ひとつに保つ。
                    value = resolve_image(value, extension)[1]
                changes[key] = value
            _changes(changes)
            if args.apply:
                bundle = prepare(args.snapshot, args.output, changes)
                _, diff = transform(read_snapshot(bundle / 'original.bin'), changes)
                print('offline bundle: ' + str(bundle))
            else:
                _, diff = transform(read_snapshot(args.snapshot), changes)
        for line in diff:
            print(line)
        return 0
    except IniError as exc:
        print('error: ' + str(exc), file=sys.stderr)
        return 2
    except OSError:
        print('error: offline file operation failed; retain any backup', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
