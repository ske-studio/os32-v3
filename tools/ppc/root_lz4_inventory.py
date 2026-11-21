#!/usr/bin/env python3
"""Stage 1 of the PPC root LZ4 design. Measure actual acceptance ELFs with
the root relocated by ld (including downstream alignment), using the same
raw HC12 block and page rounding as the envelope specification.
"""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import shutil
from unittest.mock import patch

import lz4.block
from root_lz4_layout import PAGE, STAGING_BASE, STAGING_SIZE, round_page, segments

ROOT = Path(__file__).resolve().parents[2]

SUITES = ('boot_info console_cells db display display_lifecycle font fp gfx2 kcg '
          'loop mouse newlib path pci port rtc sfs shm sqlite_stack sqlite_static '
          'surface sysinfo ver').split()
# Entry points that generate extra ELFs from the five product configurations.
# Arguments selecting just a transport or a CPU reuse the same build.
RUNNERS = {
    'display': ('ppc_display', '--present'),
    'sysinfo-memstat': ('ppc_sysinfo', '--memstat'),
    'p3': ('link_sh_run',),
    'fb': ('fb_run',),
    'sb-serial': ('sb_run', '--serial'),
    'sc': ('sc_run', '--link'),
    'sd': ('sd_run',),
    'sh-output': ('ppc_sh_output',),
    'usb': ('usb_run',),
}
SHELL_RUNNERS = ('sb_run', 'sc_run', 'sd_run', 'usb_run', 'ppc_sh_output')
# Limiting acceptance cases measured by the complete stage-1 inventory.
REPRESENTATIVES = {'kcg': 'missing', 'port': 'absent', 'font': 'font-absent'}
COLUMNS = ('resident_end', 'chain_end', 'frame', 'packed', 'iso_pages')


def check_staging_size(packed, frame):
    if not 0 < packed <= frame <= STAGING_SIZE:
        raise ValueError('compressed root does not fit boot staging area')


def measure(path, fixture=False, iso=None, damaged_root=False):
    data = path.read_bytes()
    loads = [s for s in segments(data) if s[0] == 1]
    _, _, va, pa, _, memsz, _, _ = loads[0]
    end = pa + memsz
    iso = iso or path.parent / 'root.iso'
    if len(loads) != 1 + iso.exists() or va != pa:
        raise ValueError(f'{path}: unexpected LOADs')
    chain = frame = packed = pages = 0
    if not fixture:
        prefix = Path(os.environ['PPC_CROSS_DIR']) / 'bin/powerpc-elf-nm'
        symbols = {f[2]:int(f[0], 16) for line in subprocess.check_output(
            [prefix, path], text=True).splitlines() if len(f := line.split()) == 3}
        if symbols['__kernel_resident_end'] != end:
            raise ValueError(f'{path}: resident symbol and LOAD disagree')
        chain = symbols['__kernel_chain_end']
    if iso.exists():
        raw = iso.read_bytes()
        _, off, va, pa, size, mem, flags, _ = loads[1]
        if (va, size, flags) != (pa, mem, 4) or va != symbols['__root_image_start']:
            raise ValueError(f'{path}: misplaced root')
        record = loads[0][1] + symbols['__root_record_start'] - loads[0][2]
        packed, length = struct.unpack_from('<II', data, record+8)
        check_staging_size(packed, size)
        if va != STAGING_BASE or chain > STAGING_BASE:
            raise ValueError(f'{path}: misplaced boot staging area')
        frame = size
        if not damaged_root and (any(data[off+packed:off+size]) or
                length != len(raw) or
                lz4.block.decompress(data[off:off+packed], uncompressed_size=length) != raw):
            raise ValueError(f'{path}: wrong compressed root')
        pages = round_page(len(raw)) // PAGE
    # Ignore non-loadable debug/symbol information when merging equivalent images.
    digest = hashlib.sha256(data[24:28])
    for header in loads:
        _, off, *fields = header
        digest.update(struct.pack('>6I', *fields))
        digest.update(data[off:off+header[4]])
    values = dict(zip(COLUMNS, (end, chain, frame, packed, pages)))
    values['low_memsz'] = memsz
    values['low_filesz'] = loads[0][4]
    return values, digest.hexdigest()


