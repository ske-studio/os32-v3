#!/usr/bin/env python3
"""Check generated system dictionary metadata against the SQLite contents."""
CHECK = dict(name='fep-generator-meta', tier=1, needs=[], budget=5,
             covers=['tools/fep_to_sqlite.py', 'assets/ipadic/COPYING',
                     'assets/joyo_kanji.txt', 'tools/tests/test_fep_generator_meta.py'])
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'tools/fep_to_sqlite.py'
MUTANTS = [
    ('version', 'PRAGMA user_version = 1', 'PRAGMA user_version = 2'),
    ('variant', "'variant': variant", "'variant': 'M'"),
    ('count', "'entry_count': str(final_count)", "'entry_count': str(final_count + 1)"),
    ('checksum', "return 'sha256:' + digest.hexdigest()", "return 'sha256:' + '0' * 64"),
    ('license', "'license': original", "'license': original[:1]"),
    ('attribution', "'attribution': original", "'attribution': original[:1]"),
    ('date', "'build_date': date", "'build_date': 'wrong'"),
]


def exercise(source, work):
    spec = importlib.util.spec_from_file_location('fep_builder', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original = (ROOT / 'assets/ipadic/COPYING').read_bytes().decode('latin-1')
    entries = [('あ', '亜', 2, 10), ('あ', '亜', 1, 10),
               ('い', '伊', 3, 900), ('う', '宇', 4, 1600)]
    for name in 'SML':
        path = work / (name + '.db')
        module.create_db(entries, str(path), module.SIZE_DEFS[name]['cost_limit'],
                         name, str(ROOT / 'assets/ipadic'))
        with sqlite3.connect(path) as db:
            meta = dict(db.execute('SELECT key, value FROM meta'))
            rows = db.execute('SELECT yomi,kanji,pos_id,cost FROM dict '
                              'ORDER BY yomi COLLATE BINARY, kanji COLLATE BINARY, pos_id, cost').fetchall()
            stream = bytearray()
            for yomi, kanji, pos, cost in rows:
                for value in (yomi, kanji):
                    data = value.encode('utf-8')
                    stream.extend(len(data).to_bytes(4, 'big'))
                    stream.extend(data)
                stream.extend(pos.to_bytes(4, 'big', signed=True))
                stream.extend(cost.to_bytes(4, 'big', signed=True))
            expected = {'format_version': '1', 'min_fep_version': '1',
                        'dict_id': 'ipadic-2.7.0', 'kind': 'system', 'language': 'ja',
                        'charset': 'UTF-8', 'variant': name, 'source': 'IPADIC',
                        'source_version': '2.7.0', 'license': original, 'attribution': original,
                        'build_tool': 'fep_to_sqlite.py/1', 'build_date': '2000-01-01T00:00:00Z',
                        'entry_count': str(len(rows)), 'pos_table_version': '1',
                        'cost_scale': 'os32-log2047-boost-v1', 'mem_reserve_kb': '192',
                        'cache_pages': '16', 'checksum': 'sha256:' + hashlib.sha256(stream).hexdigest()}
            assert meta == expected, (name, meta.keys() ^ expected.keys())
            assert db.execute('PRAGMA page_size').fetchone()[0] == 1024
            assert db.execute('PRAGMA user_version').fetchone()[0] == 1
            assert rows[0][2] == 1
            assert len(rows) == {'S': 1, 'M': 2, 'L': 3}[name]
    missing = work / 'missing'
    missing.mkdir()
    try:
        module.create_db(entries, str(work / 'missing.db'), 0, 'L', str(missing))
        raise AssertionError('missing COPYING accepted')
    except FileNotFoundError:
        pass
    (missing / 'COPYING').write_text('')
    try:
        module.create_db(entries, str(work / 'empty.db'), 0, 'L', str(missing))
        raise AssertionError('empty COPYING accepted')
    except ValueError:
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--source')
    args = parser.parse_args()
    os.environ['SOURCE_DATE_EPOCH'] = '946684800'
    with tempfile.TemporaryDirectory(prefix='fep-generator-') as tmp:
        work = Path(tmp)
        exercise(Path(args.source) if args.source else ROOT / PRODUCT, work)
        if args.mutate:
            code = (ROOT / PRODUCT).read_text()
            for name, before, after in MUTANTS:
                if code.count(before) != 1:
                    raise RuntimeError(f'{name}: anchor not unique')
                mutant = work / (name + '.py')
                mutant.write_text(code.replace(before, after))
                result = subprocess.run([sys.executable, '-B', __file__, '--source', str(mutant)],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                if result.returncode == 0 or b'AssertionError' not in result.stderr:
                    raise RuntimeError(f'{name}: runtime RED required: {result.stderr.decode()}')
                print('RED', name, flush=True)
    print('PASS generated metadata', flush=True)
    return 0


def run(ctx):
    args = [sys.executable, '-B', __file__]
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
