#!/usr/bin/env python3
"""Prepare guest SQL with the bundled amalgamation and unmodified product config.

Extract adjacent C literals, including comments between them. Product sources
are scanned recursively, so new SQL in existing/new applications is covered.
Application SQL is prepared and finalized without stepping; schema DDL alone
is stepped to resolve tables and subsequent statements in schema batches.
"""
CHECK = dict(name='sqlite-product-sql', tier=1, needs=[], budget=21.2,
    covers=['include/diag_stack.h', 'arch/*/arch_diag_stack.h', 'tools/tests/test_sqlite_product_sql.py', 'tools/tests/sqlite_product_host.c',
            'tools/tests/hsync_sqlite_product_host.c', 'sdk/include/os32/**',
            'lib/sqlite3/os32_sqlite_vfs.c', 'tools/mk_settings_db.py',
            'assets/settings/defaults.tsv',
            'lib/sqlite3/sqlite3.c', 'lib/sqlite3/sqlite3.h',
            'lib/sqlite3/os32_sqlite_config.h', 'userland/**',
            'kernel/sqlite_engine.c', 'kernel/ime/ime_*.c', 'kapi/kapi_db.c'])
import argparse
import ast
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOKEN = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[^\s]', re.S)
START = re.compile(r'^(?:SELECT\s|INSERT\s+(?:INTO|OR)\s|REPLACE\s+INTO\s|UPDATE\s+\w+\s+SET\s|DELETE\s+FROM\s|CREATE\s+(?:TABLE|INDEX|TRIGGER|VIRTUAL)\s|DROP\s+TABLE\s|PRAGMA\s|BEGIN(?:\s|$)|COMMIT$|ROLLBACK$|WITH\s|ALTER\s|ATTACH\s|ANALYZE(?:\s|$)|REINDEX(?:\s|$)|EXPLAIN\s)')
ANCHOR = 'length(hex(tval))/2'


def literals(source):
    parts, line = [], 0
    for token in TOKEN.finditer(source):
        value = token.group()
        if value.startswith(('/*', '//')):
            continue
        if value.startswith('"'):
            if not parts:
                line = source.count('\n', 0, token.start()) + 1
            parts.append(ast.literal_eval(value))
        else:
            if parts:
                yield line, ''.join(parts)
                parts = []
    if parts:
        yield line, ''.join(parts)


