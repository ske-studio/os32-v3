"""T2f f2: real unlinked appmem extent/search preparation (ILP32).
記録: tools/tests/appmem_tdd.md
Compile/link errors, signals, timeouts and unexpected FAIL labels are ERROR.
"""
import argparse
import hashlib
import pathlib
import statistics
import subprocess
import tempfile
import time

import host32
from mutpar import run_ordered

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SRCS = ['exec/appmem.c', 'exec/appmem.h', 'include/types.h',
               'include/memmap.h', 'kernel/paging.h',
               'tools/tests/appmem_host.c', 'tools/tests/test_appmem.py',
               'tools/tests/host32.py', 'tools/tests/mutpar.py']
MUTANTS = [
    ('unknown-flag', '(map_flags & ~(APPMEM_MAP_EXACT | APPMEM_MAP_TOPDOWN))', '0', 'unknown flag'),
    ('unaligned-hint', '!aligned(hint) || hint < MEM_EXEC_LOAD_ADDR', 'hint < MEM_EXEC_LOAD_ADDR', 'unaligned'),
    ('hint-floor', 'hint < MEM_EXEC_LOAD_ADDR', 'hint < MEM_SHLIB_BASE', 'outside'),
    ('exact-fallback', 'if (!base && (map_flags & APPMEM_MAP_EXACT)) return APPMEM_ENOVA;',
     'if (0) return APPMEM_ENOVA;', 'exact collision'),
    ('hint-overwrite', 'range_free(table, count, hint, end)) base = hint;',
     '(range_free(table, count, hint, end) || 1)) base = hint;', 'exact collision'),
    ('flags0-bottom', 'return high - low >= size ? high - size : 0;',
     'return high - low >= size ? low : 0;', 'flags0 preserves break'),
    ('merge-kind', 'a->kind == b->kind && a->flags == b->flags',
     'a->flags == b->flags', 'distinct extents'),
    ('merge-flags', 'a->kind == b->kind && a->flags == b->flags',
     'a->kind == b->kind', 'distinct extents'),
    ('merge-large', 'a->kind != APPMEM_EXEC_LARGE;', '1;', 'distinct extents'),
    ('full-changes-table', 'if (plan.count + 1 - plan.remove_count > APPMEM_EXTENT_MAX) return APPMEM_EFULL;',
     'if (plan.count + 1 - plan.remove_count > APPMEM_EXTENT_MAX) { ((struct appmem_table *)table)->e[0].end = 0; return APPMEM_EFULL; }',
     'full table unchanged'),
    ('round-overflow', 'bytes > APPMEM_U32_MAX - (PAGE_SIZE - 1)',
     '0', 'round overflow'),
    ('hint-overflow', 'size > APPMEM_U32_MAX - hint', '0', 'hint addition wrap'),
    ('exact-topdown', 'if (hint) {',
     'if (hint && !(map_flags & APPMEM_MAP_TOPDOWN)) {', 'lower hint topdown'),
    ('merge-before-full', 'plan.count + 1 - plan.remove_count > APPMEM_EXTENT_MAX',
     'plan.count + 1 > APPMEM_EXTENT_MAX', 'full left merge prepare'),
    ('topdown-floor', 'layout->exec_heap_cur_end, layout->guard_b, size);',
     'MEM_EXEC_HEAP_BASE, layout->guard_b, size);', 'upper short'),
    ('publish-shift', 'table->e[i] = table->e[i + plan->remove_count - 1];',
     'table->e[i] = table->e[i];', 'full bridge sorted tail'),
    ('same-count-plan', 'expected.first == plan->first && expected.remove_count == plan->remove_count &&',
     '1 &&', 'same count insertion invalid'),
    ('unmap-stale', 'extent_equal(&expected.left, &plan->left) && extent_equal(&expected.right, &plan->right);',
     '(extent_equal(&expected.left, &plan->left) || 1) && (extent_equal(&expected.right, &plan->right) || 1);', 'unmap stale metadata invalid'),

    ('unmap-fragment-flags', 'plan.left = (struct appmem_extent){e->base, base, e->kind, e->flags};',
     'plan.left = (struct appmem_extent){e->base, base, e->kind, 0};', 'unmap metadata preserved'),

]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'], default='native')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    before = {path: hashlib.sha256((ROOT / path).read_bytes()).digest() for path in TARGET_SRCS}
    body = (ROOT / 'exec/appmem.c').read_text()
    cc = ['gcc', '-std=gnu11', '-m32', '-march=i386', '-ffreestanding', '-fno-builtin',
          '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror',
          '-Werror=implicit-function-declaration', '-Werror=implicit-int', '-Werror=vla',
          *['-I' + str(ROOT / path) for path in ('include', 'kernel', 'exec')]]
    try:
        with tempfile.TemporaryDirectory(prefix='os32-f2-') as directory:
            tmp = pathlib.Path(directory)
            subprocess.run(cc + ['-c', str(ROOT / 'tools/tests/appmem_host.c'),
                                 '-o', str(tmp / 'fixture.o')], check=True,
                           capture_output=True, text=True, timeout=30)

            def run(source, key):
                src, obj, exe = [tmp / (key + ext) for ext in ('.c', '.o', '.elf')]
                src.write_text(source)
                subprocess.run(cc + ['-c', str(src), '-o', str(obj)], check=True,
                               capture_output=True, text=True, timeout=30)
                subprocess.run(['gcc', '-m32', '-nostdlib', '-static', '-no-pie',
                                str(tmp / 'fixture.o'), str(obj), '-o', str(exe)],
                               check=True, capture_output=True, text=True, timeout=30)
                return host32.run([str(exe)], runner=args.runner,
                                  capture_output=True, text=True, timeout=10)

            result = run(body, 'normal')
            print(result.stdout + result.stderr, end='')
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            print(f'PASS runner={args.runner}')
            if args.mutate:
                def one(entry):
                    index, (name, old, new, expected) = entry
                    start = time.monotonic()
                    try:
                        assert body.count(old) == 1, (name, body.count(old))
                        r = run(body.replace(old, new), f'mut{index}')
                    except subprocess.CalledProcessError as error:
                        return 'COMPILE_LINK_ERROR', name, time.monotonic() - start, (error.stdout or '') + (error.stderr or '')
                    except subprocess.TimeoutExpired as error:
                        return 'TIMEOUT', name, time.monotonic() - start, str(error)
                    except AssertionError as error:
                        return 'ERROR', name, time.monotonic() - start, str(error)
                    status = ('RED' if r.returncode == 1 and f'FAIL: {expected}\n' in r.stdout
                              else 'SURVIVED' if r.returncode == 0
                              else 'SIGNAL' if r.returncode < 0 else 'ERROR')
                    return status, name, time.monotonic() - start, r.stdout + r.stderr

                results = list(run_ordered(one, list(enumerate(MUTANTS))))
                for status, name, seconds, output in results:
                    print(f'{status} (runtime): {name} ({seconds:.2f}s)')
                    if status != 'RED': print(output)
                times = [r[2] for r in results]
                counts = {status: sum(r[0] == status for r in results)
                          for status in ('RED', 'SURVIVED', 'COMPILE_LINK_ERROR', 'TIMEOUT', 'SIGNAL', 'ERROR')}
                print(f'MUTATIONS {counts}; median={statistics.median(times):.2f}s max={max(times):.2f}s')
                assert counts['RED'] == len(MUTANTS) and max(times) < 30, counts
    finally:
        assert all(hashlib.sha256((ROOT / path).read_bytes()).digest() == digest
                   for path, digest in before.items()), 'input changed during test'


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise
