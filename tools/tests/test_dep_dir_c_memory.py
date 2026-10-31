"""L14a immutable pre-move memory trace (no git needed at test runtime).

Capture is deliberately separate from the command-line verifier. See the
fixture manifest for revisions, inputs, shim/source hashes and procedure.
All positive and later mutation builds use the same copied directory tree.
"""
CHECK = dict(name='dep-dir-c-memory-host', tier=1,
    covers=['tools/tests/paging_source.py', 'mm/memory_boot.c', 'platform/pc98/memory.c', 'include/platform_memory.h', 'platform/pc98/platform_memory_desc.h', 'mm/paging.c', 'mm/pgalloc.c', 'kernel/sys.c', 'mm/physmem.c', 'kernel/*.h', 'platform/pc98/*.h', 'include/*.h', 'arch/x86/*.h', 'lib/*.h', 'drivers/**/*.h', 'exec/*.h', 'sdk/include/os32/*.h', 'tools/tests/host_arch/*.h', 'tools/tests/host32.py', 'tools/tests/dep_dir_c_memory_host.c', 'tools/tests/fixtures/dep_dir_c/**', 'tools/tests/kernel_includes.py', 'mm/*.h', 'init/*.h', 'arch/x86/kernel/*.h', 'kernel/**/*.h', 'net/*.h', 'kapi/*.h', 'gfx/*.h', 'lib/sqlite3/*.h'],
    needs=[], budget=60.0)

from paging_source import paging_source
from kernel_includes import include_dirs, include_flags
import gzip
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'tools/tests/fixtures/dep_dir_c'
SOURCES = ('kernel/sys.c',) + tuple('mm/' + name + '.c' for name in
                ('paging', 'pgalloc', 'memory_boot', 'physmem')) + (
                    'platform/pc98/memory.c',)
HEADER_DIRS = tuple(dict.fromkeys(include_dirs()))
HARNESS = 'tools/tests/dep_dir_c_memory_host.c'

# name, source, literal target, replacement, fixed input
MUTATIONS = (
    ('bios-high-byte', 'platform/pc98/memory.c',
     'bios[BIOS_WORK_MEM_HIGH_MB + 1] << 8',
     'bios[BIOS_WORK_MEM_HIGH_MB + 1] << 0', 'bios-256'),
    ('low-restore', 'platform/pc98/memory.c',
     '*low = saved;', '*low = pattern;', 'wrap-later'),
    ('second-pass', 'platform/pc98/memory.c',
     'if (*(volatile u32 *)P2V_BOOT(addr) != (addr ^ MEMORY_BOOT_PROBE_XOR))',
     'if (*(volatile u32 *)P2V_BOOT(addr) == (addr ^ MEMORY_BOOT_PROBE_XOR))',
     'alias-middle'),
    ('legacy-clamp', 'platform/pc98/platform_memory_desc.h',
     'MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END,',
     'MEM_HIGH_RAM_BASE, MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END,',
     'legacy-16384'),
    ('background-end', 'mm/memory_boot.c',
     '                                  platform_memory_geometry.system_end / PAGE_SIZE,',
     '                                  platform_memory_geometry.system_end / PAGE_SIZE - 1,',
     'bios-5'),
    ('native-uc-end', 'platform/pc98/platform_memory_desc.h',
     '{TVRAM_CHAR_BASE, TVRAM_CG_WINDOW}',
     '{TVRAM_CHAR_BASE, TVRAM_CG_WINDOW + MEM_PAGE_SIZE}', 'bios-5'),
    ('native-pwt', 'mm/paging.c',
     '            return PTE_PCD;', '            return PTE_PWT;', 'bios-5'),
    ('rom-end', 'platform/pc98/platform_memory_desc.h',
     '{MEM_BIOS_ROM_START, MEM_BIOS_ROM_END + 1}',
     '{MEM_BIOS_ROM_START, MEM_BIOS_ROM_END + 1 - MEM_PAGE_SIZE}', 'bios-5'),
)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise AssertionError(f'host substitution count != 1: {old}')
    return text.replace(old, new, 1)


