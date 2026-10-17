#!/usr/bin/env python3
"""T4a-4 bounded worst-load memory gate; bundled ILP32 SQLite, no host timings.

FEP_BUDGET_IPADIC optionally supplies the real CSV distribution. The default
uses deterministic adversarial S/M/L fixtures, explicitly labelled synthetic.
Python SQLite builds disposable input files only; all measurements run the
bundled SQLite and real kernel IME under the selected host32 runner.
"""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/tests/fep_budget_host.c'
RESERVE = 192 * 1024
LEARN_ROWS = 8192


def inputs(tmp, ipadic):
    files = []
    if ipadic:
        spec = importlib.util.spec_from_file_location('fep_builder', ROOT / 'tools/fep_to_sqlite.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        entries = builder.normalize_costs(builder.load_ipadic(str(ipadic)))
        for name, definition in builder.SIZE_DEFS.items():
            path = tmp / f'{name}.db'
            builder.create_db(entries, str(path), definition['cost_limit'])
            files.append((name, path))
    else:
        # Full 31-byte readings, long values, tall trees, exact/prefix fanout
        # far beyond the 32-candidate output cap. These are workload bounds,
        # not a replacement for the real distribution's S/M/L size definitions.
        for name, count in zip('SML', (4096, 16384, 65536)):
            path = tmp / f'{name}.db'
            with sqlite3.connect(path) as db:
                db.execute('PRAGMA page_size=1024')
                db.execute('CREATE TABLE dict(yomi TEXT,kanji TEXT,pos_id INT,cost INT)')
                db.executemany('INSERT INTO dict VALUES(?,?,?,?)', (
                    ('あい' if n % 2 else 'あいう' + 'え' * 7 + 'a',
                     '亜' * 7 + f'{n:09d}a', n % 2048, n % 2048)
                    for n in range(count)))
                db.execute('CREATE INDEX idx_dict_yomi ON dict(yomi)')
            files.append((name, path))
    result = []
    for name, path in files:
        with sqlite3.connect(path) as db:
            count = db.execute('SELECT count(*) FROM dict').fetchone()[0]
            exact, exact_n = db.execute(
                'SELECT yomi,count(*) n FROM dict WHERE length(yomi)<=2 '
                'GROUP BY yomi ORDER BY n DESC,yomi LIMIT 1').fetchone()
            prefix, prefix_n = db.execute(
                'SELECT substr(yomi,1,3) k,count(*) n FROM dict WHERE length(yomi)>=3 '
                'GROUP BY k ORDER BY n DESC,k LIMIT 1').fetchone()
            db.executescript('CREATE TABLE dict_user(yomi TEXT NOT NULL,kanji TEXT NOT NULL,'
                             'freq INTEGER DEFAULT 1,last_ts INTEGER DEFAULT 0,PRIMARY KEY(yomi,kanji));'
                             'CREATE INDEX idx_user_yomi ON dict_user(yomi);')
            db.executemany('INSERT INTO dict_user VALUES(?,?,?,?)', (
                (prefix + f'{i:08d}', '亜' * 7 + f'{i:09d}a', 1, i)
                for i in range(LEARN_ROWS)))
        print(f'INPUT kind={"ipadic" if ipadic else "synthetic"} variant={name} '
              f'rows={count} learn_rows={LEARN_ROWS} bytes={path.stat().st_size} '
              f'exact_hits={exact_n} prefix_hits={prefix_n} '
              f'sha256={hashlib.sha256(path.read_bytes()).hexdigest()}', flush=True)
        result.extend((str(path), exact, prefix))
    return result


def build(tmp, source, sqlite_obj, max_file, name):
    shim = tmp / 'shim'
    shim.mkdir(exist_ok=True)
    (shim / 'memmap.h').write_text(
        'extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n'
        '#define MEM_SHM_DB_OFFSET 0UL\n'
        '#define MEM_SHM_DB_BASE (MEM_SHM_BASE + MEM_SHM_DB_OFFSET)\n')
    exe = tmp / name
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    host32.build([cc, '-m32', '-nostartfiles', '-std=gnu11', '-O2',
                  '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-Wno-unused-function',
                  '-Wno-type-limits', '-Wno-unused-parameter', '-Wno-sign-compare',
                  '-Wno-missing-field-initializers', '-D__cdecl=',
                  '-DFIXTURE_FILES=8', f'-DFIXTURE_BYTES={max_file + 131072}',
                  '-I' + str(shim),
                  *['-I' + str(ROOT / p) for p in ('include', 'fs', 'drivers',
                      'sdk/include/os32', 'lib', 'lib/sqlite3', 'kernel', 'tools/tests')],
                  str(source), str(ROOT / 'tools/tests/fep_budget_linux.c'),
                  str(sqlite_obj), '-lm', '-o', str(exe)], check=True)
    return exe


def run(exe, data, runner):
    result = host32.run([str(exe), str(RESERVE), *data], runner=runner,
                        capture_output=True, text=True, timeout=120)
    print(result.stdout, end='', flush=True)
    if result.stderr:
        print(result.stderr, end='', flush=True)
    return result


def validate(output):
    profiles = re.findall(r'^PROFILE variant=([SML]) ', output, re.M)
    assert profiles == list('SML'), profiles
    switches = re.findall(r'^SWITCH mode=(\d) from=([SML]) to=([SML]) ', output, re.M)
    assert len(switches) == 27 and len(set(switches)) == 27, switches
    assert len(re.findall(r'^RESTART .* unchanged=1$', output, re.M)) == 9
    summary = re.search(r'^BUDGET .*result=PASS$', output, re.M)
    assert summary, output
    values = dict(re.findall(r'(\w+)=(\d+)', summary.group()))
    assert int(values['bytes']) == 512 * 1024
    assert int(values['reserve']) == RESERVE
    assert int(values['denied']) > 0
    assert int(values['common_measured']) <= RESERVE


def capture_contract():
    spec = importlib.util.spec_from_file_location('capture', ROOT / 'tools/fep_budget_capture.py')
    collector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(collector)
    mem = ('diag: sbrk_used=1032 sbrk_peak=2048 sbrk_capacity=294428 '
           'exec_peak=131656 exec_capacity=262144')
    budget = (f'BUDGET bytes=524288 reserve={RESERVE} denied=1 common_measured=90048 result=PASS\n' +
              '\n'.join(f'SWITCH mode={m} from={a} to={b}'
                        for m in range(3) for a in 'SML' for b in 'SML'))
    user = '\n'.join(f'F9 v=1 case={case} phase={phase} bytes=4096 us_hi=0 us_lo=1 ok=1'
                     for case in sorted(collector.CASES) for phase in
                     ('alloc', 'free', 'trim', 'efull', 'data', 'realloc', 'retained', 'exit', 'reclaim'))
    user += '\nF9 v=1 phase=result ok=1'
    assert len(collector.capture(budget, user, mem, mem)['switches']) == 27
    for bad in (user.replace('ok=1', 'ok=0', 1), user.replace('phase=reclaim', 'phase=missing'),
                user.replace('phase=exit', 'phase=missing'),
                user.replace('phase=alloc', 'phase=missing'),
                user.replace('us_lo=1', 'missing=1'), ''):
        try:
            collector.capture(budget, bad, mem, mem)
        except AssertionError:
            pass
        else:
            raise AssertionError('capture accepted failed/incomplete USER evidence')
    for bad in (mem.replace('sbrk_peak=2048', 'sbrk_peak=294428'),
                mem.replace('exec_peak=131656', 'exec_peak=262144'), ''):
        try:
            collector.capture(budget, user, mem, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError('capture accepted absent/over-capacity resident evidence')
    print('PASS capture parser controls (scripted format tests, not guest evidence)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--ipadic', type=Path, default=os.environ.get('FEP_BUDGET_IPADIC'))
    args = parser.parse_args()
    build_id = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--short', 'HEAD'],
                              check=True, capture_output=True, text=True).stdout.strip()
    print(f'RUN stage=T4a-4 build={build_id} runner={host32.selected_runner(args.runner)} '
          f'abi=ILP32 sqlite_flags=Os,NDEBUG '
          f'fixture_sha256={hashlib.sha256(SOURCE.read_bytes()).hexdigest()}', flush=True)
    capture_contract()
    with tempfile.TemporaryDirectory(prefix='fep-budget-') as directory:
        tmp = Path(directory)
        data = inputs(tmp, args.ipadic)
        max_file = max(Path(data[i]).stat().st_size for i in range(0, len(data), 3))
        obj = tmp / 'sqlite.o'
        cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
        host32.build([cc, '-m32', '-march=i386', '-std=gnu89', '-Os',
                      '-DNDEBUG', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                      '-fsigned-char', '-fno-short-enums', '-include',
                      str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-c',
                      str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', str(obj)], check=True)
        exe = build(tmp, SOURCE, obj, max_file, 'budget')
        result = run(exe, data, args.runner)
        assert result.returncode == 0, result.returncode
        validate(result.stdout)
        if args.mutate:
            for macro, marker in (('OMIT_APP_GATE', 'denied > before'),
                                  ('OMIT_SWITCH_GATE', 'pending == 0')):
                source = tmp / f'{macro}.c'
                source.write_text(f'#define {macro} 1\n#include "{SOURCE}"\n')
                mutant = build(tmp, source, obj, max_file, macro)
                red = run(mutant, data, args.runner)
                assert red.returncode == 1 and marker in red.stderr, (macro, red)
                print(f'RED runtime {macro}: {marker}', flush=True)
    print('PASS T4a-4 memory counts; guest timing/USER/resident measurements are separate')


if __name__ == '__main__':
    main()
