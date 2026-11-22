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
from kboot_accept import build, mutant_settings
from run import ROOT, ROM

START, LENGTH = 64, 8192
VIEW = 'platform/powermac/partition_view.c'
KERNEL = 'platform/powermac/ppc_kernel.c'


def disk(work, name):
    directory = work / name
    directory.mkdir()
    (directory / 'identity').write_text(name)
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
    cases = [('normal', None, None)]
    if args.mutate:
        cases += [('start', 'first + offset', 'offset'),
                  ('last', 'count > dev->total_sects - offset', 'count >= dev->total_sects - offset')]
    hashes = set()
    for name, old, new in cases:
        out = work / name
        out.mkdir()
        code = (ROOT / KERNEL).read_text()
        decl = 'void platform_boot_run(void)'
        call = '        pm_exec_boot();'
        assert code.count(decl) == code.count(call) == 1
        probe = (ROOT / 'tools/ppc/hdd_root_diag.c').read_text()
        source = out / 'kernel.c'
        source.write_text(code.replace(decl, probe + '\n' + decl).replace(call, '        hdd_root_report();\n' + call))
        settings = ['KB_MUT_EXTRA_SRC=' + KERNEL, 'KB_MUT_EXTRA_FILE=' + str(source)]
        if old:
            settings += mutant_settings(out, name, VIEW, old, new)
        elf = build(out, '-j6', 'IMAGE=kboot-link-sh', *settings)
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert digest not in hashes
        hashes.add(digest)
        print(name, digest, flush=True)
        if args.build_only:
            continue
        stop = 'FATAL kboot hd0 root' if name == 'start' else 'ROOT-TEST done'
        lines = guest(elf, '7450', first, second=second, done=stop,
                      bootargs='os32-root=/pci@f2000000/mac-io@c/ata-3@20000/disk@1:3')
        expected = bytearray(second.read_bytes())
        if name != 'start':
            assert 'ROOT-TEST dev=hd0 id=selected bytes=8' in lines, lines[-12:]
            rc = -4 if name == 'last' else 0
            assert f'ROOT-TEST last={rc} beyond=-4' in lines
            if not rc:
                expected[-1024:-512] = bytes([0x73]) * 512
        assert (out / '7450/disk.raw').read_bytes() == first.read_bytes()
        assert (out / '7450/second.raw').read_bytes() == expected
    print(f'HDD root {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    raise SystemExit(main())
