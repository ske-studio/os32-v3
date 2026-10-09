#!/usr/bin/env python3
"""T2f f1b real nano connection. Log: tools/tests/nano_adapter_tdd.md
票: docs/tasks/v3/TASK_T2D_T2H.md §3-5 f1b
"""
import argparse
import importlib.util
import os
import shutil
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
            '-I' + str(ROOT / 'sdk/allocator'), '-I' + str(ROOT / 'sdk/include/os32'), '-c', ROOT / 'tools/tests/nano_adapter_host.c',
            '-o', work / 'host.o')


def rejection(prefix, work, archive, text, checker=gate, after=False):
    probe = work / 'probe.c'
    probe.write_text(text)
    command(prefix, 'gcc', '-ffreestanding', '-fno-builtin', '-c', probe, '-o', work / 'probe.o')
    exe, mapfile = link(prefix, work, archive, [work / 'probe.o'], after=after)
    try:
        checker.validate_output(prefix, mapfile, exe, archive.with_suffix('.json'))
    except ValueError:
        assert not exe.exists(), "rejected executable remains"
        return
    raise AssertionError('link gate accepted forbidden/duplicate provider')


def public_link(prefix, work):
    """Real USER CRT + libc callers pass through the actual link_guard."""
    public = work / 'public'
    public.mkdir()
    flags = ['-std=gnu11', '-O2', '-ffreestanding', '-fno-builtin', '-fno-pie',
             '-I' + str(ROOT / 'sdk/include/os32'),
             '-include', str(ROOT / 'sdk/include/os32/os32_unit_stamp.h')]
    crt = public / 'syscalls.o'
    command(prefix, 'gcc', *flags, '-c', ROOT / 'sdk/crt/syscalls.c', '-o', crt)
    archive = builder.build(prefix, public, crt=crt)
    source = public / 'caller.c'
    source.write_text("""
#include "os32api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
KernelAPI *kapi;
void _start(void) {
    char *p = strdup("nano");
    setenv("NANO", p, 1);
    printf("%s %d", p, 7);
    free(p);
}
""")
    command(prefix, 'gcc', *flags, '-c', source, '-o', public / 'caller.o')
    exe, mapfile = public / 'caller.elf', public / 'caller.map'
    args = [sys.executable, str(ROOT / 'sdk/link_guard.py'), str(prefix / 'bin/i386-elf-ld'),
            '-m', 'elf_i386', '-T', str(ROOT / 'sdk/link/app.ld'), '-nostdlib', '--nmagic',
            '--gc-sections', '--allow-multiple-definition', '-Map=' + str(mapfile),
            '-L' + str(prefix / 'i386-elf/lib'), '-L' + str(prefix / 'lib/gcc/i386-elf/13.2.0'),
            '-o', str(exe), str(crt), str(public / 'caller.o'), str(archive), '-u', '_dtoa_r', '-lc', '-lgcc']
    result = subprocess.run(args, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    mapping = mapfile.read_text()
    for member in ('libc_a-makebuf.o', 'libc_a-nano-vfprintf.o', 'libc_a-strdup_r.o',
                   'libc_a-setenv_r.o', 'libc_a-dtoa.o', 'libc_a-mprec.o'):
        assert member in mapping, member
    gate.check(prefix, mapfile, archive.with_suffix('.json'))
    print('PASS USER link_guard: stdio/printf/strdup/setenv/dtoa/mprec -> adapter')
    # The actual wrapper must reject even a losing duplicate and remove output.
    rogue = public / 'rogue.c'; rogue.write_text('void os32_nano_morecore(void) {}')
    command(prefix, 'gcc', *flags, '-c', rogue, '-o', public / 'rogue.o')
    result = subprocess.run([*args, str(public / 'rogue.o')], capture_output=True, text=True)
    assert result.returncode != 0 and 'independent break provider' in result.stderr and not exe.exists(), result
    # Missing library/receipt cannot fall back to libc malloc.
    result = subprocess.run([a for a in args if a != str(archive)], capture_output=True, text=True)
    assert result.returncode != 0 and not exe.exists(), result
    print('PASS public USER duplicate morecore / missing adapter rejected')


def hash_providers(prefix, work, archive):
    """Renaming valid bytes is allowed; an adapter basename grants no trust."""
    private = [
        work / name for name in __import__('json').loads(builder.inputs.LEDGER.read_text())['sdk_build']['members']]
    renamed = work / 'renamed.o'
    shutil.copyfile(work / 'nano_adapter.o', renamed)
    alt = work / 'renamed.a'
    command(prefix, 'ar', 'rcs', alt, *private, renamed)
    exe, mapfile = link(prefix, work, alt)
    gate.validate_output(prefix, mapfile, exe, archive.with_suffix('.json'))
    command(prefix, 'objcopy', '--add-symbol=foreign_provider=.text:0,global', renamed)
    spoof = work / 'spoof'; spoof.mkdir()
    shutil.copyfile(renamed, spoof / 'nano_adapter.o')
    bad = spoof / 'spoof.a'
    command(prefix, 'ar', 'rcs', bad, *private, spoof / 'nano_adapter.o')
    exe, mapfile = link(prefix, work, bad)
    try:
        gate.validate_output(prefix, mapfile, exe, archive.with_suffix('.json'))
    except ValueError as exc:
        assert 'adapter provider' in str(exc) and not exe.exists()
    else:
        raise AssertionError('same-name untrusted adapter accepted')
    print('PASS providers validated by bytes, independent of member name')


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
    ('(uintptr_t)p >= start && (uintptr_t)p < a->brk',
     '(uintptr_t)p >= start || (uintptr_t)p < a->brk', 1, 'pointer arena bounds'),
    ('next > UINTPTR_MAX - (OS32_NANO_PAGE - 1u)', '0', 1, 'page rounding overflow'),
    ('!a->grow_exact', '1', 1, 'resident branch forced for USER'),
    ('busy ? selected : NULL', 'selected', 1, 'morecore outside busy'),
    ('a = new_arena(bytes);', 'a = bytes ? NULL : new_arena(bytes);', 1, 'secondary allocation disabled'),
    ('switch_arena(a);\n        os32_private_free_r(r, p);',
     'os32_private_free_r(r, p);', 1, 'free uses selected instead of owner'),
    ('if (unmap_arena(map_opaque, base, bytes) != 0) *link = a;',
     '(void)unmap_arena(map_opaque, base, bytes);', 1, 'failed unmap loses arena'),
    ('if (!arenas || a == arenas || a->live || !a->map_base) return;',
     'if (arenas && a == arenas && !a->live) { unmap_arena(map_opaque, a->initial, a->mapped_end-a->initial); return; }\n    if (!arenas || a == arenas || a->live || !a->map_base) return;', 1, 'primary returned'),
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
    'p && maps==1 && a.next && (uintptr_t)p >= a.next->initial',
    'unmaps==1 && a.next==NULL && a.free_list==NULL && ((unsigned char*)keep)[31]==0x74',
    'a.next==other && other->live==0 && unmaps==saved',
    'base != a.initial',
]
# f8 mutations retain the runtime assertion/expected-check pairing.
MUTATIONS += [
    ('bytes >= OS32_NANO_LARGE', 'bytes > OS32_NANO_LARGE', 1, 'large boundary moved to 65537'),
    ('struct large_block **link = p ? large_link(p) : NULL;',
     'if (p) large_valid(r, (struct large_block *)p - 1);\n    struct large_block **link = p ? large_link(p) : NULL;',
     1, 'prefix read before list match'),
    ('if (bytes > SIZE_MAX - sizeof(*b)) goto fail;', 'if (0) goto fail;', 1, 'large prefix overflow'),
    ('if (total > SIZE_MAX - (OS32_NANO_PAGE - 1u)) goto fail;', 'if (0) goto fail;', 1, 'large rounding overflow'),
    ('if (n && size > SIZE_MAX / n)', 'if (0)', 1, 'calloc product overflow'),
    ('p = allocate(r, 1, size, 0, NULL);\n                if (p) {',
     'p = allocate(r, 1, size, 0, NULL);\n                if (!p) large_release(r, large_link(old));\n                if (p) {',
     1, 'large realloc failure frees old'),
    ('*link = b;\n        r->_errno = ENOMEM;', 'r->_errno = ENOMEM;', 1, 'large failed unmap loses list'),
]
EXPECTED_CHECKS += [
    'p && !a.next && last_flags==OS32_NANO_TOPDOWN && !((uintptr_t)p&7)',
    'i < MAP_SLOTS',
    '_malloc_r(&reent,SIZE_MAX)==NULL && attempts==saved',
    '_malloc_r(&reent,SIZE_MAX-64u)==NULL && attempts==saved',
    '_calloc_r(&reent,0x80000000u,2)==NULL && attempts==saved',
    'realloc(p,65535)==NULL && ((unsigned char*)p)[65536]==0x71 && unmaps==saved',
    'unmaps==saved+1',
]

