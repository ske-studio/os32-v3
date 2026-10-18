"""T4b-7: raw waits/normal controls and specifically attributed runtime RED.

Reuse the real driver/wrapper ILP32 fixture, actual SQLite stack/guard and R1
exec landings. Device IRQ/input and CPU transitions are their explicit seams;
this check does not claim to execute the NP21/W guest acceptance matrix.
"""
import argparse
import importlib.util
import os
import pathlib
import re
import sqlite3
import subprocess
import tempfile

import host32
import test_exec_r1 as lifecycle
import test_kapi_audit as audit
import test_sqlite_engine as engine

# Each mutation must reach this assertion, rather than just fail somewhere.
CASES = {
    'kbd char wait': 'FAIL input CUI abort',
    'kbd key wait': 'FAIL input CUI abort',
    'kbd gui wait': 'FAIL input GUI fallback STOP',
    'ime char cleanup': 'FAIL IME char STOP cancels composition',
    'ime key cleanup': 'FAIL IME key STOP cancels composition',
    'serial receive probe': 'FAIL serial abort',
    'mml wait probe': 'FAIL mml wait abort',
    'mml key off': 'FAIL mml sustained stop key off',
    'shared TX abort regression': 'FAIL USER internal logs and frames never abort',
    'shared puts abort regression': 'FAIL USER internal logs and frames never abort',
}