def instrument_probe(text):
    # Keep cell/low pointer assignments and volatile declarations intact.
    # The calls replace individual loads/stores, never the probe algorithm.
    if text.count('P2V_BOOT(') != 3:
        raise AssertionError('P2V_BOOT probe sites changed')
    text = text.replace('P2V_BOOT(', 'host_boot_ptr(')
    for old, new in (
        ('bios[BIOS_WORK_MEM_HIGH_MB]', 'host_bda_read(bios, BIOS_WORK_MEM_HIGH_MB)'),
        ('bios[BIOS_WORK_MEM_HIGH_MB + 1]', 'host_bda_read(bios, BIOS_WORK_MEM_HIGH_MB + 1)'),
        ('saved = *low;', 'saved = host_read((u32)low);'),
        ('*cell = pattern;', 'host_write((u32)cell, pattern);'),
        ('if (*low != saved)', 'if (host_read((u32)low) != saved)'),
        ('if (*cell != pattern)', 'if (host_read((u32)cell) != pattern)'),
        ('*(volatile u32 *)host_boot_ptr(addr)', 'host_read(addr)'),
    ):
        text = replace_once(text, old, new)
    # The restore mutation writes the pattern instead, retaining saved in the
    # preceding comparison. Both variants must compile with the same -Werror.
    import re
    text, count = re.subn(r'\*low = (saved|pattern);',
                          r'host_write((u32)low, \1);', text)
    if count != 1:
        raise AssertionError('low restore site count != 1')
    return text


def build(work, sample, source_root=ROOT, mutation=None, attributes=False):
    # N1: retain kernel/ and platform/pc98/, including every transitive header.
    # No copied source can resolve a sibling header from the original kernel.
    for directory in HEADER_DIRS:
        for source in (source_root / directory).glob('*.h'):
            dest = work / source.relative_to(source_root)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
    if mutation and mutation[1].endswith('.h'):
        path = work / mutation[1]
        path.write_text(replace_once(path.read_text(), mutation[2], mutation[3]))
    for source in (ROOT / 'tools/tests/host_arch').glob('*.h'):
        dest = work / 'host_arch' / source.name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
    for rel in SOURCES:
        text = paging_source(source_root / rel)
        if mutation and rel == mutation[1]:
            text = replace_once(text, mutation[2], mutation[3])
        unit = Path(rel).stem
        if unit in ('paging', 'pgalloc', 'sys'):
            text = text.replace('irq_save()', 'host_irq_save()')
            text = text.replace('irq_restore(flags)', 'host_irq_restore(flags)')
        if rel == 'platform/pc98/memory.c':
            text = instrument_probe(text)
        dest = work / Path(rel).parent / (unit + '_host_source.c')
        dest.write_text(text)
    shutil.copyfile(ROOT / HARNESS, work / 'harness.c')
    exe = work / 'memory-test'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
           '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
           '-nostdlib', '-static', '-no-pie', '-ffunction-sections',
           '-Wl,--gc-sections']
    cmd += [f'-DTEST_{key.upper()}={sample[key]}UL' for key in
            ('kb', 'reported', 'mode', 'fail', 'repeat')]
    if attributes:
        cmd += ['-DTEST_PLATFORM_ATTRIBUTES']
    cmd += ['-I' + str(work / 'host_arch'), *include_flags(work)]
    cmd += [str(work / 'harness.c'), str(work / 'mm/physmem_host_source.c'),
            '-o', str(exe)]
    subprocess.run(cmd, check=True)
    return exe


def trace(exe):
    result = host32.run([str(exe)], capture_output=True, timeout=120)
    if result.returncode:
        raise AssertionError(f'host rc={result.returncode}: {result.stdout[-2000:]!r}')
    return canonical(result.stdout)


def canonical(data):
    lines = data.splitlines(keepends=True)
    # Only the OR operands lack a specified C evaluation order. Probe records
    # have sequence numbers and must retain their precise read/write order.
    for i in range(len(lines) - 1):
        if lines[i].startswith(b'b ') and lines[i + 1].startswith(b'b '):
            lines[i:i + 2] = sorted(lines[i:i + 2])
    return b''.join(lines)


def validate(data):
    """Prove compressed PT records cover every allocated table exactly once."""
    phase, pdi, end, directories, phases = None, None, 0, 0, []

    def finish_table():
        if pdi is not None and end != (pdi + 1) * 1024:
            raise AssertionError('incomplete PTE table')

    for line in data.decode('ascii').splitlines():
        parts = line.split()
        if len(parts) != 5 or any(len(x) != 8 for x in parts[1:]):
            raise AssertionError('malformed trace record')
        tag = parts[0]
        a, b, c, d = (int(x, 16) for x in parts[1:])
        if tag == '!':
            raise AssertionError('host assertion')
        if tag == 'P':
            finish_table()
            if phase is not None and directories != 1024:
                raise AssertionError('incomplete PDE array')
            if (b, c, d) != (1024, 1024, 0):
                raise AssertionError('unexpected paging dimensions')
            phase, pdi, directories = a, None, 0
            phases.append(a)
        elif tag == 'D':
            finish_table()
            if a != directories or c not in (0, 1):
                raise AssertionError('PDE order/presence')
            directories += 1
            pdi = a if c else None
            end = a * 1024
        elif tag == 'T':
            if pdi is None or a != end or not a < b <= (pdi + 1) * 1024:
                raise AssertionError('PTE run gap/overlap')
            end = b
    finish_table()
    if phases != [0, 1] or directories != 1024:
        raise AssertionError('missing paging snapshot')