# f11 mutations leave all older replacement counts and first failures intact.
MUTATIONS += [
    ('if ((uintptr_t)tail + tail->size == a->brk)', 'if (1)', 1, 'trim returns USED tail'),
    ('(uintptr_t)tail + NANO_MINCHUNK + OS32_NANO_PAGE - 1u',
     '(uintptr_t)tail + NANO_MINCHUNK', 1, 'trim returns header page'),
    ('if (unmap_arena(map_opaque, keep, bytes) == 0)',
     'if ((unmap_arena(map_opaque, keep, bytes), 1))', 1, 'trim commits failed unmap'),
    ('a->brk = a->mapped_end = keep;', 'a->brk = keep;', 1, 'trim keeps stale mapped_end'),
    ('if (busy || !arenas || !selected) return 0;',
     'if (!arenas || !selected) return 0;', 1, 'trim ignores busy'),
    ('struct nano_free_chunk *tail = a->free_list;',
     'struct nano_free_chunk *tail = a->free_list;\n'
     '            if (a != selected && a->live && ((struct nano_free_chunk *)a->initial)->size <= 0) tail = NULL;',
     1, 'trim validates nonselected live chunk'),
]
EXPECTED_CHECKS += [
    'os32_nano_trim() == 0 && trim_unmaps == 0',
    'base != a.initial',
    'os32_nano_trim() == 0',
    'tail->size == (long)(keep-(uintptr_t)tail) && a.brk == keep && a.mapped_end == keep',
    'malloc(8) == NULL && reent._errno == ENOMEM',
    'os32_nano_trim() == (end-keep)/OS32_NANO_PAGE',
]
# image-page-floor removal is equivalent: every valid tail is >= initial and
# page_up(tail + MINCHUNK) already covers page_up(initial); no fake RED.


