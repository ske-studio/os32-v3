#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""assets/settings/defaults.tsv -> settings.db (SQLite) の生成。

設定レジストリの初期値の正典は tsv (人が読み書きする側) で、この道具は
それを媒体 (FDD / CD) に載せる DB へ落とすだけ。DB を作るのはビルドと
`cfg init` (S2、ゲスト内) だけで、通常配備は tsv を配る。

    python3 tools/mk_settings_db.py --tsv assets/settings/defaults.tsv \
                                    --out build/out/settings.db [--epoch N]

出力は決定的: 同じ tsv の内容 + 同じ epoch なら同じバイト列になる。
入力ファイルの mtime は一切見ない (`meta.created` は --epoch →
SOURCE_DATE_EPOCH → 0 の順で決めた時刻)。

規則違反 (列数、scope / key / type、int32 の範囲、UTF-8、NUL、長さ、
blob の hex、CR、重複) は非ゼロ終了で理由を出す。仕様は
docs/archive/settings/TASK_S0.md §3 と DESIGN.md §3。
"""

import argparse
import os
import re
import sqlite3
import sys
import time

SCHEMA_VERSION = 1
PAGE_SIZE = 1024

TYPE_INT = 0
TYPE_TEXT = 1
TYPE_BLOB = 2

MAX_SCOPE_BYTES = 63
MAX_KEY_BYTES = 63
MAX_TEXT_BYTES = 255
MAX_BLOB_BYTES = 4096
MAX_BLOB_HEX = MAX_BLOB_BYTES * 2

INT32_MIN = -2147483648
INT32_MAX = 2147483647
INT32_MAX_DIGITS = 10          # 先頭ゼロを畳んだ後の桁数の上限

FIXED_SCOPES = ('system', 'gshell', 'user')

RE_APP_NAME = re.compile(r'^[a-z0-9_]+$')
RE_KEY = re.compile(r'^[a-z0-9_]+(/[a-z0-9_]+)*$')
RE_INT = re.compile(r'^-?[0-9]+$')
RE_HEX = re.compile(r'^[0-9a-fA-F]*$')

TYPE_NAMES = {'int': TYPE_INT, 'text': TYPE_TEXT, 'blob': TYPE_BLOB}


class TsvError(Exception):
    """tsv の規則違反。path:line: reason の形で報告する。"""

    def __init__(self, path, lineno, reason):
        Exception.__init__(self, reason)
        self.path = path
        self.lineno = lineno
        self.reason = reason

    def __str__(self):
        return '%s:%d: %s' % (self.path, self.lineno, self.reason)


def _check_scope(path, lineno, scope):
    if '\x00' in scope:
        raise TsvError(path, lineno, 'scope に NUL が入っている')
    n = len(scope.encode('utf-8'))
    if n == 0:
        raise TsvError(path, lineno, 'scope が空')
    if n > MAX_SCOPE_BYTES:
        raise TsvError(path, lineno,
                       'scope が %dB (上限 %dB)' % (n, MAX_SCOPE_BYTES))
    if scope in FIXED_SCOPES:
        return
    if scope.startswith('app:'):
        name = scope[4:]
        if not RE_APP_NAME.match(name):
            raise TsvError(path, lineno,
                           "app:<name> の name が [a-z0-9_]+ でない: %r" % name)
        return
    raise TsvError(path, lineno,
                   'scope は system / gshell / user / app:<name> のいずれか: %r'
                   % scope)


def _check_key(path, lineno, key):
    if '\x00' in key:
        raise TsvError(path, lineno, 'key に NUL が入っている')
    n = len(key.encode('utf-8'))
    if n == 0:
        raise TsvError(path, lineno, 'key が空')
    if n > MAX_KEY_BYTES:
        raise TsvError(path, lineno,
                       'key が %dB (上限 %dB)' % (n, MAX_KEY_BYTES))
    if not RE_KEY.match(key):
        raise TsvError(path, lineno,
                       'key が [a-z0-9_]+(/[a-z0-9_]+)* でない: %r' % key)


def _parse_value(path, lineno, type_name, value):
    """(type_code, ival, tval, bval) を返す。"""
    if type_name == 'int':
        if not RE_INT.match(value):
            raise TsvError(path, lineno,
                           'int の字句が -?[0-9]+ でない: %r' % value)
        # 先頭ゼロは字句規則上 有効。Python の既定の桁数制限 (4300 桁) で
        # int() が ValueError を投げないよう、**変換の前に**桁を畳む。
        # 畳んだ後が 11 桁以上なら int32 には入らない (上限は 10 桁)。
        sign = '-' if value[0] == '-' else ''
        digits = (value[1:] if sign else value).lstrip('0') or '0'
        if len(digits) > INT32_MAX_DIGITS:
            raise TsvError(path, lineno,
                           'int が int32 の範囲外: %s' % value)
        iv = int(sign + digits, 10)
        if iv < INT32_MIN or iv > INT32_MAX:
            raise TsvError(path, lineno,
                           'int が int32 の範囲外: %s' % value)
        return (TYPE_INT, iv, None, None)

    if type_name == 'text':
        if '\x00' in value:
            raise TsvError(path, lineno, 'text に NUL が入っている')
        n = len(value.encode('utf-8'))
        if n > MAX_TEXT_BYTES:
            raise TsvError(path, lineno,
                           'text が %dB (上限 %dB)' % (n, MAX_TEXT_BYTES))
        return (TYPE_TEXT, None, value, None)

    if type_name == 'blob':
        if len(value) % 2 != 0:
            raise TsvError(path, lineno,
                           'blob の hex が奇数桁: %d 文字' % len(value))
        if not RE_HEX.match(value):
            raise TsvError(path, lineno,
                           'blob は hex (空白や hex 以外の文字を含まない): %r'
                           % value)
        if len(value) > MAX_BLOB_HEX:
            raise TsvError(path, lineno,
                           'blob が %d 文字 (上限 %d 文字 = %dB)'
                           % (len(value), MAX_BLOB_HEX, MAX_BLOB_BYTES))
        return (TYPE_BLOB, None, None, bytes(bytearray.fromhex(value)))

    raise TsvError(path, lineno,
                   'type は int / text / blob のいずれか: %r' % type_name)


def parse_tsv(path):
    """tsv を読んで [(scope, key, type_code, ival, tval, bval), ...] を返す。

    規則違反は TsvError。行全体を strip しないので、末尾の空欄は
    「空の text」として残る。
    """
    with open(path, 'rb') as f:
        raw = f.read()

    if b'\r' in raw:
        lineno = raw.split(b'\r', 1)[0].count(b'\n') + 1
        raise TsvError(path, lineno, 'CR が含まれている (改行は LF のみ)')

    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError as e:
        lineno = raw[:e.start].count(b'\n') + 1
        raise TsvError(path, lineno,
                       'UTF-8 として妥当でない (offset %d)' % e.start)

    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines.pop()          # 末尾の改行

    rows = []
    seen = {}
    for i, line in enumerate(lines):
        lineno = i + 1
        if line == '' or line.startswith('#'):
            continue

        fields = line.split('\t')
        if len(fields) != 4:
            raise TsvError(path, lineno,
                           '列が %d 個 (タブ区切りの厳密 4 列)' % len(fields))

        scope, key, type_name, value = fields
        _check_scope(path, lineno, scope)
        _check_key(path, lineno, key)
        tcode, ival, tval, bval = _parse_value(path, lineno, type_name, value)

        if (scope, key) in seen:
            raise TsvError(path, lineno,
                           '(scope, key) が %d 行目と重複: %s / %s'
                           % (seen[(scope, key)], scope, key))
        seen[(scope, key)] = lineno
        rows.append((scope, key, tcode, ival, tval, bval))

    return rows


def resolve_epoch(arg_epoch):
    """--epoch → SOURCE_DATE_EPOCH → 0。mtime は使わない。"""
    if arg_epoch is not None:
        return int(arg_epoch)
    env = os.environ.get('SOURCE_DATE_EPOCH')
    if env:
        try:
            return int(env, 10)
        except ValueError:
            raise ValueError('SOURCE_DATE_EPOCH が整数でない: %r' % env)
    return 0


def format_created(epoch):
    return time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(epoch))


def write_db(rows, out_path, epoch, schema_version=SCHEMA_VERSION):
    """rows を DESIGN §3 のスキーマで書き出す (決定的)。"""
    for suffix in ('', '-journal', '-wal', '-shm'):
        p = out_path + suffix
        if os.path.exists(p):
            os.remove(p)

    out_dir = os.path.dirname(out_path)
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    conn = sqlite3.connect(out_path, isolation_level=None)
    try:
        cur = conn.cursor()
        cur.execute('PRAGMA page_size=%d' % PAGE_SIZE)
        cur.execute('PRAGMA journal_mode=DELETE')
        cur.execute('BEGIN')
        cur.execute('CREATE TABLE meta ('
                    'schema_version INTEGER NOT NULL, '
                    'created TEXT)')
        cur.execute('CREATE TABLE settings ('
                    'scope TEXT NOT NULL, '
                    'key TEXT NOT NULL, '
                    'type INTEGER NOT NULL, '
                    'ival INTEGER, '
                    'tval TEXT, '
                    'bval BLOB, '
                    'PRIMARY KEY (scope, key)) WITHOUT ROWID')
        cur.execute('INSERT INTO meta (schema_version, created) VALUES (?, ?)',
                    (schema_version, format_created(epoch)))
        # 書き込み順を (scope, key) で固定する = 同じ内容なら同じページ像。
        for scope, key, tcode, ival, tval, bval in sorted(
                rows, key=lambda r: (r[0], r[1])):
            cur.execute('INSERT INTO settings '
                        '(scope, key, type, ival, tval, bval) '
                        'VALUES (?, ?, ?, ?, ?, ?)',
                        (scope, key, tcode, ival, tval,
                         sqlite3.Binary(bval) if bval is not None else None))
        cur.execute('COMMIT')
        # VACUUM で自由ページと挿入順の痕跡を落とす (user_version は
        # VACUUM をまたいで保たれるが、念のため後で入れ直す)。
        cur.execute('VACUUM')
        cur.execute('PRAGMA user_version=%d' % schema_version)
        cur.close()
    finally:
        conn.close()

    # DELETE ジャーナルは commit 後に消えているはずだが、残骸は残さない。
    journal = out_path + '-journal'
    if os.path.exists(journal) and os.path.getsize(journal) == 0:
        os.remove(journal)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='settings.db (初期値マスタ) を tsv から生成する')
    parser.add_argument('--tsv', required=True, help='入力 tsv')
    parser.add_argument('--out', required=True, help='出力 SQLite ファイル')
    parser.add_argument('--epoch', type=int, default=None,
                        help='meta.created に使う UNIX 時刻 '
                             '(既定: SOURCE_DATE_EPOCH、無ければ 0)')
    parser.add_argument('--schema-version', type=int, default=SCHEMA_VERSION,
                        help='meta.schema_version に入れる版 (既定 %d)。'
                             '**試験 fixture 専用** (S4 の VERSION 受入 G5): '
                             '媒体マスタは既定のまま' % SCHEMA_VERSION)
    args = parser.parse_args(argv)

    try:
        epoch = resolve_epoch(args.epoch)
    except ValueError as e:
        sys.stderr.write('mk_settings_db: %s\n' % e)
        return 1

    try:
        rows = parse_tsv(args.tsv)
    except TsvError as e:
        sys.stderr.write('mk_settings_db: %s\n' % e)
        return 1
    except IOError as e:
        sys.stderr.write('mk_settings_db: %s\n' % e)
        return 1

    write_db(rows, args.out, epoch, args.schema_version)
    sys.stdout.write('mk_settings_db: %s -> %s (%d 件, created=%s)\n'
                     % (args.tsv, args.out, len(rows), format_created(epoch)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
