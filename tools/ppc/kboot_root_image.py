#!/usr/bin/env python3
"""Check the root blob's placement in the product PPC linker layout."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-kboot-root-image', tier=2, budget=60,
             covers=['platform/powermac/root_image.c', 'tools/ppc/kboot_root_image.py'],
             inputs=['arch/ppc/kernel.ld', 'platform/powermac/kboot_layout.h',
                     'include/memmap.h', 'tools/gen_memmap.py', 'include/boot_root.h'])
PREFIX = '/home/hight/opt/cross-ppc/bin/powerpc-elf-'


def command(args):
    return subprocess.run([str(x) for x in args], cwd=ROOT, check=True,
                          capture_output=True, text=True).stdout


def verify(work, script, blob):
    (work / 'kernel.ld.in').write_text(script)
    command([PREFIX+'gcc', '-E', '-P', '-x', 'c', '-I'+str(work), '-I.',
             work/'kernel.ld.in', '-o', work/'kernel.ld'])
    (work/'entry.c').write_text('void ppc_kernel_entry(void) {}\n')
    command([PREFIX+'gcc', '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
             '-c', work/'entry.c', '-o', work/'entry.o'])
    objects = [work/'entry.o']
    if blob:
        (work/'blob.S').write_text('.section .rodata.boot_root,"a"\n.space 4096, 0x5a\n')
        command([PREFIX+'gcc', '-c', work/'blob.S', '-o', work/'blob.o'])
        objects.append(work/'blob.o')
    command([PREFIX+'gcc', '-nostdlib', '-Wl,--gc-sections',
             '-Wl,-T,'+str(work/'kernel.ld'), '-Wl,-Map,'+str(work/'PPCKERN.map'),
             *objects, '-o', work/'kernel.elf'])
    symbols = {p[2]: int(p[0], 16) for line in
               command([PREFIX+'nm', '-n', work/'kernel.elf']).splitlines()
               if len(p := line.split()) == 3}
    start, end = symbols['__root_image_start'], symbols['__root_image_end']
    assert end-start == (4096 if blob else 0), 'root image size'
    assert symbols['__kernel_image_start'] <= start <= end <= symbols['__kernel_resident_end']
    if blob:
        map_text = (work/'PPCKERN.map').read_text()
        assert '.rodata.boot_root' in map_text
        assert f'0x{start:08x}' in map_text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='kboot-root-', dir=os.environ.get('TMPDIR')) as tmp:
        work = Path(tmp)
        command(['python3', '-B', 'tools/gen_memmap.py', '--layout-header', work/'memmap_layout.h'])
        script = (ROOT/'arch/ppc/kernel.ld').read_text()
        verify(work, script, False)
        verify(work, script, True)
        print('PASS empty and 4KiB root image')
        if args.mutate:
            mutant = script.replace('KEEP(*(.rodata.boot_root))', '*(.rodata.boot_root)')
            assert mutant != script, 'mutation anchor'
            try:
                verify(work, mutant, True)
            except AssertionError as error:
                if str(error) != 'root image size':
                    raise
                print('RED remove KEEP')
            else:
                raise AssertionError('SURVIVED remove KEEP')
    print(f'PASS elapsed={time.monotonic()-started:.2f}s')


if __name__ == '__main__':
    main()
