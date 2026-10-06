#!/usr/bin/env python3
"""T2f f1b real nano connection. Log: tools/tests/nano_adapter_tdd.md
票: docs/tasks/v3/TASK_T2D_T2H.md §3-5 f1b
"""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from mutpar import run_ordered
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'sdk/allocator/nano_adapter.c'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


builder = load('nano_builder', ROOT / 'sdk/allocator/build_nano.py')
gate = load('nano_link_gate', ROOT / 'sdk/allocator/check_link.py')


def command(prefix, tool, *args):
    result = host32.build([str(prefix / ('bin/i386-elf-' + tool)), *map(str, args)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result


def link(prefix, work, archive, extra=(), after=False):
    exe = work / 'fixture'
    mapfile = work / 'fixture.map'
    # host gcc is only the static Linux link driver; nano and the fixture use
    # the cross compiler's exact newlib ABI (struct _reent included).
    host32.build(['gcc', '-m32', '-nostdlib', '-static', '-no-pie',
                    '-Wl,--build-id=none,--allow-multiple-definition,-Map=' + str(mapfile),
                    str(work / 'host.o'), str(work / 'libc_a-reallocf.o'), *([] if after else map(str, extra)), str(archive),
                    *(map(str, extra) if after else []), '-o', str(exe)],
                   check=True, capture_output=True, text=True)
    return exe, mapfile


def build_host(prefix, work):
    (work / 'libc_a-reallocf.o').write_bytes(builder.inputs.command(
        prefix, 'ar', 'p', prefix / 'i386-elf/lib/libc.a', 'libc_a-reallocf.o'))
    command(prefix, 'gcc', '-std=gnu11', '-O2', '-ffreestanding', '-fno-builtin',
            '-fno-pie', '-Wall', '-Wextra', '-Werror', '-Werror=vla',
            '-I' + str(ROOT / 'sdk/allocator'), '-c', ROOT / 'tools/tests/nano_adapter_host.c',
            '-o', work / 'host.o')


def rejection(prefix, work, archive, text, checker=gate, after=False):
    probe = work / 'probe.c'
    probe.write_text(text)
    command(prefix, 'gcc', '-ffreestanding', '-fno-builtin', '-c', probe, '-o', work / 'probe.o')
    exe, mapfile = link(prefix, work, archive, [work / 'probe.o'], after=after)
    try:
        checker.validate_output(prefix, mapfile, exe)
    except ValueError:
        assert not exe.exists(), "rejected executable remains"
        return
    raise AssertionError('link gate accepted forbidden/duplicate provider')


MUTATIONS = [
    ('os32_private_current_mallinfo = selected->info;', 'os32_private_current_mallinfo.arena = 0;', 1, 'statistics restore'),
    ('selected->info = os32_private_current_mallinfo;', 'selected->info.arena = 0;', 1, 'statistics save'),
    ('if (decrease > old - a->initial)', 'if (0)', 1, 'negative lower bound'),
    ('if ((uintptr_t)incr > UINTPTR_MAX - old)', 'if (0)', 1, 'addition overflow'),
    ('if (end > a->limit)', 'if (0)', 1, 'rounded page limit'),
    ('if (busy) return 0;', 'if (0) return 0;', 1, 'selection during callback'),
    ('if (busy || selected == NULL)', 'if (selected == NULL)', 1, 'public reentry'),
    ('os32_private_free_list = selected->free_list;', 'os32_private_free_list = NULL;', 1, 'free list restore'),
    ('selected->free_list = os32_private_free_list;', 'selected->free_list = NULL;', 1, 'free list save'),
    ('os32_private_sbrk_start = selected->sbrk_start;', 'os32_private_sbrk_start = NULL;', 1, 'start restore'),
    ('selected->sbrk_start = os32_private_sbrk_start;', 'selected->sbrk_start = NULL;', 1, 'start save'),
    ('if (next > a->limit)', 'if (0)', 1, 'fixed limit'),
    ('if (a->grow_exact(a->opaque, a->mapped_end, end - a->mapped_end) != 0)',
     'if ((a->grow_exact(a->opaque, a->mapped_end, end - a->mapped_end), 0))', 1, 'map failure ignored'),
    ('a->mapped_end = end;', 'a->mapped_end = next;', 1, 'mapped page end'),
    ('a->brk = next;', 'a->brk = next + 4;', 1, 'noncontiguous break'),
    ('return (void *)old;', 'return (void *)(old + 4);', 2, 'noncontiguous success'),
    ('p = os32_private_calloc_r(r, n, size);', 'p = os32_private_malloc_r(r, n * size);', 1, 'calloc bypass'),
    ('p != NULL && ((uintptr_t)p < start || (uintptr_t)p >= selected->brk)',
     'p != NULL && (uintptr_t)p < start && (uintptr_t)p >= selected->brk', 1, 'pointer arena bounds'),
    ('next > UINTPTR_MAX - (OS32_NANO_PAGE - 1u)', '0', 1, 'page rounding overflow'),
    ('!a->grow_exact', '1', 1, 'resident branch forced for USER'),
    ('busy ? selected : NULL', 'selected', 1, 'morecore outside busy'),
]


# The first failing CHECK must be the one targeted by this mutation. Compiler
# failures, crashes, timeouts and an unrelated assertion never count as RED.
EXPECTED_CHECKS = [
    'ai.hblks==17 && a.info.arena==ai.arena',
    'ai.hblks==17 && a.info.arena==ai.arena',
    'os32_nano_morecore(&a,&reent,-1)==(void*)-1 && a.brk==old',
    'os32_nano_morecore(&a,&reent,32)==(void*)-1 && a.brk==0xfffffff0u',
    'os32_nano_morecore(&a,&reent,1)==(void*)-1 && calls==0 && a.brk==a.initial',
    '!os32_nano_select(&b)',
    '_malloc_r(&nested, 8) == NULL && nested._errno == ENOMEM',
    'a.free_list != NULL',
    'a.free_list != NULL',
    'r && ((unsigned char*)r)[0]==0x37 && ((unsigned char*)r)[15]==0x37',
    'a.sbrk_start==(char*)storage+1 && a.free_list==NULL',
    'p==NULL && a.brk==(uintptr_t)storage+25',
    'malloc(OS32_NANO_PAGE)==NULL && a.brk==old && a.mapped_end==old+OS32_NANO_PAGE',
    'a.mapped_end==(uintptr_t)storage+2*OS32_NANO_PAGE',
    'os32_nano_morecore(&a,&reent,16)==(void*)old && a.brk==old+16',
    'os32_nano_morecore(&a,&reent,16)==(void*)old && a.brk==old+16',
    'q && ((unsigned char*)q)[0]==0 && ((unsigned char*)q)[31]==0',
    'a.free_list==NULL && reent._errno==EINVAL',
    'os32_nano_morecore(&a,&reent,1)==(void*)-1 && calls==0',
    'p && calls==1',
    'os32_nano_sbrk(&reent,16)==(void*)-1 && a.brk==old',
]
assert len(EXPECTED_CHECKS) == len(MUTATIONS)


@host32.control_session
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=['native', 'qemu'])
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if args.runner is None:
        runners = os.environ.get('HOST32_RUNNERS', 'native qemu').split()
        if not runners:
            raise ValueError('HOST32_RUNNERS is empty')
        for index, runner in enumerate(runners):
            command_line = [sys.executable, '-B', str(Path(__file__).resolve()), '--runner', runner]
            if args.mutate:
                command_line.append('--mutate')
            subprocess.run(command_line, check=True)
        return 0
    host32.begin_control(args.mutate, args.runner, ROOT)
    prefix = Path(os.environ.get('CROSS_DIR', '/usr/local/cross'))
    with tempfile.TemporaryDirectory(prefix='nano_adapter_') as tmp:
        work = Path(tmp)
        archive = builder.build(prefix, work)
        build_host(prefix, work)
        exe, mapfile = link(prefix, work, archive)
        gate.validate_output(prefix, mapfile, exe)
        with host32.control(args.mutate, args.runner, ROOT) as normal:
            if normal:
                result = host32.run([str(exe)], runner=args.runner, capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)
                assert result.returncode == 0, result.stdout + result.stderr
                print(result.stdout.strip())
                negative_links = 0
                for name in sorted(gate.FORBIDDEN):
                    rejection(prefix, work, archive, 'void ' + name + '(void) {}\n')
                    negative_links += 1
                for name in sorted(gate.CONNECTED):
                    rejection(prefix, work, archive, 'void ' + name + '(void) {}\n')
                    negative_links += 1
                    rejection(prefix, work, archive, 'void ' + name + '(void) {}\n', after=True)
                    negative_links += 1
                for name in sorted(gate.MORECORE):
                    for after in (False, True):
                        rejection(prefix, work, archive, 'void ' + name + '(void) {}\n', after=after)
                        negative_links += 1
                for member in ('libc_a-malignr.o', 'libc_a-msizer.o'):
                    obj = work / member
                    obj.write_bytes(builder.inputs.command(prefix, 'ar', 'p', prefix / 'i386-elf/lib/libc.a', member))
                    exe, mapfile = link(prefix, work, archive, [obj], after=True)
                    try:
                        gate.validate_output(prefix, mapfile, exe)
                    except ValueError as exc:
                        assert 'unconnected' in str(exc) and not exe.exists()
                        negative_links += 1
                    else:
                        raise AssertionError('original newlib unconnected entry accepted')
                print(f'nano link gate: {negative_links} negative links rejected (allow-multiple-definition)')
        if args.mutate:
            source = SOURCE.read_text()
            def one(entry):
                (old, new, hits, label), expected = entry
                assert source.count(old) == hits, label
                mutant = work / 'mutant.c'
                mutant.write_text(source.replace(old, new))
                mutant_archive = builder.build(prefix, work / "mut-build", adapter=mutant, reuse=archive)
                exe, mapfile = link(prefix, work, mutant_archive)
                gate.validate_output(prefix, mapfile, exe)
                result = host32.run([str(exe)], runner=args.runner, capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)
                assert result.returncode == 1 and result.stdout.endswith(' check: ' + expected + '\n'), (label, result)
                return 'MUTATION runtime RED: ' + label
            red = 0
            # These mutations share the link workspace and must remain serial.
            for message in run_ordered(one, zip(MUTATIONS, EXPECTED_CHECKS), serial=True):
                print(message)
                red += 1
            print(f'nano adapter: {red} runtime RED / 0 survived / 0 ERROR')
            # A gate mutation counts only an executed assertion on a valid link.
            gate_source = (ROOT / 'sdk/allocator/check_link.py').read_text()
            gate_red = 0
            for old, new, symbol in (
                    ('if definitions.get(symbol):', 'if False:', '_memalign_r'),
                    ('if len(providers) != 1 or not providers[0].endswith',
                     'if False and not providers[0].endswith', 'malloc'),
                    ("if any(not provider.endswith('(nano_adapter.o)') for provider in providers):",
                     'if False:', '_sbrk')):
                assert gate_source.count(old) == 1
                script = work / 'gate_mutant.py'
                changed = gate_source.replace(old, new)
                compile(changed, str(script), 'exec')
                script.write_text(changed)
                mutant_gate = load('mutant_gate', script)
                try:
                    rejection(prefix, work, archive, 'void ' + symbol + '(void) {}\n', mutant_gate)
                except AssertionError as exc:
                    assert str(exc) == 'link gate accepted forbidden/duplicate provider'
                    gate_red += 1
                    print('MUTATION runtime RED: link gate ' + symbol)
                else:
                    raise AssertionError('gate mutant survived')
            print(f'nano link gate: {gate_red} runtime RED / 0 survived / 0 ERROR')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
