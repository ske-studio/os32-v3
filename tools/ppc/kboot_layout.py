#!/usr/bin/env python3
"""Link the product kernel layout; independent of make check and firmware."""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-kboot-layout', tier=2, budget=60,
             covers=['arch/ppc/kernel.ld', 'platform/powermac/platform_kboot_layout.h',
                     'include/memmap.h', 'tools/gen_memmap.py',
                     'tools/ppc/kboot_layout.py', 'lib/kstring_c.c', 'lib/kstring.h',
                     'lib/crc32.c', 'lib/crc32_core.inc', 'lib/crc32.h',
                     'include/types.h', 'tools/ppc/kboot_root_image.py'])


def command(args):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, text=True,
                            capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


def report_product(path):
    from kboot_root_image import verify_embedded
    elf = path.read_bytes()
    hdr = struct.unpack_from('>16sHHIIIIIHHHHHH', elf)
    assert hdr[0][:6] == b'\x7fELF\x01\x02' and hdr[2] == 20
    ph = [struct.unpack_from('>8I', elf, hdr[5]+i*hdr[9]) for i in range(hdr[10])]
    load, = [p for p in ph if p[0] == 1]
    symbols = {name: int(value, 16) for value, name in re.findall(
        r'^\s*(0x[0-9a-f]+)\s+(__\w+) =', path.with_suffix('.map').read_text(), re.M)}
    _, _, va, pa, raw, memory, _, _ = load
    end = symbols['__kernel_resident_end']
    chain = symbols['__kernel_chain_end']
    assert va == pa == symbols['__kernel_image_start']
    assert va + raw == symbols['__kernel_file_end'] and va + memory == end
    layout = {name: int(value, 16) for name, value in re.findall(
        r'#define LAYOUT_(\w+) (0x[0-9A-Fa-f]+)', (path.parent/'memmap_layout.h').read_text())}
    shell = layout['MEM_SHELL_LOAD_ADDR']
    guard = layout['MEM_SHELL_GUARD']
    rows = [
        (va, end, 'kernel PT_LOAD', 'kernel.ld'),
        (end, chain, 'heap/KAPI/guards/SHM', 'kernel.ld'),
        (chain, layout['MEM_FIXED_PAGING_GUARD_BASE'], 'chain slack', 'kernel.ld'),
        (0x3e0000, layout['MEM_FIXED_PAGING_BASE'], 'paging guard', 'memmap.h'),
        (0x3e1000, layout['MEM_FIXED_PAGING_FRAME_END'], 'page tables', 'memmap.h'),
        (layout['MEM_STACK_GUARD'], layout['MEM_KSTACK_BASE'], 'kernel guard', 'memmap.h'),
        (0x3fc000, shell, 'kernel stack', 'memmap.h'),
        (0x400000, shell + layout['MEM_SHELL_MAX_SIZE'], 'shell image', 'memmap.h'),
        (0x4a5000, layout['MEM_SHELL_HEAP_BASE'], 'libc headroom', 'memmap.h'),
        (0x4b5000, guard, 'exec heap', 'memmap.h'),
        (0x4f5000, guard + layout['MEM_PAGE_SIZE'], 'shell guard', 'paging_boot.inc'),
        (0x4f6000, layout['MEM_SHELL_STACK_TOP'], 'shell stack', 'memmap.h')]
    cursor = 0x100000
    for first, stop, use, basis in rows:
        assert first == cursor and first <= stop, use
        print(f'{first:#08x} {stop:#08x} {use:22} {basis}')
        cursor = stop
    assert cursor == layout['MEM_POOL_BASE'] == 0x500000
    root = verify_embedded(elf, load, symbols, (path.parent/'root.iso').read_bytes())
    bss = symbols['__bss_end'] - symbols['__bss_start']
    print(f'PT_LOAD raw={raw} memory={memory} BSS={bss} root={root} '
          f'resident_end={end} chain_slack={0x3e0000-chain}')
    return end