def collect(mutant=None):
    paths = sorted(set(ROOT.glob('userland/**/*.[ch]')) |
                   set(ROOT.glob('kernel/ime/ime_*.c')) |
                   {ROOT / 'kernel/sqlite_engine.c', ROOT / 'kapi/kapi_db.c'})
    rows = []
    for path in paths:
        source = path.read_text()
        if path.name == 'raw_wait_test.c':
            # Expand its string-only SQL macros (line numbers stay diagnostic).
            definitions = {}
            for match in re.finditer(r'^#define (RAW_(?:DB_CREATE|DB_INSERT|SQL_FOUR|SQL_TWO|SLOW_SQL)) (.*(?:\\\n.*)*)', source, re.M):
                value = match[2].replace('\\\n', '')
                for name, replacement in definitions.items():
                    value = value.replace(name, replacement)
                definitions[match[1]] = value
            source = re.sub(r'^#define RAW_(?:DB_CREATE|DB_INSERT|SQL_FOUR|SQL_TWO|SLOW_SQL) .*?(?<!\\)\n',
                            lambda m: '\n' * m[0].count('\n'), source, flags=re.M | re.S)
            for name, value in reversed(list(definitions.items())):
                source = re.sub(r'\b' + name + r'\b', lambda m: value, source)
        if mutant == 'hsync' and path == ROOT / 'userland/system/hsync.c':
            assert source.count(ANCHOR) == 1
            source = source.replace(ANCHOR, 'length(CAST(tval AS BLOB))')
        if mutant == 'fep-meta' and path == ROOT / 'userland/tests/fep_meta_test.c':
            anchor = "SELECT ''||ival FROM settings"
            assert source.count(anchor) == 1
            source = source.replace(anchor, 'SELECT CAST(ival AS TEXT) FROM settings')
        if mutant == 'fep-meta-value' and path == ROOT / 'userland/tests/fep_meta_test.c':
            anchor = "SELECT value FROM meta WHERE key='%s'"
            assert source.count(anchor) == 1
            source = source.replace(anchor, "SELECT CAST(value AS TEXT) FROM meta WHERE key='%s'")
        for line, sql in literals(source):
            if START.match(sql):
                if 'tests' in path.relative_to(ROOT).parts and sql.startswith('SELECT '):
                    # Diagnostic labels such as SELECT (3 rows) are not SQL.
                    if ' FROM ' not in sql and not re.match(r'^SELECT [1-9?]', sql):
                        continue
                if path.name == 'fep_meta_test.c' and '%s' in sql:
                    assert sql.endswith(" FROM meta WHERE key='%s'") and sql.count('%s') == 1, sql
                    sql = sql.replace('%s', 'license')
                # The only printf SQL templates in these product sources.
                if sql.startswith('PRAGMA ') and '%' in sql:
                    assert re.fullmatch(r'PRAGMA (?:page_size|cache_size)=%u', sql), (path, line, sql)
                    sql = sql.replace('%u', '1024')
                rows.append((str(path.relative_to(ROOT)), line, sql))
    assert any(ANCHOR in s or 'CAST(tval AS BLOB)' in s for _, _, s in rows)
    required = {'userland/system/hsync.c', 'userland/lib/cfg/cfg_init.c',
                'userland/lib/cfg/libos32cfg.c', 'userland/lib/cfg/cfg_enum.c',
                'kernel/sqlite_engine.c', 'kernel/ime/ime_dict.c',
                'kernel/ime/ime_dict_info.c', 'kernel/ime/ime_user_store.c',
                'kapi/kapi_db.c', 'userland/tests/db_v50_test.c',
                'userland/tests/db_test.c', 'userland/tests/dbq.c',
                'userland/tests/raw_wait_test.c', 'userland/tests/fep_meta_test.c'}
    assert required <= {p for p, _, _ in rows}
    return rows


class Engine:
    def __init__(self, lib):
        self.lib = lib
        for name, args, result in [
            ('sqlite3_open', [C.c_char_p, C.POINTER(C.c_void_p)], C.c_int),
            ('sqlite3_close', [C.c_void_p], C.c_int),
            ('sqlite3_prepare_v2', [C.c_void_p, C.c_char_p, C.c_int, C.POINTER(C.c_void_p), C.POINTER(C.c_char_p)], C.c_int),
            ('sqlite3_finalize', [C.c_void_p], C.c_int),
            ('sqlite3_step', [C.c_void_p], C.c_int),
            ('sqlite3_column_count', [C.c_void_p], C.c_int),
            ('sqlite3_errmsg', [C.c_void_p], C.c_char_p),
            ('product_open_copy', [C.c_void_p, C.c_int, C.POINTER(C.c_void_p)], C.c_int),
        ]:
            fn = getattr(lib, name)
            fn.argtypes, fn.restype = args, result
        assert lib.product_init() == 0

    def open(self):
        db = C.c_void_p()
        assert self.lib.sqlite3_open(b':memory:', C.byref(db)) == 0
        return db

    def prepare(self, db, sql, execute=False):
        # Follow the tail to cover every statement in sqlite3_exec strings.
        remaining = sql.encode()
        while remaining.strip():
            stmt, tail = C.c_void_p(), C.c_char_p()
            rc = self.lib.sqlite3_prepare_v2(db, remaining, -1, C.byref(stmt), C.byref(tail))
            if rc:
                error = self.lib.sqlite3_errmsg(db).decode()
                self.lib.sqlite3_finalize(stmt)
                raise AssertionError(f'prepare rc={rc}: {error}: {sql}')
            try:
                if execute and stmt:
                    assert self.lib.sqlite3_step(stmt) == 101, sql
                # Unknown/omitted PRAGMAs silently prepare an empty program.
                # user_version is an intentional, documented no-op in cfg_init.
                if sql.startswith('PRAGMA ') and '=' not in sql:
                    assert self.lib.sqlite3_column_count(stmt) > 0, sql
            finally:
                assert self.lib.sqlite3_finalize(stmt) == 0
            new = tail.value or b''
            assert len(new) < len(remaining)
            remaining = new