# g2s: one retry and the safe trim service, with named runtime failures.
MUTATIONS += [
    ('if (!p && retry_allowed(size)) {\n        p = malloc_once(r, size);', 'while (!p && retry_allowed(size)) {\n        p = malloc_once(r, size);', 1, 'retry-unbounded'),
    ('if (busy) { retry_refused_count++; return 0; }', 'if (busy) { if (gui_retry) kapi->sys_yield(); retry_refused_count++; return 0; }', 1, 'retry-while-busy'),
    ('last_failure == FAIL_ALLOC && kapi->sys_yield', 'kapi->sys_yield', 1, 'retry-on-enter-refused'),
    ('gui_retry && !in_trim &&', 'gui_retry &&', 1, 'retry-in-trim'),
    ('size && gui_retry &&', 'size &&', 1, 'retry-without-gui'),
    ('    if (size && gui_retry &&', '    (void)size;\n    if (gui_retry &&', 1, 'retry-size0'),
    ('memcpy(p, old, old_size < size ? old_size : size);', 'if (!retry_count) memcpy(p, old, old_size < size ? old_size : size);', 3, 'realloc-retry-no-copy'),
    ('large_release(r, large_link(old));', 'if (!retry_count) large_release(r, large_link(old));', 1, 'realloc-retry-leaks-old'),
    ('    if (hook) hook_pages_total += hook();', '    kapi->gui_call(GUI_OP_TRIM_DONE, epoch);\n    if (hook) hook_pages_total += hook();', 1, 'done-before-hook'),
]
EXPECTED_CHECKS += ['yields <= 1', 'yields <= 1', 'malloc(8) == NULL && yields == 0', 'malloc(65536) == NULL && yields == before', 'malloc(65536) == NULL && yields == 0', 'calloc(0,8) == NULL && yields == 0', '((const unsigned char *)p)[i] == byte', 'unmaps == old_unmaps+1 && mapped[0] == 0', 'done_calls == 0 && serve_probe == 2']

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
        gate.validate_output(prefix, mapfile, exe, archive.with_suffix('.json'))
        with host32.control(args.mutate, args.runner, ROOT) as normal:
            if normal:
                result = host32.run([str(exe)], runner=args.runner, capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)
                assert result.returncode == 0, result.stdout + result.stderr
                print(result.stdout.strip())
                public_link(prefix, work)
                hash_providers(prefix, work, archive)
                negative_links = 0
                for name in sorted(gate.FORBIDDEN):
                    rejection(prefix, work, archive, 'void ' + name + '(void) {}\n')
                    negative_links += 1
                rejection(prefix, work, archive, '__asm__(".globl memalign\\n.set memalign, 0");')
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
                        gate.validate_output(prefix, mapfile, exe, archive.with_suffix('.json'))
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
                gate.validate_output(prefix, mapfile, exe, mutant_archive.with_suffix('.json'))
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
                    ("if len(providers) != 1 or hashes[providers[0]] != trusted['adapter']:",
                     'if False:', 'malloc'),
                    ("if len(providers) > 1 or (required and not providers) or any(hashes[p] != expected for p in providers):",
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
