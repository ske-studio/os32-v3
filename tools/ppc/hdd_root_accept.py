"""TFTP-stage root selection, using a firmware-loaded ELF and two disposable disks."""
import argparse
import hashlib
import os
import shutil
from pathlib import Path
import struct
import subprocess
import tempfile

from ata_accept import guest
from kboot_accept import build_media, mutant_settings
from run import ROOT, ROM

START, LENGTH = 64, 8192
VIEW = 'platform/powermac/partition_view.c'
KERNEL = 'platform/powermac/ppc_kernel.c'


def disk(work, name, populate=None):
    directory = work / name
    directory.mkdir()
    (directory / 'identity').write_text(name)
    if populate:
        populate(directory)
    fs = work / (name + '.ext2')
    subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '128',
                    '-O', 'none', '-d', directory, fs, str(LENGTH // 2)], check=True)
    raw = bytearray([0xa5]) * ((START + LENGTH + 1) * 512)
    struct.pack_into('>HHI', raw, 0, 0x4552, 512, len(raw) // 512)
    for i, (start, size, kind) in enumerate(((1, 8, 'Apple_partition_map'),
                                             (9, 32, 'Apple_HFS'),
                                             (START, LENGTH, 'Apple_UNIX_SVR2')), 1):
        entry = bytearray(512)
        struct.pack_into('>HHIII', entry, 0, 0x504d, 0, 3, start, size)
        entry[48:48+len(kind)] = kind.encode()
        raw[i*512:(i+1)*512] = entry
    raw[START*512:-512] = fs.read_bytes()
    path = work / (name + '.raw')
    path.write_bytes(raw)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').exists() or not shutil.which('mke2fs') or (
            not args.build_only and (not ROM.exists() or not shutil.which('genisoimage')
                                     or not shutil.which('qemu-system-ppc'))):
        print('SKIP: missing PowerPC toolchain or HDD acceptance tools')
        return 3
    work = Path(tempfile.mkdtemp(prefix='hdd-root-'))
    first, second = disk(work, 'first'), disk(work, 'selected')
    tree = 'platform/powermac/ide_tree.c'
    apm = 'platform/powermac/apm.c'
    rootarg = 'os32-root=/pci@f2000000/mac-io@c/ata-3@20000/disk@1:3'
    no_ext2 = work / 'hfs.raw'
    data = bytearray(second.read_bytes())
    data[3*512+48:3*512+80] = b'Apple_HFS'.ljust(32, b'\0')
    no_ext2.write_bytes(data)
    oversized = work / 'oversized.raw'
    data = bytearray(second.read_bytes())
    struct.pack_into('<I', data, START*512 + 1024 + 4, LENGTH // 2 + 1)
    oversized.write_bytes(data)
    cases = [('normal', VIEW, None, None)]
    if args.mutate:
        cases += [('start', VIEW, 'first + offset', 'offset'),
                  ('last', VIEW, 'count > dev->total_sects - offset', 'count >= dev->total_sects - offset'),
                  ('resolve-last', VIEW, 'count > dev->total_sects-lba', 'count >= dev->total_sects-lba'),
                  ('type', apm, 'i + 1 == number && kstrncmp', '0 && kstrncmp'),
                  ('duplicate', tree, 'root_controller = 0; return;', 'return;'),
                  ('unit', tree, 'previous * 2 + root_target', 'previous * 2 + (1 - root_target)')]
    hashes = set()
    for name, target, old, new in cases:
        out = work / name
        out.mkdir()
        code = '#include "dev.h"\n' + (ROOT / KERNEL).read_text()
        decl = 'void platform_boot_run(void)'
        call = '        pm_exec_boot();'
        assert code.count(decl) == code.count(call) == 1
        probe = (ROOT / 'tools/ppc/hdd_root_diag.c').read_text()
        source = out / 'kernel.c'
        select = '            kboot_stop("hd0 select");'
        assert code.count(select) == 1
        code = code.replace(select, '            kprintf(7, "ROOT-TEST select hd0=%d\\r\\n", dev_find("hd0") != 0);\n' + select)
        source.write_text(code.replace(decl, probe + '\n' + decl).replace(call, '        hdd_root_report();\n' + call))
        settings = ['KB_MUT_EXTRA_SRC=' + KERNEL, 'KB_MUT_EXTRA_FILE=' + str(source)]
        if old:
            settings += mutant_settings(out, name, target, old, new)
        boot = build_media(out, '-j6', 'IMAGE=kboot-link-sh', *settings)
        digest = hashlib.sha256(boot.elf.read_bytes()).hexdigest()
        assert digest not in hashes
        hashes.add(digest)
        print(name, digest, flush=True)
        if args.build_only:
            continue
        trials = {
            'valid': (second, rootarg, 'ROOT-TEST done'),
            'oversized': (oversized, rootarg, 'FATAL kboot hd0 root'),
            'hfs': (no_ext2, rootarg, 'FATAL kboot hd0 select'),
            'wrong-number': (second, rootarg[:-1] + '2', 'FATAL kboot hd0 select'),
            'duplicate': (second, rootarg + ' ' + rootarg, 'FATAL kboot hd0 select'),
            'unresolved': (second, rootarg.replace('mac-io@c', 'missing@c'), 'FATAL kboot hd0 select'),
        }
        runs = list(trials) if name == 'normal' else [
            {'type': 'hfs', 'duplicate': 'duplicate'}.get(name, 'valid')]
        for trial in runs:
            media, bootargs, stop = trials[trial]
            wanted = stop
            if name == 'start':
                stop = 'FATAL kboot hd0 root'
            elif name in ('type', 'duplicate'):
                stop = 'ROOT-TEST done'
            directory = out / trial
            directory.mkdir()
            lines = guest(boot, '7450', first, second=media, done=stop, directory=directory, bootargs=bootargs)
            expected = bytearray(media.read_bytes())
            if stop == 'ROOT-TEST done':
                identity = 'first' if name == 'unit' else 'selected'
                assert f'ROOT-TEST dev=hd0 id={identity} bytes={len(identity)}' in lines
                if identity == 'selected':
                    rc = -4 if name in ('last', 'resolve-last') else 0
                    assert f'ROOT-TEST last={rc} beyond=-4' in lines
                    if not rc:
                        expected[-1024:-512] = bytes([0x73]) * 512
            elif stop == 'FATAL kboot hd0 root':
                rc = -13 if trial == 'oversized' else -2
                assert f'HDD root hd0 ext2_mount rc={rc}' in lines
                if trial == 'oversized':
                    assert '[EXT2] fs (4097 blocks) exceeds partition (8192 sectors)' in lines
            elif stop == 'FATAL kboot hd0 select':
                assert 'ROOT-TEST select hd0=0' in lines
                assert not any('ROOT-TEST dev=' in line for line in lines)
            assert (directory / '7450/disk.raw').read_bytes() == first.read_bytes()
            assert (directory / '7450/second.raw').read_bytes() == expected
            if name != 'normal':
                assert stop != wanted or name in ('unit', 'last', 'resolve-last')
                print(name, 'RED', flush=True)
    print(f'HDD root {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    raise SystemExit(main())
