"""L16a/L16b: immutable pre-move attribute/grid/cursor traces and isolated closure."""
import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/tests/fixtures/dep_dir_d'
BACKEND = 'platform/pc98/console_attr.c'
HEADER = 'include/console_attr.h'
HARNESS = 'tools/tests/dep_dir_d_console_host.c'
FILES = (BACKEND, HEADER, HARNESS, 'lib/kprintf_attr.c', 'include/types.h',
         'include/pc98.h', 'include/kprintf.h', 'sdk/include/os32/os32_kapi_shared.h',
         'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_generations.h',
         'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ls.h',
         'sdk/include/os32/os32_kapi_generated.h')
OLD, NEW = 'attr | TATTR_VISIBLE', 'attr | 0'


def kernel_list():
    # The broad selector owns build-rule changes, without a build/*.mk input.
    result = subprocess.run(['make', '-qp'], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode in (0, 1), result.stderr
    return result.stdout


def once(text, old, new):
    assert text.count(old) == 1, old
    result = text.replace(old, new, 1)
    assert result.count(old) == 0 and (new == '' or result.count(new) == text.count(new) + 1)
    return result


def build(work, source, omit_header=False):
    for rel in FILES:
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, dest)
    (work / BACKEND).write_text(source)
    if omit_header:
        (work / HEADER).unlink()
    exe = work / 'console-attr'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', '-I' + str(work / 'include'),
                    str(work / HARNESS), str(work / 'lib/kprintf_attr.c'),
                    str(work / BACKEND), '-o', str(exe)], check=True)
    return exe


def trace(exe):
    result = host32.run([str(exe)], runner='qemu', capture_output=True)
    assert result.returncode == 0, result
    return result.stdout


def resident_check(text):
    block = re.search(r'^C_KERNEL = (.*)$', text, re.M).group(1)
    assert block.split().count(BACKEND) == 1


def verify():
    expected = (FIXTURE / 'console_attr.trace').read_bytes()
    manifest = json.loads((FIXTURE / 'console_attr.json').read_text())
    assert len(expected) == 256
    assert hashlib.sha256(expected).hexdigest() == manifest['trace_sha256']
    assert hashlib.sha256((ROOT / HARNESS).read_bytes()).hexdigest() == manifest['harness_sha256']
    resident_check(kernel_list())
    source = (ROOT / BACKEND).read_text()
    once(source, OLD, NEW)
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-console-') as td:
        assert trace(build(Path(td), source)) == expected
    print('PASS L16a all 256 fixed values, wrapper, isolated headers, resident membership')
    return source, expected


def mutation_build(source, expected, execute=False):
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-console-mut-') as td:
        exe = build(Path(td), once(source, OLD, NEW))
        if execute:
            assert trace(exe) != expected
    print('PASS -Werror visible-OR mutation preparation')


def mutations(source, expected):
    mutation_build(source, expected, execute=True)
    text = kernel_list()
    omitted = re.sub(r'^C_KERNEL = (.*)$',
                     lambda m: once(m[0], BACKEND + ' ', ''), text, flags=re.M)
    try:
        resident_check(omitted)
    except AssertionError:
        pass
    else:
        raise AssertionError('resident omission escaped')
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-console-header-') as td:
        try:
            build(Path(td), source, omit_header=True)
        except subprocess.CalledProcessError:
            print('PASS isolated header omission rejected (not runtime kill)')
        else:
            raise AssertionError('original header leaked into copy')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--compile-mutations', action='store_true')
    args = parser.parse_args()
    source, expected = verify()
    grid_verify(args.compile_mutations, args.mutate)
    if args.compile_mutations:
        mutation_build(source, expected)
    if args.mutate:
        mutations(source, expected)


GRID_FILES = ('arch/x86/arch_io.h', 'arch/x86/x86_flags.h', 'include/bootlog.h', 'include/con_sink.h', 'include/config.h', 'include/console.h', 'include/console_attr.h', 'include/console_output.h', 'include/io.h', 'include/kbd_inject.h', 'include/kprintf.h', 'include/memmap.h', 'include/pc98.h', 'include/tvram.h', 'include/types.h', 'kernel/console.c', 'kernel/console_backend.h', 'lib/kstring.h', 'lib/utf8.h', 'platform/pc98/console.c', 'platform/pc98/platform_io.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h', 'tools/tests/dep_dir_d_console_grid_host.c')

GRID = 'platform/pc98/console.c'
GRID_KERNEL = 'kernel/console.c'
GRID_HEADER = 'kernel/console_backend.h'
GRID_HARNESS = 'tools/tests/dep_dir_d_console_grid_host.c'
GRID_MUTATIONS = (
    (GRID, '*p ^= TATTR_REVERSE;', '*p |= TATTR_REVERSE;'),
    (GRID_KERNEL, '    con_sink_enable();\n    if (v86_is_active()) return;',
     '    if (v86_is_active()) return;\n    con_sink_enable();'),
)