def image_table(rows):
    # Order by frame so the high LOAD's limiting images are adjacent.
    return sorted(rows, key=lambda row: (row['frame'], row['names'][0]))


def envelope(rows):
    return {col: max(row[col] for row in rows) for col in COLUMNS}



def collect(out, suites, representative=False):
    sys.path[:0] = [str(ROOT / 'tools/accept'), str(ROOT / 'tools/ppc')]
    import kboot_accept
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    rows = {}
    plans = {}

    def record(elf, name, fixture=False, iso=None, damaged_root=False):
        values, digest = measure(elf, fixture, iso, damaged_root)
        row = rows.setdefault(digest, dict(**values, names=[], sha256=digest))
        row['names'].append(name)
        print(name, *(hex(values[c]) for c in COLUMNS), digest, flush=True)

    def build(folder, *settings):
        folder.mkdir(parents=True, exist_ok=True)
        options = [s for s in settings if '=' in s]
        cmd = ['make', '-j8', '--no-print-directory', 'ARCH=ppc', 'PLATFORM=powermac',
               'KB_OUT='+str(folder), *options]
        if shutil.which('ccache'):
            cmd.append('KB_CC=ccache '+str(Path(os.environ['PPC_CROSS_DIR'])/'bin/powerpc-elf-gcc'))
        linker = folder / 'kernel.ld'
        elf = folder / 'PPCKERN.ELF'
        with (folder / 'layout-build.log').open('w') as log:
            def make(*targets):
                subprocess.run([*cmd, *targets], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
            linker.unlink(missing_ok=True)
            # Let the real Makefile select KB_EXEC, user profile, root bins and
            # manifest, including per-case overrides. The two-argument -f form
            # leaves the normal recursive userland makefiles untouched.
            info = folder/'layout.inputs'
            helper = folder/'layout.mk'
            helper.write_text('.PHONY: layout-inputs\n'
                              'layout-inputs: $(if $(KB_EXEC),$(ROOT_ISO))\n'
                              '\t@$(file >$(KB_OUT)/layout.inputs,$(IMAGE))'
                              '$(file >>$(KB_OUT)/layout.inputs,$(if $(KB_EXEC),$(ROOT_ISO),-)):\n')
            make(str(linker))
            make('-f', 'Makefile', '-f', str(helper), 'layout-inputs')
            image, iso_path = info.read_text().splitlines()
            iso = None
            if iso_path != '-':
                iso = folder/'root.iso'
                cmd.append('--old-file='+iso_path)
            # A recursive kb-user make reads the outer tree's dependency
            # files. Finish it before compiler jobs start rewriting those files.
            prerequisites = [s for s in settings if '=' not in s and s != 'kernel']
            if prerequisites:
                make(*prerequisites)
            make(str(elf))
        case = folder.parent.name if folder.name == 'kernel' else folder.name
        name = suite+'/'+case
        # Disambiguate cases that build multiple image profiles.
        if name in plans:
            name = suite+'/'+image+'/'+case
        plans[name] = dict(args=settings, elf=str(elf), iso=str(iso) if iso else None)
        return elf

    for suite in suites:
        if suite == 'product':
            for image in sorted((ROOT/'build/images').glob('kboot-*.mk')):
                build(out/image.stem, 'IMAGE='+image.stem)
            continue
        if suite == 'kboot':
            from kboot_mutations import KERNEL_NEGATIVES, KERNEL_MUTATIONS
            for name, number in KERNEL_NEGATIVES.items():
                build(out/suite/name, 'KBOOT_LINK=0', 'NEGATIVE='+str(number))
            for name, source, old, new, *_ in KERNEL_MUTATIONS:
                folder = out/suite/name
                folder.mkdir(parents=True, exist_ok=True)
                build(folder, 'KBOOT_LINK=0', *kboot_accept.mutant_settings(folder, name, source, old, new))
            continue
        module_name, *extra = RUNNERS.get(suite, ('ppc_'+suite,))
        if suite == 'p3':
            extra.extend(['--out', str(out/suite)])
        if suite == 'sqlite_stack':
            mod = importlib.import_module(module_name)
            normal = out/suite/'normal'
            normal.mkdir(parents=True)
            objects = {n: mod.compile_object(normal, n, ROOT/n) for n in mod.SOURCES}
            mod.link_image(normal, objects)
            record(normal/'SSTACK.ELF', suite+'/normal', True)
            for case, src, before, after in mod.MUTATIONS:
                folder = normal.parent/case
                folder.mkdir()
                source = folder/Path(src).name
                original = (ROOT/src).read_text()
                if original.count(before) != 1:
                    raise ValueError(case)
                source.write_text(original.replace(before, after))
                mod.link_image(folder, {**objects, src: mod.compile_object(folder, src, source)})
                record(folder/'SSTACK.ELF', suite+'/'+case, True)
            continue
        if representative:
            extra.extend(['--case', REPRESENTATIVES[suite]])
        with patch.object(kboot_accept, 'build', build), patch.object(
                sys, 'argv', [module_name, '--build-only', '--mutate', *extra]):
            module = importlib.reload(importlib.import_module(module_name))
            def boot_image(case, elf, iso):
                name = suite+'/'+case
                plans.setdefault(name, {}).update(elf=str(elf), iso=str(iso),
                    damaged_root=case in ('invalid-offset', 'short-output', 'data-crc', 'allocation-failed'))

            status = module.main(on_image=boot_image) if suite == 'boot_info' else module.main()
            if status or not any(n.startswith(suite+'/') for n in plans):
                raise RuntimeError('incomplete suite '+suite)
    for name, plan in plans.items():
        elf = Path(plan['elf'])
        iso = Path(plan['iso']) if plan['iso'] else None
        record(elf, name, iso=iso, damaged_root=plan.get('damaged_root', False))
    # Equivalence across suites is expected. Within these shell/USB suites
    # each enumerated case changes product code or embedded input.
    for row in rows.values():
        families = [name.split('/')[0] for name in row['names']]
        for family in set(families):
            if family in RUNNERS and RUNNERS[family][0] in SHELL_RUNNERS and families.count(family) > 1:
                raise ValueError(f'acceptance variants have identical LOADs: {row["names"]}')
    result = image_table(rows.values())
    recorded = {name for row in result for name in row['names']}
    if set(plans) - recorded:
        raise ValueError('missing measured images: ' + ', '.join(sorted(set(plans) - recorded)))
    (out/'plans.json').write_text(json.dumps(plans, indent=2))
    (out/'inventory.json').write_text(json.dumps(result, indent=2))
    print('image resident_end chain_end frame packed iso_pages')
    for row in result:
        print(','.join(row['names']), *(hex(row[c]) for c in COLUMNS[:-1]), row['iso_pages'])
    maxima = envelope(result)
    # Reserve one more 64 KiB alignment step for unmeasured acceptance images.
    from root_lz4_layout import layout_constant
    if maxima['chain_end'] + (0x10000 if representative else 0) > layout_constant('MEM_FIXED_PAGING_GUARD_BASE'):
        raise ValueError('kernel chain exceeds fixed paging guard')
    (out/'maxima.json').write_text(json.dumps({
        col: dict(value=value, images=[name for row in result if row[col] == value for name in row['names']])
        for col, value in maxima.items()}, indent=2))

    print('MAX', ' '.join(f"{col}={maxima[col]:#x}({next(r['names'][0] for r in result if r[col] == maxima[col])})"
                          for col in COLUMNS))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    choices = list(dict.fromkeys(['product', *SUITES, *RUNNERS, 'kboot']))
    parser.add_argument('--suite', action='append', choices=choices, help='measure one family (repeatable)')
    parser.add_argument('--representative', action='store_true')
    args = parser.parse_args()
    if args.representative and args.suite:
        parser.error('--representative selects its own suites')
    args.out.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')
    suites = ['product', *REPRESENTATIVES] if args.representative else args.suite or choices
    collect(args.out.resolve(), suites, args.representative)
