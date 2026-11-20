"""ATA IDENTIFY words 27–46/60–61, docs/ppc/ata.md §3. The 1 MiB raw disk
defines the sector count and every byte independently of the guest driver.
QEMU mac99's IDE disk model is QEMU HARDDISK (read-only ATA probe)."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from kboot_accept import build, mutant_settings
from run import ROOT, ROM

BOOT = 'platform/powermac/ide_boot.c'
BUS = 'platform/powermac/platform_ide_bus.h'
TREE = 'platform/powermac/ide_tree.c'
DRIVER = 'drivers/ata/ide.c'
MUTANTS = ('word', 'bar', 'wait', 'lba')


def make_image(work, case):
    out = work / case
    out.mkdir()
    source = (ROOT / BOOT).read_text()
    diag = (ROOT / 'tools/ppc/ata_accept_diag.c').read_text()
    source = source.replace('void pm_ide_boot(void)', diag + '\nvoid pm_ide_boot(void)')
    end = 'if (ide_drive_present(i)) dev_register_hdd(i);'
    assert source.count(end) == 1
    boot = out / 'boot.c'
    boot.write_text(source.replace(end, end + '\n    ata_accept_report();'))
    settings = ['IMAGE=kboot-sh', 'KB_MUT_EXTRA_SRC=' + BOOT, 'KB_MUT_EXTRA_FILE=' + str(boot)]
    if case == 'word':
        before = '__builtin_bswap16(ppc_load16(ide_window))'
        header = (ROOT / BUS).read_text()
        assert header.count(before) == 1
        (out / 'platform_ide_bus.h').write_text(header.replace(before, 'ppc_load16(ide_window)'))
    if case == 'bar':
        settings += mutant_settings(out, case, TREE, 'node, base + offset, bytes', 'node, base, bytes')
    if case == 'wait':
        settings += mutant_settings(out, case, DRIVER, 'int timeout = IDE_TIMEOUT_BSY;', 'int timeout = 0;')
    if case == 'lba':
        path = 'drivers/base/dev.c'
        settings += mutant_settings(out, case, path, 'ide_read_sectors((int)self->bus_id, (u32)lba,',
                                    'ide_read_sectors((int)self->bus_id, (u32)lba + 1,')
    return build(out, '-j6', *settings)


def sector(lba):
    return bytes((i * 19 + (i >> 3) + lba * 7 + (lba >> 8)) & 255 for i in range(512))


def digest(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return f'{h:08x}'


def guest(elf, cpu, disk, absent=False):
    work = elf.parent / (cpu + ('-absent' if absent else ''))
    work.mkdir()
    cd = work / 'cd'
    cd.mkdir()
    shutil.copy(elf, cd)
    iso = work / 'ata.iso'
    subprocess.run(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop',
                    '-o', iso, cd], check=True)
    serial = work / 'serial.log'
    args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-smp', '1',
            '-accel', 'tcg', '-bios', ROM, '-display', 'none', '-serial', 'file:' + str(serial),
            '-serial', 'null', '-monitor', 'none', '-no-reboot', '-nic', 'none',
            '-drive', f'file={iso},format=raw,if=ide,index=2,media=cdrom', '-boot', 'd',
            '-prom-env', r'boot-device=cd:,\PPCKERN.ELF', '-prom-env', 'auto-boot?=true']
    if not absent:
        args += ['-drive', f'file={disk},format=raw,if=ide,index=0,media=disk,snapshot=on']
    with (work / 'qemu.log').open('wb') as log, subprocess.Popen(args, stderr=log, stdout=log) as proc:
        try:
            deadline = time.monotonic() + 90
            while proc.poll() is None and time.monotonic() < deadline:
                lines = serial.read_bytes().decode(errors='replace').splitlines() if serial.exists() else []
                if 'ATA-TEST done' in lines or 'ATA-TEST absent=1 registered=0' in lines:
                    return lines
                time.sleep(.15)
            raise RuntimeError(f'guest stopped or timed out: {serial}')
        finally:
            proc.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file() or (not args.build_only and
            (not ROM.is_file() or not all(shutil.which(t) for t in ('qemu-system-ppc', 'genisoimage')))):
        print('SKIP: PowerPC toolchain or mac99 runtime unavailable')
        return 3
    work = Path(tempfile.mkdtemp(prefix='ata-accept-'))
    disk = work / 'disk.raw'
    disk.write_bytes(b''.join(sector(i) for i in range(2048)))
    hashes = set()
    for case in ('normal', *(MUTANTS if args.mutate or args.build_only else ())):
        elf = make_image(work, case)
        sha = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert sha not in hashes, case
        hashes.add(sha)
        print(case, sha, elf, flush=True)
        if args.build_only:
            continue
        for cpu in ('7450', '7455'):
            lines = guest(elf, cpu, disk)
            ident = 'ATA-TEST model=QEMU HARDDISK sectors=2048' in lines
            missing = 'ATA-TEST absent=1 registered=0'
            if case == 'normal':
                assert ident and all(f'ATA-TEST lba={i} rc=0 hash={digest(sector(i))}' in lines
                                     for i in (0, 1024, 2047)), lines[-8:]
                assert missing in guest(elf, cpu, disk, absent=True)
            elif case == 'lba':
                assert ident and f'ATA-TEST lba=0 rc=0 hash={digest(sector(1))}' in lines
            elif case == 'word':
                assert not ident and (missing in lines or any(s.startswith('ATA-TEST model=') for s in lines))
            else:
                assert missing in lines
    print(f'ATA acceptance {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    raise SystemExit(main())