def compare(actual, expected):
    validate(actual)
    validate(expected)
    if actual != expected:
        left, right = actual.splitlines(), expected.splitlines()
        for i, (a, b) in enumerate(zip(left, right)):
            if a != b:
                raise AssertionError(f'trace row {i + 1}: actual={a!r} expected={b!r}')
        raise AssertionError(f'trace length: actual={len(left)} expected={len(right)}')


def comparator_selftest(expected):
    # Required comparator check: flip one PTE bit in the EXPECTED fixture.
    rows = expected.splitlines(keepends=True)
    for i, row in enumerate(rows):
        if row.startswith(b'T '):
            fields = row.decode().split()
            fields[3] = f'{int(fields[3], 16) ^ 1:08x}'
            rows[i] = (' '.join(fields) + '\n').encode()
            break
    else:
        raise AssertionError('fixture has no PTE')
    try:
        compare(expected, b''.join(rows))
    except AssertionError:
        return
    raise AssertionError('comparator accepted flipped PTE')


def verify():
    manifest = json.loads((FIXTURES / 'manifest.json').read_text())
    input_path = FIXTURES / 'inputs.json'
    if hashlib.sha256(input_path.read_bytes()).hexdigest() != manifest['capture_sha256'][
            'tools/tests/fixtures/dep_dir_c/inputs.json']:
        raise AssertionError('fixed inputs digest mismatch')
    inputs = json.loads(input_path.read_text())
    for sample in inputs:
        path = FIXTURES / (sample['name'] + '.trace.gz')
        expected = gzip.decompress(path.read_bytes())
        if hashlib.sha256(expected).hexdigest() != manifest['traces'][sample['name']]:
            raise AssertionError('fixture digest mismatch: ' + sample['name'])
        comparator_selftest(expected)
        with tempfile.TemporaryDirectory(prefix='dep-dir-c-memory-') as td:
            actual = trace(build(Path(td), sample))
        compare(actual, expected)
        print('PASS L14a ' + sample['name'], flush=True)
    print('PASS fixed traces / full PDE+PTE coverage / comparator bit flip', flush=True)
    # L15a direct checks run separately, preserving the capture harness's
    # trace format and allowing L14 sources to use build(source_root=...).
    sample = next(s for s in inputs if s['name'] == 'bios-5')
    with tempfile.TemporaryDirectory(prefix='dep-dir-c-attributes-') as td:
        actual = trace(build(Path(td), sample, attributes=True))
    compare(actual, gzip.decompress((FIXTURES / 'bios-5.trace.gz').read_bytes()))
    print('PASS L15a attributes / byte boundaries / legacy native UC / ROM priority',
          flush=True)


def mutations(compile_only=False):
    samples = {s['name']: s for s in json.loads((FIXTURES / 'inputs.json').read_text())}
    for mutation in MUTATIONS:
        name, rel, old, _, sample_name = mutation
        if (ROOT / rel).read_text().count(old) != 1:
            raise AssertionError('mutation target count != 1: ' + name)
        with tempfile.TemporaryDirectory(prefix='dep-dir-c-mutation-') as td:
            exe = build(Path(td), samples[sample_name], mutation=mutation)
            if compile_only:
                print('COMPILED -Werror ' + name, flush=True)
                continue
            actual = trace(exe)  # A signal/compile failure is never a detection.
        expected = gzip.decompress((FIXTURES / (sample_name + '.trace.gz')).read_bytes())
        try:
            compare(actual, expected)
        except AssertionError:
            print('RED trace comparison ' + name, flush=True)
        else:
            raise AssertionError('mutation survived: ' + name)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--compile-mutations', action='store_true')
    args = parser.parse_args()
    if args.compile_mutations:
        mutations(compile_only=True)
    else:
        verify()
        if args.mutate:
            mutations()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dep_dir_c_memory.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
