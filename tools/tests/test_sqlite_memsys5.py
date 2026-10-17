#!/usr/bin/env python3
"""T4a-3: bundled MEMSYS5, real hooks, ILP32 tag comparison; no guest I/O.

MUTATE=0 runs controls only. --mutate builds disposable prototype copies;
compile errors, signals and timeouts are never counted as mutation detection.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'tools/tests'
PROTO = HERE / 'sqlite_memsys5_proto.c'
CASES = [(case, mode) for mode in (0, 1)
         for case in ('hooks', 'realloc', 'exhaust', 'sql')] + [('capacity', 1)]


def build(tmp, source=PROTO, name='control', sqlite=None):
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    if sqlite is None:
        sqlite = tmp / 'sqlite.o'
        host32.build([cc, '-m32', '-std=gnu89', '-Os', '-fno-pie',
                      '-include', str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'),
                      '-c', str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', str(sqlite)],
                     check=True)
    exe = tmp / name
    host32.build([cc, '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-Werror=vla', '-fno-pie', '-no-pie', '-static',
                  '-nostartfiles', '-Wl,-e,_start',
                  '-I' + str(HERE), '-I' + str(ROOT / 'lib/sqlite3'),
                  str(source), str(HERE / 'sqlite_memsys5_host.c'),
                  str(HERE / 'sqlite_memsys5_start.S'),
                  str(HERE / 'sqlite_memsys5_linux.c'), str(sqlite),
                  '-o', str(exe)], check=True)
    return exe, sqlite


def run(exe, case, mode, runner):
    result = host32.run([str(exe), case, str(mode)], runner=runner,
                        capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)
    print(f'RUN case={case} mode={mode} runner={host32.selected_runner(runner)} '
          f'rc={result.returncode}', flush=True)
    print(result.stdout, end='', flush=True)
    if result.stderr:
        print(result.stderr, end='', flush=True)
    return result


def mutations(tmp, sqlite, runner):
    original = PROTO.read_text()
    # Each expected failure is an explicit fixture verdict after a normal build.
    variants = [
        ('lost-rounded-charge', 'return state.underlying.xSize(base);', '(void)base; return 1;',
         'hooks', 0, 'p5_stats().used[P5_FEP] == start.used[P5_FEP] + expected'),
        ('lost-tag-release', 'find_tag(p)->ptr = 0;', 'find_tag(p)->owner = 0;',
         'capacity', 1, 'blocks[i]'),
        ('lost-realloc-retention', 'if (!next) return 0;',
         'if (!next) { account(owner, -old_size); return 0; }',
         'realloc', 1, 'a.used[i] == b.used[i]'),
        ('lost-table-precheck', 'state.stats.table_denials++;\n            return 0;',
         'state.stats.table_denials++;\n            (void)state.underlying.xMalloc(raw);\n'
         '            state.stats.malloc_calls++;\n            return 0;',
         'capacity', 1, 'rejected.malloc_calls == full.malloc_calls'),
        ('lost-tag-budget', 'rc = sqlite3_config(SQLITE_CONFIG_MALLOC, &wrapper);',
         'state.stats.heap_bytes += state.stats.tag_bytes;\n'
         '    rc = sqlite3_config(SQLITE_CONFIG_MALLOC, &wrapper);',
         'hooks', 1, 'start.heap_bytes + start.tag_bytes + start.state_bytes == P5_BUDGET'),
        ('lost-allocation-context', 'return tag->owner;', 'return P5_APP;',
         'realloc', 1, 'p5_stats().used[P5_FEP] - start.used[P5_FEP] == charge(mode, 4096)'),
    ]
    for name, old, new, case, mode, failure in variants:
        assert original.count(old) == 1, name
        source = tmp / (name + '.c')
        source.write_text(original.replace(old, new))
        exe, _ = build(tmp, source, name, sqlite)
        result = run(exe, case, mode, runner)
        assert result.returncode == 1, (name, result)
        assert any(line.startswith('FAIL ') and line.endswith(' ' + failure)
                   for line in result.stdout.splitlines()), (name, result.stdout)
        print(f'RED runtime {name}: {failure}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='memsys5-') as directory:
        tmp = Path(directory)
        exe, sqlite = build(tmp)
        for case, mode in CASES:
            result = run(exe, case, mode, args.runner)
            assert result.returncode == 0, (case, mode, result)
            assert result.stdout.splitlines()[-1] == (
                'PASS real MEMSYS5 hooks/accounting; remaining=0'), result.stdout
        print(f'GREEN {len(CASES)} controls', flush=True)
        if args.mutate:
            mutations(tmp, sqlite, args.runner)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