def verify(work, script, cc, prefix):
    (work/'kernel.ld.in').write_text(script)
    command([cc, '-E', '-P', '-x', 'c', '-I'+str(work), '-I.',
             work/'kernel.ld.in', '-o', work/'kernel.ld'])
    flags = ['-mcpu=7450', '-m32', '-mbig-endian', '-msoft-float',
             '-mno-altivec', '-msdata=none', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', '-nostdlib', '-fcommon', '-fsigned-char',
             '-fno-short-enums', '-D__cdecl=', '-Iinclude', '-Ilib', '-I.']
    (work/'entry.c').write_text('''#include "platform/powermac/platform_kboot_layout.h"
void ppc_kernel_entry(void) { for (;;) {} }
unsigned char common_storage[123];
const unsigned layout[] = {PM_KERNEL_BASE, PM_KERNEL_IMAGE_MAX,
 PM_KERNEL_CHAIN_LIMIT, PM_KERNEL_STACK_SIZE, PM_KERNEL_LEDGER_SIZE};
''')
    objects = []
    for i, source in enumerate([work/'entry.c', ROOT/'lib/kstring_c.c',
                                ROOT/'lib/crc32.c']):
        obj = work/f'{i}.o'
        command([cc, *flags, '-c', source, '-o', obj])
        objects.append(obj)
    command([cc, *flags, '-Wl,--build-id=none', '-Wl,-T,'+str(work/'kernel.ld'),
             *objects, '-o', work/'kernel.elf'])
    elf = (work/'kernel.elf').read_bytes()
    hdr = struct.unpack_from('>16sHHIIIIIHHHHHH', elf)
    assert hdr[0][:6] == b'\x7fELF\x01\x02' and hdr[2] == 20, 'ELF32 BE PPC'
    syms = {m[2]: int(m[0], 16) for line in
            command([prefix+'nm', '-n', work/'kernel.elf']).splitlines()
            if len(m := line.split()) == 3}
    assert not command([prefix+'nm', '-u', work/'kernel.elf']).strip()
    sections = {}
    shoff, shsize, shnum, stridx = hdr[6], hdr[11], hdr[12], hdr[13]
    sh = [struct.unpack_from('>10I', elf, shoff+i*shsize) for i in range(shnum)]
    strings = elf[sh[stridx][4]:sh[stridx][4]+sh[stridx][5]]
    for s in sh:
        name = strings[s[0]:].split(b'\0')[0].decode()
        sections[name] = s
    data = sections['.rodata']
    offset = data[4]+syms['layout']-data[3]
    base, budget, limit, stack, ledger = struct.unpack_from('>5I', elf, offset)
    assert syms['__kernel_image_start'] == base == 0x100000, 'load base'
    assert hdr[4] == syms['ppc_kernel_entry'], 'entry'
    assert syms['__bss_end'] == syms['__ledger_fixed_end'], 'BSS end'
    assert syms['__ledger_fixed_end']-syms['__ledger_fixed_start'] == ledger
    assert syms['__kernel_stack_top']-syms['__kernel_stack_bottom'] == stack
    assert syms['__bss_end'] <= syms['__kernel_stack_bottom']
    assert syms['__kernel_resident_end']-base <= budget
    assert syms['__kernel_chain_end'] <= limit == 0x3e0000
    ph = [struct.unpack_from('>8I', elf, hdr[5]+i*hdr[9]) for i in range(hdr[10])]
    loads = [p for p in ph if p[0] == 1]
    assert len(loads) == 1
    p = loads[0]
    assert p[2] == p[3] == base and p[2]+p[5] == syms['__kernel_resident_end']
    assert p[2]+p[4] == syms['__kernel_file_end'] and p[5] > p[4]
    note = next(p for p in ph if p[0] == 4)
    assert struct.unpack_from('>III8sIIIII', elf, note[1]) == (
        8, 20, 0x1275, b'PowerPC\0', 0, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff)
    (work/'readelf.txt').write_text(command([prefix+'readelf', '-lSWs', work/'kernel.elf']))
    # Oversized BSS must fail the product linker ASSERTs.
    (work/'large.c').write_text('char too_large[0x300000];\n')
    command([cc, *flags, '-c', work/'large.c', '-o', work/'large.o'])
    result = subprocess.run([cc, *flags, '-Wl,-T,'+str(work/'kernel.ld'),
                             *objects, work/'large.o', '-o', work/'large.elf'],
                            text=True, capture_output=True)
    assert result.returncode != 0 and 'exceeds' in result.stderr, 'oversize accepted'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--product', type=Path, help='inspect linked PPCKERN.ELF and adjacent map/ISO')
    args = parser.parse_args()
    if args.product:
        report_product(args.product)
        return
    prefix = str(Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-')
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='kboot-layout-', dir=os.environ.get('TMPDIR')) as tmp:
        work = Path(tmp)
        command(['python3', '-B', 'tools/gen_memmap.py', '--layout-header', work/'memmap_layout.h'])
        script = (ROOT/'arch/ppc/kernel.ld').read_text()
        verify(work, script, prefix+'gcc', prefix)
        destination = Path(os.environ.get('TMPDIR', str(work)))
        (destination/'kboot-layout-readelf.txt').write_text((work/'readelf.txt').read_text())
        print('PASS product link / ELF / note / BSS / stack / ledger / oversize')
        if args.mutate:
            mutations = {
                'remove-budget-asserts': re.sub(r'    ASSERT\(__kernel_(?:resident_end - __kernel_image_start|chain_end).*?\)\n', '', script, flags=re.S),
                'shift-base': script.replace('. = PM_KERNEL_BASE;', '. = PM_KERNEL_BASE + PM_KERNEL_PAGE_SIZE;'),
                'shift-bss-end': script.replace('__bss_end = .;', '__bss_end = . + 4;'),
                'ledger-size': script.replace('. += PM_KERNEL_LEDGER_SIZE;', '. += PM_KERNEL_LEDGER_SIZE + 4;'),
                'ledger-unaligned': script.replace('        . = ALIGN(PM_KERNEL_PAGE_SIZE);\n        __ledger_fixed_start = .;', '        . += 4;\n        __ledger_fixed_start = .;'),
                'stack-overlaps-bss': script.replace('__kernel_stack_bottom = .;', '__kernel_stack_bottom = . - 4;'),
            }
            for name, mutant in mutations.items():
                assert mutant != script, name+' anchor'
                try:
                    verify(work, mutant, prefix+'gcc', prefix)
                except AssertionError:
                    print('RED '+name)
                except RuntimeError as error:
                    if not any(m in str(error) for m in ('ledger must terminate BSS', 'entry stack must be outside BSS')):
                        raise
                    print('RED '+name+' (linker ASSERT)')
                else:
                    raise AssertionError('SURVIVED '+name)
    print(f'PASS elapsed={time.monotonic()-started:.2f}s')


if __name__ == '__main__':
    main()
