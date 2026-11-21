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
    'sc': ('sc_run',),
    'sc-serial': ('sc_run', '--serial'),
    'sc-link': ('sc_run', '--link'),
    'sd': ('sd_run',),
    'sd-link': ('sd_run', '--link'),
    'sh-output': ('ppc_sh_output',),
    'sh-output-serial': ('ppc_sh_output', '--serial'),
    'sh-output-link': ('ppc_sh_output', '--link'),
    'usb': ('usb_run',),
    'usb-link': ('usb_run', '--link', '--lan'),
    'usb-legacy': ('usb_run', '--legacy', '--lan'),
}
SHELL_RUNNERS = ('sb_run', 'sc_run', 'sd_run', 'usb_run', 'ppc_sh_output')
COLUMNS = ('resident_end', 'chain_end', 'frame', 'packed', 'iso_pages')


def check_staging_size(packed, frame):
    if not 0 < packed <= frame <= STAGING_SIZE:
        raise ValueError('compressed root does not fit boot staging area')


def measure(path, fixture=False, iso=None):
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
        if (any(data[off+packed:off+size]) or
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



def shell_images(suite, work, build):
    """Build the acceptance configurations without invoking their guest drivers."""
    from kboot_accept import mutant_settings
    from sb_run import INPUT
    from kboot_run import require

    work.mkdir(parents=True, exist_ok=True)
    if suite == 'sb-serial':
        return sb_images(work, build)
    if suite.startswith('sc'):
        return sc_images(suite, work, build)
    if suite.startswith('usb'):
        return usb_images(suite, work, build)
    if suite.startswith('sd'):
        from sd_run import link_settings
        if suite == 'sd-link':
            for case in ('normal', 'no-boot-follow'):
                folder = work/case
                folder.mkdir()
                build(folder/'kernel', *link_settings(folder, case != 'normal'))
        else:
            opts = ['KBOOT_SH=1', 'KBOOT_LINK=0']
            build(work/'product', *opts)
            folder = work/'no-boot-follow'
            folder.mkdir()
            build(folder/'kernel', *opts, *mutant_settings(
                folder, 'fixed-nonce', 'fs/serialfs/serialfs_persistent.c',
                'nonce = sfs_crc32(token, sizeof(token));', 'nonce = 0; (void)token;'))
        return

    source = 'platform/powermac/console_stream.c'
    def redirect(case):
        return mutant_settings(work, case, source, '    console_write(buf, size, color);',
                               '    (void)color; pm_log_write(buf, size);')

    if suite == 'sh-output-link':
        bins = 'KB_ROOT_BINS=build/out/ppc-user/link-sh/hello.bin build/out/ppc-user/link-sh/fault.bin build/out/ppc-user/link-sh/outprobe.bin'
        opts = ['KBOOT_SH=1', 'KBOOT_LINK=1', bins]
        build(work/'link-normal', *opts)
        build(work/'link-fd1-log', *opts, *redirect('link-fd1-log'))
        return
    src = source
    text = (ROOT / src).read_text()
    before = '    (void)pm_escc_write(&serial, bytes, len);'
    require(text.count(before) == 1, 'stream mirror anchor')
    injected = work / 'console_stream.c'
    if suite == 'sh-output-serial':
        # Mark only the probe's stream write; ordinary CLI output stays intact.
        replacement = ('    if (len == 1 && ((const char *)bytes)[0] == 70)\n'
                       '        (void)pm_escc_write(&serial, "STREAM ", 7);\n' + before)
        injected.write_text(text.replace(before, replacement))
        cli = 'platform/powermac/cli_boot.c'
        cli_text = (ROOT / cli).read_text()
        cli_file = work / 'cli_boot.c'
        commands = INPUT.replace('/bin/hello.bin ok\\n/bin/hello.bin ok\\n/bin/fault.bin\\n/bin/hello.bin ok\\n', '/bin/outprobe.bin\\n')
        cli_file.write_text(cli_text.replace('enum { CLI_CAT_BYTES', commands+'\nenum { CLI_CAT_BYTES')
                            .replace('.getkey = pm_terminal_getkey', '.getkey = sb_getkey'))
    else:
        injected.write_text(text.replace(before, '    (void)serial;'))
    bins = 'KB_ROOT_BINS=build/out/ppc-user/hello.bin build/out/ppc-user/fault.bin build/out/ppc-user/outprobe.bin'
    settings = ['KBOOT_SHELL=1', 'KBOOT_LINK=0', bins,
                'KB_MUT_EXTRA_SRC='+src, 'KB_MUT_EXTRA_FILE='+str(injected)]
    if suite == 'sh-output-serial':
        settings += ['KB_MUT_THIRD_SRC='+cli, 'KB_MUT_THIRD_FILE='+str(cli_file)]
    build(work/'normal', *settings)
    build(work/'fd1-log', *settings, *redirect('fd1-log'))


def sb_images(work, build):
    from sb_run import INPUT, mutant_settings
    source = 'platform/powermac/cli_boot.c'
    text = (ROOT/source).read_text()
    injected = work/'cli_boot.c'
    injected.write_text(text.replace('enum { CLI_CAT_BYTES', INPUT+'\nenum { CLI_CAT_BYTES')
                        .replace('.getkey = pm_terminal_getkey', '.getkey = sb_getkey'))
    settings = ['KBOOT_SHELL=1', 'KBOOT_LINK=0', 'KB_MUT_SRC='+source, 'KB_MUT_FILE='+str(injected)]
    build(work/'normal', *settings)
    mutations = [
        ('exit-status', 'arch/ppc/kapi_call.c',
         'return ppc_kapi_invoke(&api, current_slot, values);',
         'struct kapi_values changed = *values;\n'
         '    if (current_slot == (__builtin_offsetof(KernelAPI, sys_exit) - 8) / 4) changed.value[0] = 0;\n'
         '    return ppc_kapi_invoke(&api, current_slot, &changed);'),
        ('fault-exit', 'arch/ppc/kernel_trap.c',
         '        ring3_fault_kill();',
         '        extern void kapi_sys_exit(int);\n        kapi_sys_exit(0);')]
    for name, src, before, after in mutations:
        extra = mutant_settings(work, name, src, before, after)
        extra = [s.replace('KB_MUT_', 'KB_MUT_EXTRA_') for s in extra]
        build(work/name, *settings, *extra)


def sc_images(suite, work, build):
    import sc_run
    from kboot_accept import mutant_settings
    from link_sh_run import settings as link_settings
    from sc_run import BINS, INPUT

    def parent_result(folder, prefix):
        extra = mutant_settings(folder, 'parent-result', 'arch/ppc/kapi_call.c',
                                'return ppc_kapi_invoke(&api, current_slot, values);',
                                'static unsigned reads;\n'
                                '    if (current_slot == (__builtin_offsetof(KernelAPI, exec_last_result) - 8) / 4 && ++reads == 2) return (u32)-1;\n'
                                '    return ppc_kapi_invoke(&api, current_slot, values);')
        return [s.replace('KB_MUT_', 'KB_MUT_'+prefix+'_') for s in extra]

    if suite == 'sc-link':
        for case in ('normal', 'parent-result'):
            folder = work/case
            folder.mkdir()
            opts = link_settings(folder)
            bins = ' '.join(str(folder/'user'/(name+'.bin')) for name in ('hello', 'fault', 'nest'))
            opts = ['KB_ROOT_BINS='+bins if opt.startswith('KB_ROOT_BINS=') else opt for opt in opts]
            if case == 'parent-result':
                opts.extend(parent_result(folder, 'THIRD'))
            build(folder/'kernel', *opts)
        return
    settings = ['KBOOT_SHELL=1', 'KBOOT_LINK=0', BINS]
    if suite == 'sc-serial':
        source = 'platform/powermac/cli_boot.c'
        text = (ROOT/source).read_text()
        injected = work/'cli_boot.c'
        input_text = INPUT.replace('/bin/hello.bin ok\\n/bin/hello.bin ok\\n/bin/fault.bin\\n/bin/hello.bin ok\\n', '/bin/nest.bin\\n')
        injected.write_text(text.replace('enum { CLI_CAT_BYTES', input_text+'\nenum { CLI_CAT_BYTES')
                            .replace('.getkey = pm_terminal_getkey', '.getkey = sb_getkey'))
        settings += ['KB_MUT_SRC='+source, 'KB_MUT_FILE='+str(injected)]
    build(work/'normal', *settings)
    if suite == 'sc-serial':
        build(work/'parent-result', *settings, *parent_result(work, 'EXTRA'))
    shell = work/'shell'
    shell.mkdir()
    with patch.object(sc_run, 'build', build):
        sc_run.shell_serial(shell, [], suite == 'sc-serial')
    if suite == 'sc':
        build(work/'product', 'KBOOT_SH=1', 'KBOOT_LINK=0')


def usb_images(suite, work, build):
    from usb_run import mutation, product_settings, settings
    cross = 'PPC_CROSS_DIR='+os.environ.get('USB_PPC_CROSS_DIR', os.environ['PPC_CROSS_DIR'])
    if suite == 'usb-link':
        opts = ['KBOOT_SH=1', 'KBOOT_LINK=1', cross]
        build(work/'link-lan-normal', *opts)
        src, dest = mutation(work, 'no-service')
        build(work/'link-lan-no-service', *opts, 'KB_MUT_SRC='+src, 'KB_MUT_FILE='+str(dest))
    elif suite == 'usb-legacy':
        build(work/'lan-normal', 'KBOOT_SH=1', 'KBOOT_LINK=0', cross)
    else:
        for mode in ('normal', 'invert-key', 'no-forward'):
            build(work/('product-'+mode), cross, *product_settings(work, mode))
        build(work/'mouse-wait', cross, *product_settings(work, 'mouse-wait', late=True))
        for mode in ('normal', 'no-service', 'syscall-ee'):
            build(work/mode, cross, *settings(work, mode))


def collect(out, suites):
    sys.path[:0] = [str(ROOT / 'tools/accept'), str(ROOT / 'tools/ppc')]
    import kboot_accept
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    rows = {}
    plans = {}

    def record(elf, name, fixture=False, iso=None):
        values, digest = measure(elf, fixture, iso)
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
        # S-C builds two different families with a case named "normal".
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
        if module_name in SHELL_RUNNERS:
            shell_images(suite, out/suite, build)
            continue
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
        with patch.object(kboot_accept, 'build', build), patch.object(
                sys, 'argv', [module_name, '--build-only', *([] if suite == 'sc' else ['--mutate']), *extra]):
            module = importlib.reload(importlib.import_module(module_name))
            if module.main() or not any(n.startswith(suite+'/') for n in plans):
                raise RuntimeError('incomplete suite '+suite)
    for name, plan in plans.items():
        elf = Path(plan['elf'])
        iso = Path(plan['iso']) if plan['iso'] else None
        # boot_info applies objcopy after make to the image actually booted.
        if name.startswith('boot_info/') and elf.with_name('unverified.elf').exists():
            elf = elf.with_name('unverified.elf')
            iso = elf.with_name('root.iso')
            plan.update(elf=str(elf), iso=str(iso))
        record(elf, name, iso=iso)
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
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')
    collect(args.out.resolve(), args.suite or choices)