def audit_silent_omits(path, sql):
    # Several OMITs accept SQL as a no-op instead of returning a prepare error.
    config = (ROOT / 'lib/sqlite3/os32_sqlite_config.h').read_text()
    omitted = set(re.findall(r'^#define SQLITE_OMIT_(\w+)\s+1', config, re.M))
    unquoted = re.sub(r"'(?:''|[^'])*'", "''", sql)
    if 'FOREIGN_KEY' in omitted:
        assert not re.search(r'\b(?:REFERENCES|FOREIGN\s+KEY)\b', unquoted, re.I), sql
    if 'LOCALTIME' in omitted:
        assert not re.search(r"'localtime'", sql, re.I), sql
    if sql.startswith('PRAGMA '):
        pragma = re.match(r'PRAGMA (\w+)', sql)[1]
        if path == 'userland/lib/cfg/cfg_init.c' and sql == 'PRAGMA user_version=1':
            # cfg_host.c documents this historical no-op; meta is authoritative.
            return
        assert pragma in {'page_size', 'cache_size', 'journal_mode'}, sql


def check_sql(engine, rows):
    cfg_ddl = [s for p, _, s in rows if p.endswith('/cfg_init.c') and s.startswith('CREATE TABLE')]
    ime_ddl = [s for p, _, s in rows if p.endswith('/ime_user_store.c') and s.startswith('CREATE TABLE')]
    assert len(cfg_ddl) == 2 and len(ime_ddl) == 1
    for path, line, sql in rows:
        audit_silent_omits(path, sql)
        db = engine.open()
        try:
            ddl = ime_ddl if '/ime_' in path or sql == 'SELECT key,value FROM meta' else cfg_ddl
            if path.startswith('userland/tests/'):
                # Tests reuse table names with different schemas. Use the latest
                # preceding declaration, without executing the application SQL.
                tables = {}
                for p, l, declaration in rows:
                    if p == path and l <= line and declaration.startswith('CREATE TABLE '):
                        name = re.search(r'TABLE (?:IF NOT EXISTS )?(\w+)', declaration)[1]
                        tables[name] = declaration
                ddl = list(tables.values())
                if path.endswith('/dbq.c'):
                    ddl = ['CREATE TABLE enemies(id,name,stage,kind,max_hp,atk,def,spd,mag,elements,exp,gold,class_id)']
            if path.endswith('/fep_meta_test.c'):
                ddl = [s for s in cfg_ddl if 'CREATE TABLE settings' in s] + ime_ddl
            for schema in ddl:
                engine.prepare(db, schema, execute=True)
            if '/ime_' in path or path in ('kernel/sqlite_engine.c', 'userland/tests/fep_meta_test.c'):
                engine.prepare(db, 'CREATE TABLE dict(yomi TEXT,kanji TEXT,pos_id INTEGER,cost INTEGER)', execute=True)
            if not sql.startswith('CREATE '):
                try:
                    engine.prepare(db, sql)
                except AssertionError as err:
                    if path.startswith('userland/tests/') and sql in (
                        'SELECT * FROM no_such_table', 'INSERT INTO nonexistent VALUES(1)'):
                        assert 'no such table' in str(err), err
                        continue
                    raise AssertionError(f'{path}:{line}: {err}') from err
            else:
                # DDL must prepare against a fresh schema, before it exists.
                assert engine.lib.sqlite3_close(db) == 0
                db = engine.open()
                engine.prepare(db, sql, execute=True)
        finally:
            assert engine.lib.sqlite3_close(db) == 0
    print(f'GREEN product SQL: {len(rows)} strings, {len(set(p for p, _, _ in rows))} sources', flush=True)


