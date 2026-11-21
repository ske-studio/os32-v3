"""ATA IDENTIFY words 27–46/60–61, docs/ppc/ata.md §3. The 1 MiB raw disk
defines the sector count and every byte independently of the guest driver.
ATA_HAL_DESIGN §6-4 P2 requires odd-address public KAPI writes, range and
child denial, and unchanged neighbouring sectors. The host checks every byte."""
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
WRITE_MUTANTS = ('write-word', 'write-lba', 'write-count')
WRITE_CASES = ('write', 'deny', *WRITE_MUTANTS)
MUTANTS = ('word', 'bar', 'wait', 'lba')


def make_write_image(out, case):
    kernel = 'platform/powermac/ppc_kernel.c'
    diag = (ROOT / 'tools/ppc/ata_write_diag.c').read_text()
    source = (ROOT / kernel).read_text()
    entry = 'void platform_boot_run(void)'
    anchor = '        pm_exec_boot();'
    assert source.count(entry) == source.count(anchor) == 1
    source = source.replace(entry, diag + '\n' + entry)
    call = f'        ata_write_report({int(case == "deny")});'
    kernel_copy = out / 'boot.c'
    kernel_copy.write_text(source.replace(anchor, '\n'.join((anchor, call,
                           '        kprintf(7, "ATA-TEST done\\r\\n");'))))
    settings = ['KB_MUT_EXTRA_SRC=' + kernel, 'KB_MUT_EXTRA_FILE=' + str(kernel_copy)]
    if case == 'write-word':
        header = (ROOT / BUS).read_text()
        word = '((u16)p[0] << 8) | p[1]'
        assert header.count(word) == 1
        (out / 'platform_ide_bus.h').write_text(header.replace(word, '((u16)p[1] << 8) | p[0]'))
    if case in ('write-lba', 'write-count'):
        device = 'kernel/dev/device_ide.c'
        before, after = (('ide_write_sector(drv, lba,', 'ide_write_sector(drv, lba + 1,')
                         if case == 'write-lba' else
                         ('ide_write_sectors(drv, lba, cnt,', 'ide_write_sectors(drv, lba, cnt > 1 ? cnt - 1 : cnt,'))
        settings += mutant_settings(out, case, device, before, after)
    if case == 'deny':
        # Depth zero must first load a resident image in the shell band.
        # Its exec_run then gives the probe an ordinary USER child slot.
        resident = 'userland/shell/main.c'
        entry = 'int main(int argc, char **argv, KernelAPI *api)\n{'
        launcher = (ROOT / resident).read_text()
        assert launcher.count(entry) == 1
        path = out / 'launcher.c'
        path.write_text(launcher.replace(entry, entry + '\n    return api->exec_run("/bin/atadeny.bin");'))
        user = out / 'user'
        settings += ['KB_SH_OUT=' + str(user), 'KB_ROOT_BINS=' + str(user / 'atadeny.bin'),
                     'PPC_RES_MUT_SRC=' + resident, 'PPC_RES_MUT_FILE=' + str(path)]
    return build(out, '-j6', 'IMAGE=kboot-sh', *settings)


