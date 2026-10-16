#!/usr/bin/env python3
"""T3ab-A: product linker/header agreement, mixed inputs and incremental selection.

No emulator and no mutation pass. Deliberately malformed input artifacts exercise
rejection, paired with a valid control for both selections.
"""
import os
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_memmap as gen
from test_vmkernel_layout import fixture, generate, edit_elf
from test_memmap_gen import make_tree, CLEAN
import shutil


def run(cmd):
    result = subprocess.run([str(x) for x in cmd], cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


def check_fixture(work, layout):
    directory = work / ('layout' + str(layout))
    elf = fixture(directory, layout=layout, core_bss=33, sqlite_bss=35)
    header = directory / 'memmap_layout.h'
    gen.write_layout_header(ROOT, header, layout)
    assert not gen.selected_layout_defects(ROOT, elf, header, layout)
    sections, syms = gen.read_elf(elf)
    m = gen.Macros(gen.read_defines(ROOT / 'include/memmap.h', layout), syms)
    for lo, hi in (('__bss_start', '__bss_end'), ('__sqlite_data_end', '__sqlite_end')):
        assert syms[lo] <= syms[hi] and (syms[lo] | syms[hi]) & 3 == 0
    for s in sections:
        if s['noload']:
            lo, hi = ('__sqlite_data_end', '__sqlite_end') if s['name'].startswith('.sqlite') else ('__bss_start', '__bss_end')
            assert syms[lo] <= s['start'] <= s['end'] <= syms[hi]
    if layout:
        assert syms['__bss_end'] == syms['__ledger_fixed_end']
        assert syms['__ledger_fixed_end'] - syms['__ledger_fixed_start'] == 8192
        assert syms['__sqlite_start'] == syms['__bss_end']
        assert syms['__kernel_resident_end'] == m.get('MEM_SQLITE_STACK_BASE') + 131072
        assert m.get('KHEAP_BASE') == syms['__kernel_resident_end']
        expected = {'MEM_DMA_POOL_BASE': 0x7f0000, 'MEM_FIXED_PAGING_GUARD_BASE': 0x3e0000,
                    'MEM_FIXED_PAGING_BASE': 0x3e1000, 'MEM_FIXED_PAGING_USED_END': 0x3ec000,
                    'MEM_FIXED_PAGING_FRAME_END': 0x3fb000, 'MEM_STACK_GUARD': 0x3fb000,
                    'MEM_KSTACK_BASE': 0x3fc000, 'MEM_SHELL_LOAD_ADDR': 0x400000,
                    'MEM_POOL_BASE': 0x500000, 'MEM_SHELL_HEAP_BASE': 0x4b5000,
                    'MEM_SHELL_HEAP_SIZE': 0x40000, 'MEM_SHELL_GUARD': 0x4f5000,
                    'MEM_SHELL_STACK_SIZE': 0xa000, 'MEM_SHELL_MAX_SIZE': 0x95000,
                    'MEM_SHELL_IMAGE_HEADROOM': 0x20000, 'KHEAP_SIZE': 0x30000}
        assert all(m.get(k) == v for k, v in expected.items())
    else:
        assert syms['__sqlite_start'] == 0x200000
        assert m.get('KHEAP_SIZE') == 0x2c000
    result = generate(ROOT / 'tools/mkvmkernel.py', directory, elf)
    assert result.returncode == 0, result.stderr.decode()
    (directory / 'vmkernel.lz4').write_bytes((directory / 'image.lz4').read_bytes())
    canon = json.loads((ROOT / 'sdk/kapi.json').read_text())
    files = []
    for name in ('kernel.bin', 'sqlite.bin', 'vmkernel.lz4'):
        path = directory / name
        data = path.read_bytes()
        files.append(dict(path=os.path.relpath(path, ROOT), size=len(data),
                          sha256=hashlib.sha256(data).hexdigest()))
    generation = dict(build_id='fixture', kernel_commit='fixture',
                      kapi_version=canon['version'], generations=canon['generations'], files=files)
    (directory / 'generations-manifest.json').write_text(json.dumps(generation))
    # A does not switch the shipped shell consumer (F/G). Model that consumer
    # explicitly in this manifest fixture; the real-tree mirror still rejects it.
    manifest_root = make_tree(directory / 'manifest-root', CLEAN)
    manifest_out = manifest_root / 'build/out'
    for name in ('kernel.elf', 'kernel.map', 'kernel.bin', 'sqlite.bin',
                 'vmkernel.lz4', 'memmap_layout.h'):
        shutil.copy(directory / name, manifest_out / name)
    (manifest_root / 'sdk/kapi.json').write_text(json.dumps(canon))
    for entry in generation['files']:
        entry['path'] = 'build/out/' + Path(entry['path']).name
    (manifest_out / 'generations-manifest.json').write_text(json.dumps(generation))
    if layout:
        shell = manifest_root / 'sdk/os32x_hdr.py'
        shell.write_text(shell.read_text().replace('OS32X_SHELL_LOAD_ADDR = 0x300000',
                                                  'OS32X_SHELL_LOAD_ADDR = 0x400000'))
    manifest = gen.layout_manifest(manifest_root, manifest_out / 'kernel.map',
                                   manifest_out / 'kernel.elf', layout=layout)
    assert not gen.layout_defects(manifest, sections, syms)
    assert manifest['chain_limit']['value'] == m.get('MEM_LAYOUT_LOAD_END')
    for mode in (1 - layout, layout):
        gen.write_layout_header(ROOT, header, mode)
        errors = gen.selected_layout_defects(ROOT, elf, header, layout)
        assert bool(errors) == (mode != layout), errors
        result = generate(ROOT / 'tools/mkvmkernel.py', directory, elf,
                          '--layout-header', header, '--layout', str(layout))
        assert (result.returncode != 0) == (mode != layout), result.stderr.decode()
    original = header.read_text()
    header.write_text(original.replace('#define LAYOUT_MEM_FIXED_PAGING_USED_END ',
                                       '#define LAYOUT_MEM_FIXED_PAGING_USED_END 0x3FB000 + 0 * '))
    assert gen.selected_layout_defects(ROOT, elf, header, layout)
    header.write_text(original)
    broken = edit_elf(elf, directory / 'mixed.elf', 'MEM_KSTACK_TOP', syms['MEM_KSTACK_TOP'] + 4096)
    assert gen.selected_layout_defects(ROOT, broken, header, layout)
    broken = edit_elf(elf, directory / 'bss-omitted.elf', '__bss_end', syms['__bss_end'] - 4)
    result = generate(ROOT / 'tools/mkvmkernel.py', directory, broken)
    assert result.returncode != 0 and b'outside zero range' in result.stderr
    ceiling = m.get('MEM_LAYOUT_LOAD_END')
    assert gen.chain_slack(ceiling, ceiling)['size'] == 0
    try:
        gen.chain_slack(ceiling + 4096, ceiling)
    except ValueError:
        pass
    else:
        raise AssertionError('reversed chain accepted')
    print('layout %d: ELF/header/source, BSS/NOLOAD, fixed values, mixed rejection PASS' % layout)


def check_switch(work):
    out = work / 'switch'
    previous = None
    initial = None
    for mode in (0, 1, 0):
        obj = out / ('obj/layout%d/kernel/kentry.o' % mode)
        targets = [out / 'os32.ld', out / 'r1/os32.ld', obj,
                   out / ('boot/layout%d/loader_fat_new.bin' % mode),
                   out / ('boot/layout%d/vk32_boot.o' % mode)]
        run(['make', '-s', 'MEM_LAYOUT_V5=' + str(mode), 'BUILD_OUT=' + str(out), *targets])
        header = out / 'memmap_layout.h'
        definitions = gen.Macros(gen.read_defines(header), {})
        assert definitions.get('LAYOUT_MEM_LAYOUT_V5') == mode
        stamp = [(p.stat().st_mtime_ns, p.read_bytes()) for p in targets]
        if previous:
            assert all(a[0] > b[0] for a, b in zip(stamp, previous))
            assert stamp[0][1] != previous[0][1]
        # Normal and R1 scripts must differ only in object prefixes.
        normal = targets[0].read_text().replace(str(out / ('obj/layout%d' % mode)) + '/', '')
        r1 = targets[1].read_text().replace(str(out / 'r1/obj') + '/', '')
        assert normal == r1
        if initial is None:
            initial = stamp
        elif mode == 0:
            assert [x[1] for x in stamp] == [x[1] for x in initial]
        previous = stamp
    print('selection 0 -> 1 -> 0: header, normal/R1 ld, kernel ASM, loader ASM/C rebuilt PASS')


def main():
    with tempfile.TemporaryDirectory(prefix='os32-layout-defs-') as tmp:
        work = Path(tmp)
        for layout in (0, 1):
            check_fixture(work, layout)
        check_switch(work)
    return 0


if __name__ == '__main__':
    sys.exit(main())