def guest_inputs(tmp):
    """Use the real OMIT parser and the distribution builder, not SQL mocks."""
    root = pathlib.Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('fep_builder', root / 'tools/fep_to_sqlite.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    entries = builder.normalize_costs(builder.load_ipadic(str(root / 'assets/ipadic')))
    cc = str(pathlib.Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    obj = tmp / 'sqlite.o'
    host32.build([cc, '-m32', '-std=gnu89', '-Os', '-fno-pie',
                  '-include', str(root / 'lib/sqlite3/os32_sqlite_config.h'),
                  '-c', str(root / 'lib/sqlite3/sqlite3.c'), '-o', str(obj)], check=True)
    original = (root / 'userland/tests/raw_wait_test.c').read_text()

    def run(source):
        definitions = source[source.index('#define RAW_USER_BYTES'):source.index('static int parent_run')]
        yomi = re.search(r'^#define RAW_IME_YOMI "([^"]+)"', definitions, re.M)[1]
        # Normalize the whole source before filtering: costs use global bounds.
        # Build only the chosen reading, with the same filter/dedup as fep.db.
        path = tmp / 'reading.db'
        builder.create_db([e for e in entries if e[0] == yomi], str(path),
                          builder.SIZE_DEFS['M']['cost_limit'])
        with sqlite3.connect(path) as db:
            count = db.execute('SELECT count(*) FROM dict WHERE yomi=?', (yomi,)).fetchone()[0]
        (tmp / 'guest_inputs.inc').write_text(definitions + f'\n#define RAW_DICT_CANDIDATES {count}\n')
        exe = tmp / 'sql-test'
        host32.build([cc, '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                      '-nostartfiles', '-fno-pie', '-static',
                      '-include', str(root / 'lib/sqlite3/os32_sqlite_config.h'),
                      '-I' + str(tmp), '-I' + str(root / 'lib/sqlite3'),
                      '-I' + str(root / 'sdk/include/os32'),
                      str(root / 'tools/tests/raw_wait_sql_host.c'),
                      str(root / 'tools/tests/fep_budget_linux.c'), str(obj), '-lm',
                      '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True, text=True, timeout=120)
        print(result.stdout + result.stderr, end='', flush=True)
        return result

    result = run(original)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    return original, run


def guest_fixture(mutation=None):
    root = pathlib.Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='raw-wait-guest-') as directory:
        tmp = pathlib.Path(directory)
        source = (root / 'userland/tests/raw_wait_test.c').read_text()
        if mutation == 'skip FEP warmup':
            source = source.replace(
                'if ((!strcmp(entry, "imechar") || !strcmp(entry, "imekey")) && !strcmp(end, "stop"))',
                'if (0)')
        elif mutation == 'combined SQL':
            source = source.replace('"CREATE TABLE probe(x)"',
                                    '"CREATE TABLE probe(x); INSERT INTO probe VALUES(73)"')
        elif mutation == 'ignore INSERT error':
            source = source.replace('api->db_exec(db, RAW_DB_INSERT) < 0',
                                    '(api->db_exec(db, RAW_DB_INSERT), 0)')
        elif mutation == 'skip log warmup':
            source = source.replace('if (!strcmp(entry, "log") && api->ime_switch_dict(RAW_DICT_VARIANT) < 0)',
                                    'if (0)')
        elif mutation == 'ignore log warmup error':
            source = source.replace('api->ime_switch_dict(RAW_DICT_VARIANT) < 0',
                                    '(api->ime_switch_dict(RAW_DICT_VARIANT), 0)')
        (tmp / 'guest_source.inc').write_text(source.replace('#include <string.h>', ''))
        exe = tmp / 'test'
        subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                        '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                        '-I' + str(tmp), '-I' + str(root / 'sdk/include/os32'),
                        '-I' + str(root / 'include'), '-I' + str(root / 'lib'),
                        str(root / 'tools/tests/raw_wait_guest_host.c'),
                        str(root / 'lib/kstring.c'), str(root / 'lib/kstring_c.c'),
                        '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True, text=True, timeout=30)
        print(result.stdout + result.stderr, end='', flush=True)
        if mutation:
            expected = {
                'skip FEP warmup': 'FAIL guest fixture: raw_guest_main(4,argv,&table)==(leak ? 1 : 0)',
                'combined SQL': 'FAIL guest fixture: raw_guest_main(3,argv,&table)==0 && saw_pass==1 && !bad',
                'ignore INSERT error': 'FAIL guest fixture: raw_guest_main(3,argv,&table)==1 && !reads && sql_calls==failure',
                'skip log warmup': 'FAIL guest fixture: raw_guest_main(4,argv,&table)==(leak ? 1 : 0)',
                'ignore log warmup error': 'FAIL guest fixture: raw_guest_main(4,log,&table)==1 && !stats && !children',
            }[mutation]
            assert result.returncode == 1 and expected in result.stdout, (
                mutation, result.returncode, result.stdout, result.stderr)
            print(f'RED runtime: guest {mutation}', flush=True)
        else:
            assert result.returncode == 0, result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='raw-wait-inputs-') as directory:
        original, run = guest_inputs(pathlib.Path(directory))
        if args.mutate:
            variants = [
                ('oversized SQL', re.sub(r'([a-k]), ', r'\1 CROSS JOIN ', original),
                 'FAIL guest inputs: sizeof(RAW_SLOW_SQL) <= DB_SQL_MAX_BYTES'),
                ('CTE SQL', original.replace('"SELECT sum(a.x+b.x+c.x+d.x+e.x+f.x+g.x+h.x+i.x+j.x+k.x+l.x) FROM "',
                                            '"WITH n AS (SELECT 1) SELECT * FROM n, "'),
                 'FAIL guest inputs: sqlite3_prepare_v2(db, RAW_SLOW_SQL, -1, &stmt, &tail) == SQLITE_OK'),
                ('short SQL', original.replace('"(SELECT 1 AS x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4)"',
                                              '"(SELECT 1 AS x)"'),
                 'FAIL guest inputs: sqlite3_column_int64(stmt, 0) == 503316480'),
                ('absent reading', original.replace('#define RAW_IME_YOMI "か"', '#define RAW_IME_YOMI "あ"'),
                 'FAIL guest inputs: RAW_DICT_CANDIDATES > 0'),
            ]
            for name, source, expected in variants:
                assert source != original, name
                result = run(source)
                assert result.returncode == 1 and expected in result.stdout, (
                    name, result.returncode, result.stdout, result.stderr)
                print(f'RED runtime: guest {name} (compiled)', flush=True)
    guest_fixture()
    audit.run('qemu')
    result = engine.run('qemu')
    print(result.stdout + result.stderr, end='', flush=True)
    assert result.returncode == 0, result.returncode
    status, log = lifecycle.run()
    assert status == 'pass', (status, log)
    print('PASS raw waits: real exec cleanup once / parent esp0+caller / next KAPI / USER heap')
    if args.mutate:
        for mutation in ('skip FEP warmup', 'combined SQL', 'ignore INSERT error',
                         'skip log warmup', 'ignore log warmup error'):
            guest_fixture(mutation)
        for name, expected in CASES.items():
            mutation = next(m for m in audit.MUTATIONS if m[0] == name)
            audit.run('qemu', mutation, expected=expected)
        result = engine.run('qemu', 'early STOP')
        assert result.returncode == 1 and 'FAIL: !sqlite_engine_active && !kills' in result.stdout, (
            result.returncode, result.stdout, result.stderr)
        print('RED runtime: SQLite STOP before leave')
        print(f'PASS {len(CASES) + 10} raw-wait mutations: named runtime RED, compile failures 0')


if __name__ == '__main__':
    main()