def make_image(work, case):
    out = work / case
    out.mkdir()
    if case in WRITE_CASES:
        return make_write_image(out, case)
    source = (ROOT / BOOT).read_text()
    diag = (ROOT / 'tools/ppc/ata_accept_diag.c').read_text()
    source = source.replace('void pm_ide_boot(void)', diag + '\nvoid pm_ide_boot(void)')
    end = 'if (ide_drive_present(i)) dev_register_hdd(i);'
    assert source.count(end) == 1
    boot = out / 'boot.c'
    source = source.replace(end, end + '\n    ata_accept_report();')
    if case in ('normal', 'wait'):
        source = source.replace('    ide_init();', '    extern void ata_check_wait(void);\n'
                                '    ata_check_wait();\n    ide_init();')
    if case in ('normal', 'bar'):
        # Observe the first resolved window before touching controller registers.
        report = ('    extern u32 ata_first_pa;\n'
                  '    kprintf(7, "ATA-TEST pa=%08x\\r\\n", ata_first_pa);\n')
        if case == 'bar':
            report += '    kprintf(7, "ATA-TEST done\\r\\n");\n    return;\n'
        source = source.replace('    ide_init();', report + '    ide_init();')
    boot.write_text(source)
    settings = ['IMAGE=kboot-sh', 'KB_MUT_EXTRA_SRC=' + BOOT, 'KB_MUT_EXTRA_FILE=' + str(boot)]
    if case == 'word':
        before = '__builtin_bswap16(ppc_load16(ide_window))'
        header = (ROOT / BUS).read_text()
        assert header.count(before) == 1
        (out / 'platform_ide_bus.h').write_text(header.replace(before, 'ppc_load16(ide_window)'))
    if case in ('normal', 'bar'):
        tree = (ROOT / TREE).read_text()
        if case == 'bar':
            address = 'node, base + offset, bytes'
            assert tree.count(address) == 1
            tree = tree.replace(address, 'node, base, bytes')
        capture = 'walk(ofw_child(root), root, 0, 0, &left, &bank, pa);'
        assert tree.count(capture) == 1
        tree = tree.replace(capture, capture + '\n    ata_first_pa = pa[0];')
        path = out / 'tree.c'
        path.write_text('#include "ide_boot.h"\nu32 ata_first_pa;\n' + tree)
        settings += ['KB_MUT_THIRD_SRC=' + TREE, 'KB_MUT_THIRD_FILE=' + str(path)]
    if case in ('normal', 'wait'):
        # Feed BSY then ready to the actual polling loop during the diagnostic.
        before = 'static int ide_wait_bsy(void)'
        driver = (ROOT / DRIVER).read_text()
        start = driver.index(before)
        stop = driver.index('\n}', start) + 2
        polling = driver[start:stop]
        assert polling.count('ide_bus_alt()') == 1
        instrumented = polling.replace('ide_bus_alt()', '(probe ? '
                                       '(++reads == 1 ? IDE_ST_BSY : 0) : ide_bus_alt())')
        if case == 'wait':
            assert instrumented.count('int timeout = IDE_TIMEOUT_BSY;') == 1
            instrumented = instrumented.replace('int timeout = IDE_TIMEOUT_BSY;', 'int timeout = 1;')
        settings += mutant_settings(out, case, DRIVER, polling,
                                    'static unsigned probe, reads;\n' + instrumented + """
void ata_check_wait(void)
{
    probe = 1;
    int rc = ide_wait_bsy();
    probe = 0;
    kprintf(7, "ATA-TEST wait reads=%u rc=%d\\r\\n", reads, rc);
}
""")
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
        media = work / 'disk.raw'
        shutil.copyfile(disk, media)
        args += ['-drive', f'file={media},format=raw,if=ide,index=0,media=disk,cache=writethrough']
    with (work / 'qemu.log').open('wb') as log, subprocess.Popen(args, stderr=log, stdout=log) as proc:
        try:
            deadline = time.monotonic() + 90
            while proc.poll() is None and time.monotonic() < deadline:
                lines = serial.read_bytes().decode(errors='replace').splitlines() if serial.exists() else []
                if 'ATA-TEST done' in lines:
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
    cases = ('normal', 'write', 'deny')
    if args.mutate or args.build_only:
        cases += MUTANTS + WRITE_MUTANTS
    for case in cases:
        elf = make_image(work, case)
        sha = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert sha not in hashes, case
        hashes.add(sha)
        print(case, sha, elf, flush=True)
        if args.build_only:
            continue
        for cpu in ('7450', '7455'):
            lines = guest(elf, cpu, disk)
            if case in WRITE_CASES:
                expected = bytearray(disk.read_bytes())
                if case == 'deny':
                    assert 'ATA-TEST child=73' in lines, lines[-10:]
                else:
                    range_rc = 0 if case == 'write-count' else -4
                    assert f'ATA-TEST writes=0,0 range={range_rc}' in lines, lines[-10:]
                    payload = bytes((i * 37 + (i >> 8) + 13) & 255 for i in range(1024))
                    if case == 'write-word':
                        payload = bytes(payload[i ^ 1] for i in range(1024))
                    first = 33 if case == 'write-lba' else 32
                    count = 1 if case == 'write-count' else 2
                    expected[first*512:(first+1)*512] = payload[:512]
                    expected[64*512:(64+count)*512] = payload[:count*512]
                    if case == 'write-count':
                        expected[-512:] = payload[:512]
                    for n in range(3):
                        equal = (case != 'write-word' and not (case == 'write-lba' and n == 0)
                                 and not (case == 'write-count' and n == 2))
                        assert f'ATA-TEST read={n} equal={int(equal)}' in lines
                assert (elf.parent / cpu / 'disk.raw').read_bytes() == expected, case
                continue
            ident = 'ATA-TEST model=QEMU HARDDISK sectors=2048' in lines
            if case in ('normal', 'bar'):
                # mac99 probe: BAR0 0x80000000, first ata-3 reg offset 0x20000.
                expected = '80000000' if case == 'bar' else '80020000'
                assert f'ATA-TEST pa={expected}' in lines
            if case == 'normal':
                assert 'ATA-TEST wait reads=2 rc=0' in lines
                assert 'ATA-TEST present=1 registered=1' in lines
                assert ident and all(f'ATA-TEST lba={i} rc=0 hash={digest(sector(i))}' in lines
                                     for i in (0, 1024, 2047)), lines[-8:]
                assert 'ATA-TEST present=0 registered=0' in guest(elf, cpu, disk, absent=True)
            elif case == 'lba':
                assert ident and f'ATA-TEST lba=0 rc=0 hash={digest(sector(1))}' in lines
            elif case == 'word':
                assert 'ATA-TEST model=EQUMH RADDSI K sectors=8' in lines
            elif case == 'wait':
                assert 'ATA-TEST wait reads=1 rc=-1' in lines
    print(f'ATA acceptance {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    raise SystemExit(main())
