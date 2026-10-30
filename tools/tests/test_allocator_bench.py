#!/usr/bin/env python3
"""F-9 observer/format tests. Host clock/backing are NOT performance evidence."""
from kernel_includes import include_flags
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
HOST_PASS = 'HOST F9 retained-list/owner/exec-teardown PASS'


def rows(output):
    result = []
    for line in output.splitlines():
        if line == HOST_PASS:
            continue
        match = ROW.fullmatch(line)
        assert match, line
        values = match.groups()
        result.append(dict(zip(KEYS, [*values[:2], *map(int, values[2:])])))
    return result


def teardown(source):
    start = source.index('static void exec_teardown_app(')
    return source[start:source.index('\n}', start) + 2]


def mutations(cross, tmp, cc, objects, archive, runner):
    """Compile/link errors and unrelated failures never count as detection."""
    source = (ROOT / 'sdk/allocator/nano_adapter.c').read_text()
    variants = (
        ('lost-large-list', '*link = b;\n        r->_errno = ENOMEM;',
         'r->_errno = ENOMEM;', 'holes',
         'FAIL backend_unmaps == before + 1 && full_checks == failures + 1', None),
        ('lost-ENOMEM', '*link = b;\n        r->_errno = ENOMEM;',
         '*link = b;', 'holes', None, 'efull'),
        ('realloc-no-copy', 'memcpy(p, old, old_size < size ? old_size : size);',
         '(void)old_size;', 'realloc', None, 'realloc'),
    )
    for name, old, new, case, failure, phase in variants:
        assert source.count(old) == (3 if name == 'realloc-no-copy' else 1), name
        changed = tmp / (name + '.c')
        changed.write_text(source.replace(old, new))
        library = load_builder().build(cross, tmp / name, adapter=changed, reuse=archive)
        exe = tmp / (name + '.elf')
        host32.build(['gcc', '-m32', '-nostdlib', '-static', '-no-pie',
                      '-Wl,--build-id=none', *objects, str(library), '-o', str(exe)], check=True)
        run = host32.run([str(exe), case], runner=runner, capture_output=True,
                         text=True, timeout=host32.RUN_TIMEOUT)
        assert run.returncode == 1, (name, run)
        if failure:
            assert run.stdout.splitlines()[-1] == failure, (name, run)
        else:
            data = rows(run.stdout)
            # The first failing phase identifies the violated contract.
            assert next(r['phase'] for r in data if not r['ok']) == phase, (name, run)
        print('RED runtime: ' + name)

    source = teardown((ROOT / 'exec/exec.c').read_text())
    old = 'e->kind == APPMEM_ANON || e->kind == APPMEM_EXEC_ARENA || e->kind == APPMEM_EXEC_LARGE'
    assert source.count(old) == 1
    (tmp / 'allocator_teardown.inc').write_text(source.replace(old, 'e->kind == APPMEM_EXEC_ARENA || e->kind == APPMEM_EXEC_LARGE'))
    obj = tmp / 'lost-anon.o'
    host32.build([*cc, '-c', str(ROOT / 'tools/tests/allocator_bench_host.c'), '-o', str(obj)], check=True)
    exe = tmp / 'lost-anon.elf'
    host32.build(['gcc', '-m32', '-nostdlib', '-static', '-no-pie', '-Wl,--build-id=none',
                  str(obj), objects[1], str(archive), '-o', str(exe)], check=True)
    for case in ('holes', 'realloc'):
        run = host32.run([str(exe), case], runner=runner, capture_output=True,
                         text=True, timeout=host32.RUN_TIMEOUT)
        assert run.returncode == 1 and run.stdout.splitlines()[-1] == 'FAIL a->as && exiting && !leftover', run
        print('RED runtime: exit omitted ANON after ' + case)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'])
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ['CROSS_DIR'])
    with tempfile.TemporaryDirectory(prefix='f9-') as directory:
        tmp = Path(directory)
        (tmp / 'allocator_teardown.inc').write_text(teardown((ROOT / 'exec/exec.c').read_text()))
        archive = load_builder().build(cross, tmp / 'nano')
        cc = [str(cross / 'bin/i386-elf-gcc'), '-std=gnu11', '-O2', '-ffreestanding',
              '-fno-builtin', '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra',
              '-Werror', '-Werror=vla', '-I' + str(tmp), *(['-I' + str(ROOT / p) for p in ('sdk/allocator',)] + include_flags(ROOT))]
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
            assert run.stdout.splitlines()[-1] == HOST_PASS, run.stdout
            data = rows(run.stdout)
            assert all(row['ok'] == 1 for row in data), data
            assert data[-1]['phase'] == 'exit'
            assert all(r['extents'] + r['slots'] == 32 for r in data)
            if name.startswith('small'):
                n = int(name[5:])
                assert [r['phase'] for r in data] == ['alloc', 'free', 'trim', 'exit']
                alloc, free, trim, _ = data
                # A 64KiB mapping has a management page and fits fourteen
                # 4096-byte nano allocations (each has its own chunk header).
                secondary = (n + 13) // 14
                assert alloc['value'] == n and alloc['arenas'] == secondary + 1
                assert alloc['map'] == alloc['exact'] + secondary
                assert alloc['exact_fail'] == alloc['exact']
                assert alloc['map_fail'] == alloc['exact_fail']
                assert alloc['exact_fail'] < n * (n - 1) // 4
                assert free['arenas'] == trim['arenas'] == 1
                assert free['unmap'] == secondary
                assert alloc['owner'] - free['owner'] == 16 * secondary
                assert free['owner'] == trim['owner']
                assert all(r['us_lo'] == 64 and r['us_hi'] == 0 for r in data[:3])
                print(f"COUNTS {name}: arenas={secondary} map={alloc['map']} "
                      f"exact_fail={alloc['exact_fail']} owner={alloc['owner']} "
                      f"free_owner={free['owner']} (host policy counts, not timing)")
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
                    assert moved['owner'] == full['owner'] + 18
                    assert retained['owner'] == moved['owner']
            print(f'PASS {name}: real nano/extent policy, observer counters and v=1 rows')
            print(f'PASS {name}: retained list/independent owner model/real exec teardown walker')
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
        if args.mutate:
            mutations(cross, tmp, cc, objects, archive, args.runner)
    assert 'userland/tests/allocator_bench 76 0' in (ROOT / 'build/app.conf').read_text()
    assert 'host: userland/tests/allocator_bench.bin' in (ROOT / 'userland/deploy.yaml').read_text()
    print('PASS deployment registration')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
