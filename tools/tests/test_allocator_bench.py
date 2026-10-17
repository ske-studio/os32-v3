#!/usr/bin/env python3
"""F-9 observer/format tests. Host clock/backing are NOT performance evidence."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]


def load_builder():
    spec = importlib.util.spec_from_file_location('nano_builder', ROOT / 'sdk/allocator/build_nano.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ROW = re.compile(
    r'^F9 v=1 case=(\w+) phase=(\w+) '
    r'n=(\d+) bytes=(\d+) arenas=(\d+) large=(\d+) map=(\d+) exact=(\d+) '
    r'exact_fail=(\d+) map_fail=(\d+) unmap=(\d+) unmap_full=(\d+) '
    r'extents=(\d+) slots=(\d+) owner=(\d+) errno=(-?\d+) '
    r'us_hi=(\d+) us_lo=(\d+) value=(\d+) ok=([01])$')
KEYS = 'case phase n bytes arenas large map exact exact_fail map_fail unmap unmap_full extents slots owner errno us_hi us_lo value ok'.split()


def rows(output):
    result = []
    for line in output.splitlines():
        match = ROW.fullmatch(line)
        assert match, line
        values = match.groups()
        result.append(dict(zip(KEYS, [*values[:2], *map(int, values[2:])])))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'])
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ['CROSS_DIR'])
    with tempfile.TemporaryDirectory(prefix='f9-') as directory:
        tmp = Path(directory)
        archive = load_builder().build(cross, tmp / 'nano')
        cc = [str(cross / 'bin/i386-elf-gcc'), '-std=gnu11', '-O2', '-ffreestanding',
              '-fno-builtin', '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra',
              '-Werror', '-Werror=vla', *['-I' + str(ROOT / p) for p in
              ('include', 'kernel', 'exec', 'sdk/include/os32', 'sdk/allocator')]]
        objects = []
        for source in ('tools/tests/allocator_bench_host.c', 'exec/appmem.c'):
            obj = tmp / (Path(source).stem + '.o')
            host32.build([*cc, '-c', str(ROOT / source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        exe = tmp / 'fixture'
        host32.build(['gcc', '-m32', '-nostdlib', '-static', '-no-pie',
                      '-Wl,--build-id=none', *objects, str(archive), '-o', str(exe)], check=True)
        for name in ('small16', 'small64', 'small256', 'threshold', 'holes', 'realloc'):
            run = host32.run([str(exe), name], runner=args.runner, capture_output=True,
                             text=True, timeout=host32.RUN_TIMEOUT)
            assert run.returncode == 0, (name, run.returncode, run.stdout, run.stderr)
            data = rows(run.stdout)
            assert all(row['ok'] == 1 for row in data), data
            assert data[-1]['phase'] == 'exit'
            assert all(r['extents'] + r['slots'] == 32 for r in data)
            if name.startswith('small'):
                n = int(name[5:])
                assert [r['phase'] for r in data] == ['alloc', 'free', 'trim', 'exit']
                alloc, free, trim, _ = data
                assert alloc['value'] == n and alloc['arenas'] == n + 1
                assert alloc['map'] == alloc['exact'] + n
                assert alloc['exact_fail'] >= n
                assert free['arenas'] == trim['arenas'] == 1
                assert free['unmap'] == n
                assert all(r['us_lo'] == 64 and r['us_hi'] == 0 for r in data[:3])
            elif name == 'threshold':
                assert [r['bytes'] for r in data[:-1]] == [65535, 65535, 65536, 65536, 65537, 65537]
                assert [r['large'] for r in data[:-1]] == [0, 0, 1, 0, 1, 0]
            else:
                full = next(r for r in data if r['phase'] == 'efull')
                assert full['slots'] == 0 and full['unmap_full'] == 1 and full['errno'] == 12
                assert full['large'] == 70 - (full['value'] - 1) // 2
                if name == 'realloc':
                    moved = next(r for r in data if r['phase'] == 'realloc')
                    retained = next(r for r in data if r['phase'] == 'retained')
                    assert moved['large'] == full['large'] + 1 == moved['value']
                    assert moved['unmap_full'] == 2 and retained['unmap_full'] == 3
                    assert moved['errno'] == retained['errno'] == 12
                    assert moved['owner'] > full['owner']
            print(f'PASS {name}: real nano/extent policy, observer counters and v=1 rows')
        reclaim_re = re.compile(
            r'^F9 v=1 case=(small16|small64|small256|threshold|holes|realloc) phase=reclaim '
            r'rc=(-?\d+) kind=(\d+) code=(-?\d+) pool_before=(\d+) pool_after=(\d+) '
            r'owner_before=(\d+) owner_after=(\d+) leftover_before=(\d+) leftover_after=(\d+) ok=([01])$')
        for mode in ('parent-ok', 'parent-leak', 'parent-leftover', 'parent-no-reclaim', 'parent-failure'):
            run = host32.run([str(exe), mode], runner=args.runner, capture_output=True,
                             text=True, timeout=host32.RUN_TIMEOUT)
            passed = mode == 'parent-ok'
            assert run.returncode == (0 if passed else 1), run
            lines = run.stdout.splitlines()
            assert len(lines) == 7 and all(reclaim_re.fullmatch(line) for line in lines[:-1]), run.stdout
            assert lines[-1] == f'F9 v=1 phase=result ok={int(passed)}', run.stdout
            assert all(line.endswith('ok=1') for line in lines[:-1]) if passed else any(
                line.endswith('ok=0') for line in lines[:-1])
            print(f'PASS {mode}: parent reclaim verdict and output shape (scripted MemStat)')
        run = host32.run([str(exe), 'small16', 'no-clock'], runner=args.runner,
                         capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)
        assert run.returncode == 1 and all(r['ok'] == 0 for r in rows(run.stdout)), run
        print('PASS unavailable clock: explicit failure, no substitute timing')
    assert 'userland/tests/allocator_bench 76 0' in (ROOT / 'build/app.conf').read_text()
    assert 'host: userland/tests/allocator_bench.bin' in (ROOT / 'userland/deploy.yaml').read_text()
    print('PASS deployment registration')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
