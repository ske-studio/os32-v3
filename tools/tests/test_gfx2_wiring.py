#!/usr/bin/env python3
"""Exercise final-image checks with linked weak/strong and impostor objects."""
CHECK = dict(name='gfx2-wiring-host', tier=1, needs=[], budget=5,
    covers=['tools/checks/gfx2_wiring.txt', 'tools/checks/gfx2_no_weak.sh',
            'tools/tests/test_gfx2_wiring.py', 'gfx/v2/a0_weak.c',
            'gfx/sync/a0_weak.c', 'drivers/video/geforce/gf_v2_weak.c'])
from pathlib import Path
import argparse
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/checks/gfx2_no_weak.sh'


def fixture(work, strong=False, impostor=False, kind='kernel', elsewhere=False, storage='bss'):
    name = 'gfx2_pal' if kind == 'kernel-data' else 'gfx2_surf_new' if kind == 'kernel' else 'gf_v2_alloc'
    data = kind == 'kernel-data'
    declaration = ('const int ' + name + ' = 3;' if storage == 'rodata' else 'int ' + name + (' = 3;' if storage == 'data' else ';')) if data else 'int ' + name + '(void) { return -1; }'
    (work / 'manifest').write_text('I1 ' + kind + ' ' + name + '\n')
    weak = work / ('a0_weak.o' if kind.startswith('kernel') else 'gf_v2_weak.o')
    if elsewhere:
        weak = work / 'external.o'
    source = work / 'weak.c'
    source.write_text(('' if impostor else '__attribute__((weak)) ') + declaration + '\n')
    subprocess.run(['gcc', '-m32', '-ffreestanding', '-fno-pie', '-c', str(source), '-o', str(weak)], check=True)
    objects = [str(weak)]
    if strong:
        source = work / 'strong.c'
        source.write_text((declaration if data else 'int ' + name + '(void) { return 0; }') + '\n')
        obj = work / 'strong.o'
        subprocess.run(['gcc', '-m32', '-ffreestanding', '-fno-pie', '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    elf, mapfile = work / 'fixture.elf', work / 'fixture.map'
    subprocess.run(['ld', '-m', 'elf_i386', '-e', name, '-Map=' + str(mapfile), *objects, '-o', str(elf)], check=True)
    return [str(work / 'manifest'), str(elf), str(mapfile), str(elf), str(mapfile)]


def check(script, args):
    return subprocess.run(['sh', str(script), 'I1', *args], cwd=ROOT).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        for kind in ('kernel', 'geforce', 'kernel-data'):
            assert check(SCRIPT, fixture(work, kind=kind)) == 1
            assert check(SCRIPT, fixture(work, strong=True, kind=kind)) == 0
            assert check(SCRIPT, fixture(work, impostor=True, kind=kind)) == 1
        for storage in ('bss', 'data', 'rodata'):
            assert check(SCRIPT, fixture(work, kind='kernel-data', storage=storage, elsewhere=True)) == 1
            assert check(SCRIPT, fixture(work, kind='kernel-data', storage=storage, strong=True)) == 0
            assert check(SCRIPT, fixture(work, kind='kernel-data', storage=storage, impostor=True)) == 1
        # Every placeholder function has exactly one manifest row, and no row
        # names a missing function. Stage ownership is reviewed with §12.
        import re
        rows = [l.split() for l in (ROOT / 'tools/checks/gfx2_wiring.txt').read_text().splitlines() if l and not l.startswith('#')]
        actual = set()
        for path in ('gfx/v2/a0_weak.c', 'gfx/sync/a0_weak.c', 'drivers/video/geforce/gf_v2_weak.c'):
            target = 'geforce' if 'gf_v2_weak' in path else 'kernel'
            actual.update((target, n) for n in re.findall(r'^__attribute__\(\(weak\)\).*?\b(\w+)\([^;]*?\)\n\{', (ROOT / path).read_text(), re.M))
        # C8 adds a strong internal entry with no A0 placeholder.
        actual.add(('kernel', 'gfx2_check_exec'))
        actual.update(('kernel-data', n) for n in ('gfx2_v1_shim', 'gfx2_lost_be', 'gfx2_sw_table', 'gfx2_adapters', 'gfx2_surfs', 'gfx2_pal', 'gfxsync_ctx_pool', 'gfx2_quar_owner'))
        assert len(rows) == len(actual)
        assert {(target, name) for _, target, name in rows} == actual
        if args.mutate:
            original = SCRIPT.read_text()
            mutant = work / 'mutant.sh'
            anchor = "symbol[1] not in allowed"
            assert original.count(anchor) == 1
            mutant.write_text(original.replace(anchor, "symbol[1] not in (*allowed, 'W')"))
            assert check(mutant, fixture(work, elsewhere=True)) == 0
            print('RED weakened type gate: weak ELF incorrectly accepted')
            mutant.write_text(original.replace(anchor, "symbol[1] not in (*allowed, 'V', 'v')"))
            assert check(mutant, fixture(work, kind='kernel-data', elsewhere=True)) == 0
            print('RED weakened data gate: weak data incorrectly accepted')
            anchor = 'any(lo <= symbol[0] < hi for lo, hi in ranges)'
            assert original.count(anchor) == 1
            mutant.write_text(original.replace(anchor, 'False'))
            assert check(mutant, fixture(work, impostor=True)) == 0
            assert check(mutant, fixture(work, kind='kernel-data', impostor=True)) == 0
            print('RED removed range gate: code/data impostor ELF incorrectly accepted')
    print('PASS final ELF weak/strong/impostor gates and manifest coverage')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
