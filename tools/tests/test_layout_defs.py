#!/usr/bin/env python3
"""T3ab-A: product linker/header agreement, mixed inputs and incremental selection.

No emulator and no mutation pass. Deliberately malformed input artifacts exercise
rejection, paired with a valid control for both selections.
"""
import os
import argparse
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


EXPECTED = {'MEM_DMA_POOL_BASE': 0x7f0000, 'MEM_FIXED_PAGING_GUARD_BASE': 0x3e0000,
            'MEM_FIXED_PAGING_BASE': 0x3e1000, 'MEM_FIXED_PAGING_USED_END': 0x3ec000,
            'MEM_FIXED_PAGING_FRAME_END': 0x3fb000, 'MEM_STACK_GUARD': 0x3fb000,
            'MEM_KSTACK_BASE': 0x3fc000, 'MEM_SHELL_LOAD_ADDR': 0x400000,
            'MEM_POOL_BASE': 0x500000, 'MEM_SHELL_HEAP_BASE': 0x4b5000,
            'MEM_SHELL_HEAP_SIZE': 0x40000, 'MEM_SHELL_GUARD': 0x4f5000,
            'MEM_SHELL_STACK_SIZE': 0xa000, 'MEM_SHELL_MAX_SIZE': 0x95000,
            'MEM_SHELL_IMAGE_HEADROOM': 0x20000, 'KHEAP_SIZE': 0x30000}

def run(cmd):
    result = subprocess.run([str(x) for x in cmd], cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


def check_fixture(work):
    directory = work / 'layout'
    elf = fixture(directory, core_bss=33, sqlite_bss=35)
    header = directory / 'memmap_layout.h'
    gen.write_layout_header(ROOT, header)
    assert not gen.layout_header_defects(ROOT, elf, header)
    sections, syms = gen.read_elf(elf)
    m = gen.Macros(gen.read_defines(ROOT / 'include/memmap.h'), syms)
    for lo, hi in (('__bss_start', '__bss_end'), ('__sqlite_data_end', '__sqlite_end')):
        assert syms[lo] <= syms[hi] and (syms[lo] | syms[hi]) & 3 == 0
    for s in sections:
        if s['noload']:
            lo, hi = ('__sqlite_data_end', '__sqlite_end') if s['name'].startswith('.sqlite') else ('__bss_start', '__bss_end')
            assert syms[lo] <= s['start'] <= s['end'] <= syms[hi]
    assert syms['__bss_end'] == syms['__ledger_fixed_end']
    assert syms['__ledger_fixed_end'] - syms['__ledger_fixed_start'] == 8192
    assert syms['__sqlite_start'] == syms['__bss_end']
    assert syms['__kernel_resident_end'] == m.get('MEM_SQLITE_STACK_BASE') + 131072
    assert m.get('KHEAP_BASE') == syms['__kernel_resident_end']
    assert all(m.get(k) == v for k, v in EXPECTED.items())
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
    manifest_root = make_tree(directory / 'manifest-root', CLEAN)
    manifest_out = manifest_root / 'build/out'
    for name in ('kernel.elf', 'kernel.map', 'kernel.bin', 'sqlite.bin',
                 'vmkernel.lz4', 'memmap_layout.h'):
        shutil.copy(directory / name, manifest_out / name)
    (manifest_root / 'sdk/kapi.json').write_text(json.dumps(canon))
    for entry in generation['files']:
        entry['path'] = 'build/out/' + Path(entry['path']).name
    (manifest_out / 'generations-manifest.json').write_text(json.dumps(generation))
    manifest = gen.layout_manifest(manifest_root, manifest_out / 'kernel.map',
                                   manifest_out / 'kernel.elf')
    assert not gen.layout_defects(manifest, sections, syms)
    assert manifest['chain_limit']['value'] == m.get('MEM_LAYOUT_LOAD_END')
    original = header.read_text()
    header.write_text(original.replace('#define LAYOUT_MEM_FIXED_PAGING_USED_END ',
                                       '#define LAYOUT_MEM_FIXED_PAGING_USED_END 0x3FB000 + 0 * '))
    assert gen.layout_header_defects(ROOT, elf, header)
    header.write_text(original)
    broken = edit_elf(elf, directory / 'mixed.elf', 'MEM_KSTACK_TOP', syms['MEM_KSTACK_TOP'] + 4096)
    assert gen.layout_header_defects(ROOT, broken, header)
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
    print('ELF/header/source, BSS/NOLOAD, fixed values, mixed rejection PASS')


def check_generated(work):
    out = work / 'generated'
    targets = [out / 'os32.ld', out / 'r1/os32.ld', out / 'obj/kernel/kentry.o',
               out / 'boot/loader_fat_new.bin', out / 'boot/vk32_boot.o', out / 'app_sys.ld']
    run(['make', '-s', 'BUILD_OUT=' + str(out), *targets])
    normal = targets[0].read_text().replace(str(out / 'obj') + '/', '')
    r1 = targets[1].read_text().replace(str(out / 'r1/obj') + '/', '')
    assert normal == r1
    for path in [out / 'memmap_layout.h', targets[0], targets[1], targets[-1]]:
        assert not retired_names(path.read_text()), path
    print('normal/R1 ld, kernel ASM, loader ASM/C, resident ld generated PASS')


def retired_names(source):
    # Build the retired selector spelling here so this test is not its own exception.
    return any(name in source for name in ('MEM_LAYOUT_' + 'V5',
                                          'MEM_KERNEL_RESV_START', 'MEM_KERNEL_RESV_END'))


def check_retired():
    paths = run(['git', 'ls-files']).splitlines()
    for name in paths:
        path = ROOT / name
        if path.suffix in ('.c', '.h', '.asm', '.S', '.ld', '.mk') and path.is_file():
            assert not retired_names(path.read_text()), name
    for name in ('tools/gen_memmap.py', 'tools/mkvmkernel.py'):
        assert not retired_names((ROOT / name).read_text()), name
    print('retired product selector/reservation names absent PASS')


def check_product(outputs):
    for rel in outputs:
        out = ROOT / rel
        _, symbols = gen.read_elf(out / 'kernel.elf')
        header = out / 'memmap_layout.h' if (out / 'memmap_layout.h').exists() else out.parent / 'memmap_layout.h'
        assert not gen.layout_header_defects(ROOT, out / 'kernel.elf', header)
        m = gen.Macros(gen.read_defines(ROOT / 'include/memmap.h'), symbols)
        assert all(m.get(k) == v for k, v in EXPECTED.items()), rel
        assert m.get('MEM_SHM_RESV_START') <= m.get('MEM_FIXED_PAGING_GUARD_BASE')
        assert symbols['__ledger_fixed_end'] == symbols['__bss_end'] == symbols['__sqlite_start']
        assert (out / 'vmkernel.lz4').stat().st_size <= 508 * 1024
    print('product ELF matches fixture fixed values, resident/SLACK and compressed budget PASS:', ', '.join(outputs))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--product-out', action='append', help='built product directory (default: build/out)')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='os32-layout-defs-') as tmp:
        work = Path(tmp)
        check_fixture(work)
        check_generated(work)
        check_retired()
        check_product(args.product_out or ['build/out'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