def grid_command(work, exe, baseline=False):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0', '-Wall', '-Wextra',
            '-Werror', '-Wno-sign-compare', *(['-Wno-unused-variable', '-DBASELINE'] if baseline else []),
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-static', '-no-pie', '-D__KERNEL_BUILD__',
            *['-I' + str(work / p) for p in ('.', 'include', 'arch/x86',
                 'platform/pc98', 'kernel', 'lib', 'sdk/include/os32')],
            str(work / GRID_HARNESS), '-o', str(exe)]


def grid_instrument(text):
    # Preserve each product lvalue's width; loops/conditions/values stay intact.
    text = re.sub(r'volatile (u8|u16)\s+\*(\w+) = \(volatile \1 \*\)P2V_IO\(([^;]+)\);',
                  lambda m: f'u32 {m[2]} = {m[3]};', text)
    text = re.sub(r'volatile u8\s+\*attr;', 'u32 attr;', text)
    text = re.sub(r'attr = \(volatile u8 \*\)P2V_IO\(([^;]+)\);', r'attr = \1;', text)
    text = re.sub(r'\*\(volatile (u8|u16) \*\)P2V_IO\(([^\n;]+)\) = ([^;]+);',
                  lambda m: f'store({m[2]}, sizeof({m[1]}), {m[3]});', text)
    text = re.sub(r'\*\(volatile (u8|u16) \*\)P2V_IO\(([^\n;]+)\)',
                  lambda m: f'load({m[2]}, sizeof({m[1]}))', text)
    for pointer in ('text', 'attr'):
        text = re.sub(r'\b'+pointer+r'\[([^\]]+)\] = ([^;]+);',
                      lambda m: f'store({pointer} + ({m[1]}) * sizeof(u16), sizeof(u16), {m[2]});', text)
        text = re.sub(r'\b'+pointer+r'\[([^\]]+)\]',
                      lambda m: f'load({pointer} + ({m[1]}) * sizeof(u16), sizeof(u16))', text)
    text = re.sub(r'^        \*attr = ([^;]+);', r'        store(attr, sizeof(u8), \1);', text, flags=re.M)
    text = re.sub(r'\*p ([\^|])= TATTR_REVERSE;',
                  lambda m: f'store(p, sizeof(u8), load(p, sizeof(u8)) {m[1]} TATTR_REVERSE);', text)
    assert not re.search(r'P2V_IO|\b(?:text|attr)\[|\*p [\^|]=', text)
    return text


def grid_build(work, replacement=None, omit_header=False):
    for rel in GRID_FILES:
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, dest)
    if replacement:
        path, old, new = replacement
        dest = work / path
        dest.write_text(once(dest.read_text(), old, new))
    (work / 'console_source.inc').write_text(grid_instrument((work / GRID_KERNEL).read_text()))
    (work / GRID).write_text(grid_instrument((work / GRID).read_text()))
    if omit_header:
        (work / GRID_HEADER).unlink()
    exe = work / 'grid'
    subprocess.run(grid_command(work, exe), check=True)
    return exe


def grid_verify(compile_mutations=False, mutate=False):
    expected = gzip.decompress((FIXTURE / 'console_grid.trace.gz').read_bytes())
    manifest = json.loads((FIXTURE / 'console_grid.json').read_text())
    assert manifest['baseline'] == 'b400acb6'
    assert hashlib.sha256(expected).hexdigest() == manifest['trace_sha256']
    assert hashlib.sha256((ROOT / GRID_HARNESS).read_bytes()).hexdigest() == manifest['harness_sha256']
    text = kernel_list()
    block = re.search(r'^C_KERNEL = (.*)$', text, re.M).group(1).split()
    assert block.count(GRID) == 1
    generic = (ROOT / GRID_KERNEL).read_text()
    assert not re.search(r'\b(?:P2V_IO|outp|inp|GDC_\w+)\b', generic)
    assert '#include "io.h"' not in generic and '#include "pc98.h"' not in generic
    for path, old, new in GRID_MUTATIONS:
        once((ROOT / path).read_text(), old, new)
    # Both the copy manifest and selection map must carry the full closure.
    mapping = (ROOT / 'tools/check_map.d/check-dep-dir-d-console-host.yaml').read_text()
    for rel in GRID_FILES:
        assert '\n- ' + rel + '\n' in mapping, rel
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-grid-') as td:
        actual = trace(grid_build(Path(td)))
        assert actual == expected, 'grid/cursor/sink trace differs from b400acb6'
    print('PASS L16b fixed grid/cursor/sink byte trace, isolated closure and resident membership')
    if compile_mutations or mutate:
        for item in GRID_MUTATIONS:
            with tempfile.TemporaryDirectory(prefix='dep-dir-d-grid-mut-') as td:
                exe = grid_build(Path(td), item)
                if mutate:
                    assert trace(exe) != expected, item
        print('PASS L16b -Werror mutation preparation (reverse, sink-before-V86)')
    if mutate:
        with tempfile.TemporaryDirectory(prefix='dep-dir-d-grid-header-') as td:
            try:
                grid_build(Path(td), omit_header=True)
            except subprocess.CalledProcessError:
                print('PASS isolated grid header omission rejected (not runtime kill)')
            else:
                raise AssertionError('grid header leaked into isolated copy')


if __name__ == '__main__':
    main()