def hsync_fixture(work, source):
    def function(name):
        start = re.search(r'^static (?:int|const char \*) ' + name + r'\(', source, re.M).start()
        return source[start:source.index('\n}', start) + 2]
    (work / 'hsync_sql_source.inc').write_text('\n'.join(
        function(n) for n in ('str_len', 'str_ncat', 'str_ncpy', 'register_fep_user_path')))
    start = source.index('    {\n        const char *stage;\n        int rc = register_fep_user_path')
    (work / 'hsync_sql_warning.inc').write_text(source[start:source.index('\n    }', start) + 6])
    # The same OMIT_ATTACH compatibility shim used by the product VFS.
    vfs = (ROOT / 'lib/sqlite3/os32_sqlite_vfs.c').read_text()
    start = vfs.index('int sqlite3DbIsNamed(')
    (work / 'attach_stub.c').write_text(vfs[start:vfs.index('\n}', start) + 2])


def check_settings(engine, work, evidence):
    # Stable portable fixture generated by the repository's normal DB builder.
    # --settings-copy additionally checks the supplied real-machine bytes.
    if evidence is None:
        evidence = work / 'settings.db'
        subprocess.run(['python3', '-B', str(ROOT / 'tools/mk_settings_db.py'),
                        '--tsv', str(ROOT / 'assets/settings/defaults.tsv'), '--out', str(evidence)], cwd=ROOT, check=True)
    contents = C.create_string_buffer(evidence.read_bytes())
    db = C.c_void_p()
    assert engine.lib.product_open_copy(contents, len(contents) - 1, C.byref(db)) == 0
    try:
        stmt = C.c_void_p()
        assert engine.lib.sqlite3_prepare_v2(db,
            b"SELECT 1 FROM settings WHERE scope='system' AND key='fep_user_dict_path'",
            -1, C.byref(stmt), None) == 0
        assert engine.lib.sqlite3_step(stmt) == 101, 'fixture must have no path row'
        assert engine.lib.sqlite3_finalize(stmt) == 0
        engine.lib.check_hsync_default.argtypes = [C.c_void_p]
        assert engine.lib.check_hsync_default(db) == 1, 'hsync must select default without warning'
        print('GREEN real product SQLite + hsync: default path, no warning: ' + str(evidence), flush=True)
    finally:
        assert engine.lib.sqlite3_close(db) == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--settings-copy', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='sqlite-product-') as td:
        work = Path(td)
        library = work / 'product.so'
        hsync_fixture(work, (ROOT / 'userland/system/hsync.c').read_text())
        subprocess.run(['gcc', '-shared', '-fPIC', '-Os', '-std=gnu89', '-include',
            str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-I' + str(ROOT / 'lib/sqlite3'),
            '-D__cdecl=', '-I' + str(ROOT / 'sdk/include'),
            '-I' + str(ROOT / 'userland/system'), '-I' + str(work),
            str(ROOT / 'lib/sqlite3/sqlite3.c'), str(ROOT / 'tools/tests/sqlite_product_host.c'),
            str(ROOT / 'tools/tests/hsync_sqlite_product_host.c'),
            str(work / 'attach_stub.c'), '-o', str(library)], check=True)
        engine = Engine(C.CDLL(str(library)))
        rows = collect()
        check_sql(engine, rows)
        check_settings(engine, work, args.settings_copy)
        if args.mutate:
            for mutant, path in [('hsync', 'userland/system/hsync.c'),
                                 ('fep-meta', 'userland/tests/fep_meta_test.c'),
                                 ('fep-meta-value', 'userland/tests/fep_meta_test.c')]:
                try:
                    check_sql(engine, collect(mutant))
                except AssertionError as err:
                    assert path + ':' in str(err) and 'syntax error' in str(err), err
                    print('RED runtime ' + mutant + ' CAST: ' + str(err), flush=True)
                else:
                    raise AssertionError(mutant + ' CAST mutant survived')



if __name__ == '__main__':
    main()


def run(ctx):
    argv = ['python3', '-B', 'tools/tests/test_sqlite_product_sql.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)
