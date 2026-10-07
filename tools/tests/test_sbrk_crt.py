#!/usr/bin/env python3
"""f6 real USER/resident CRT; only named runtime assertions count as RED.
票: docs/tasks/v3/TASK_T2D_T2H.md §3-5 f6
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
import importlib.util

ROOT = Path(__file__).resolve().parents[2]
CRT = 'sdk/crt/syscalls.c'
ADAPTER = 'sdk/allocator/nano_adapter.c'
MUTANTS = [
    (ADAPTER, 'if (kapi->sbrk_heap_limit < initial) goto fail;', 'if (0) goto fail;',
     'invalid handoff rejected', True),
    (ADAPTER, 'if (decrease > old - a->initial)', 'if (0)', 'negative lower bound', False),
    (ADAPTER, 'if ((uintptr_t)incr > UINTPTR_MAX - old)', 'if (0)', 'addition overflow', False),
    (ADAPTER, '== (void *)base ? 0 : -1', '!= NULL ? 0 : -1', 'noncontiguous map refused', False),
    (ADAPTER, 'if (a->grow_exact(a->opaque, a->mapped_end, end - a->mapped_end) != 0) goto fail;',
     'if (a->grow_exact(a->opaque, a->mapped_end, end - a->mapped_end) != 0) { a->brk = next; goto fail; }',
     'failed map unchanged', False),
    (ADAPTER, 'primary_arena.grow_exact = NULL;', 'primary_arena.grow_exact = crt_grow_exact;',
     'resident no map beyond backing', True),
]


def run(runner, resident=False, mutant=None):
    with tempfile.TemporaryDirectory(prefix='os32-sbrk-crt-') as td:
        tree = Path(td)
        for name in (CRT, ADAPTER, 'sdk/allocator/nano_adapter.h', 'userland/lib/rt/ls.c'):
            source = (ROOT / name).read_text()
            if mutant and name == mutant[0]:
                assert source.count(mutant[1]) == 1, mutant
                source = source.replace(mutant[1], mutant[2])
                if resident and 'crt_grow_exact' in mutant[2]:
                    source = source.replace('#ifndef OS32_CRT_RESIDENT\nstatic int crt_grow_exact', '#if 1\nstatic int crt_grow_exact')
                    source = source.replace('static void *crt_map', '__attribute__((unused)) static void *crt_map').replace('static int crt_unmap', '__attribute__((unused)) static int crt_unmap')
            path = tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
        gccinc = subprocess.check_output(['gcc', '-m32', '-print-file-name=include'], text=True).strip()
        flags = ['-std=gnu11', '-m32', '-march=i386', '-ffreestanding', '-fno-builtin',
                 '-fno-pie', '-fno-stack-protector', '-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=0', '-nostdinc', '-isystem', gccinc,
                 '-isystem', str(cross / 'i386-elf/include'), '-I' + str(tree),
                 '-I' + str(ROOT / 'sdk/include/os32'), '-O1', '-Wall', '-Wextra',
                 '-Werror', '-Werror=vla', '-Wno-unused-parameter', '-D_end=crt_fixture_end',
                 '-ffunction-sections', '-fdata-sections']
        if resident:
            flags.append('-DOS32_CRT_RESIDENT')
        spec = importlib.util.spec_from_file_location('nano_builder', ROOT / 'sdk/allocator/build_nano.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        archive = builder.build(cross, tree / 'nano')
        # Only the six nano members are needed; the actual adapter is included
        # with syscalls in this fixture, so the archive adapter stays unselected.
        exe = tree / 'fixture'
        # Unused syscall functions are discarded; _sbrk and its exact dependency
        # closure still compile from the actual source with newlib headers.
        subprocess.run(['gcc', *flags, '-nostdlib', '-static', '-no-pie',
                        '-Wl,--gc-sections', str(ROOT / 'tools/tests/sbrk_crt_host.c'),
                        str(archive), '-o', str(exe)], check=True, text=True)
        result = host32.run([str(exe)], runner=runner, capture_output=True, text=True, timeout=60)
        if mutant:
            assert result.returncode == 1 and result.stdout == 'FAIL: ' + mutant[3] + '\n', result
            print('RED runtime: ' + mutant[3])
        else:
            assert result.returncode == 0 and 'PASS CRT morecore' in result.stdout, result
            print(f'PASS {"resident" if resident else "USER"} runner={runner}')


def check_build_routes():
    # Inspect make's parsed database rather than reimplementing its includes
    # and variable expansion. -q/-p never executes build or deploy recipes.
    result = subprocess.run(['make', '-qp'], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode in (0, 1), result.stderr
    rules = result.stdout
    assert 'CRT0_OBJ = $(CRT0_USER_OBJ)' in rules
    for target in ('userland/shell.elf:', 'userland/gshell.elf:'):
        stanza = rules.split('\n' + target, 1)[1].split('\n\n', 1)[0]
        assert 'sdk/crt/resident/syscalls.o' in stanza, target
        assert '$(CRT0_RESIDENT_OBJ)' in stanza, target
    user = rules.split('\nuserland/sh.elf:', 1)[1].split('\n\n', 1)[0]
    assert 'sdk/crt/syscalls.o' in user and 'sdk/crt/resident/syscalls.o' not in user
    assert '$(CRT0_OBJ)' in user
    sdk = rules.split('\nsdk:', 1)[1].split('\n\n', 1)[0]
    assert '$(CRT0_USER_OBJ)' in sdk and '$(CRT0_RESIDENT_OBJ)' not in sdk
    print('PASS shell/gshell resident, sh/SDK USER link routes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'], default=host32.selected_runner())
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    check_build_routes()
    run(args.runner)
    run(args.runner, resident=True)
    if args.mutate:
        for mutant in MUTANTS:
            run(args.runner, resident=mutant[4], mutant=mutant)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
