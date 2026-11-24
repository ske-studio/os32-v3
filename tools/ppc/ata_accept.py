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
import zlib

from kboot_accept import build_media, mutant_settings
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
    # Run the fixture at the resident shell entry, with its normal owner and slot.
    kernel_copy = out / 'boot.c'
    kernel_copy.write_text(source.replace(anchor, anchor + '\n'
                           '        exec_run("/sys/shell.bin");\n'
                           '        kprintf(7, "ATA-TEST done\\r\\n");'))
    resident = 'userland/shell/main.c'
    entry = 'int main(int argc, char **argv, KernelAPI *api)\n{'
    launcher = (ROOT / resident).read_text()
    assert launcher.count(entry) == 1
    path = out / 'launcher.c'
    path.write_text(launcher.replace(entry, entry + '\n    api->ide_init();\n    return 0;'))
    user = out / 'user'
    settings = ['KB_MUT_EXTRA_SRC=' + kernel, 'KB_MUT_EXTRA_FILE=' + str(kernel_copy),
                'KB_SH_OUT=' + str(user), 'PPC_RES_MUT_SRC=' + resident,
                'PPC_RES_MUT_FILE=' + str(path)]
    device = 'kernel/dev/device_ide.c'
    provider = (ROOT / device).read_text()
    init = '    (void)ide_init();'
    assert provider.count(init) == 1
    provider = provider.replace(init, '    extern void ata_write_report(int);\n'
                                f'    ata_write_report({int(case == "deny")});')
    header = (ROOT / BUS).read_text()
    store = 'if (ide_window) ppc_store8(ide_window + 16 * reg, v);'
    assert header.count(store) == 1
    header = header.replace(store, 'if (ide_window && reg == IDE_REG_STATUS && '
                            '(v == IDE_CMD_WRITE || v == IDE_CMD_WRITE_MULTIPLE)) '
                            '++ata_write_commands;\n    ' + store)
    if case == 'write-word':
        word = '((u16)p[0] << 8) | p[1]'
        assert header.count(word) == 1
        header = header.replace(word, '(ata_payload_phase ? ((u16)p[1] << 8) | p[0] : '+word+')')
    (out / 'platform_ide_bus.h').write_text(
        '#include "drivers/ata/ide.h"\nextern unsigned ata_write_commands;\n'
        'extern int ata_payload_phase;\n' + header)
    if case == 'write-lba':
        anchor = 'ide_write_sector(drv, lba,'
        assert provider.count(anchor) == 1
        provider = provider.replace(anchor, 'ide_write_sector(drv, lba + 1,')
    if case == 'write-count':
        settings += mutant_settings(out, case, DRIVER, 'return ide_request(unit, lba, count, 0, sectors);',
                                    'for (u32 i = 0; i < count; ++i) {\n'
                                    '        int rc = ide_request(unit, lba+i, 1, 0, (const u8 *)sectors+i*512);\n'
                                    '        if (rc) return rc;\n    }\n    return 0;')
    if case == 'deny':
        for name in ('ide_write_sector', 'ide_write_sectors'):
            call = '    return ' + name + '('
            assert provider.count(call) == 1
            provider = provider.replace(call, '    ++ata_write_provider_calls;\n' + call)
        settings.append('KB_ROOT_BINS=' + str(user / 'atadeny.bin'))
    path = out / 'device_ide.c'
    path.write_text('extern unsigned ata_write_provider_calls;\n' + provider)
    settings += ['KB_MUT_THIRD_SRC=' + device, 'KB_MUT_THIRD_FILE=' + str(path)]
    return build_media(out, '-j6', 'IMAGE=kboot-sh', *settings)


def make_image(work, case):
    out = work / case
    out.mkdir()
    if case in WRITE_CASES:
        return make_write_image(out, case)
    source = (ROOT / BOOT).read_text()
    diag = (ROOT / 'tools/ppc/ata_accept_diag.c').read_text()
    source = source.replace('void pm_ide_boot(void)', diag + '\nvoid pm_ide_boot(void)')
    end = '    ide_init();'
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
        before = 'static int ide_wait_bsy(u32 limit_ms)'
        driver = (ROOT / DRIVER).read_text()
        assert driver.count(before) == 1
        start = driver.index(before)
        stop = driver.index('\n}', start) + 2
        polling = driver[start:stop]
        assert polling.count('ide_bus_alt()') == 1
        instrumented = polling.replace('ide_bus_alt()', '(probe ? '
                                       '(++reads == 1 ? IDE_ST_BSY : 0) : ide_bus_alt())')
        if case == 'wait':
            pending = 'ide_wait_pending(&wait, limit_ms)'
            assert instrumented.count(pending) == 1
            instrumented = instrumented.replace(pending, '(!probe || reads == 0) && ' + pending)
        settings += mutant_settings(out, case, DRIVER, polling,
                                    'static unsigned probe, reads;\n' + instrumented + """
void ata_check_wait(void)
{
    probe = 1;
    int rc = ide_wait_bsy(IDE_IO_TIMEOUT_MS);
    probe = 0;
    kprintf(7, "ATA-TEST wait reads=%u rc=%d\\r\\n", reads, rc);
}
""")
    if case == 'lba':
        path = 'drivers/ata/ide_addr.c'
        before = 'out->sect_num = (u8)(lba & 0xFFUL);'
        settings += mutant_settings(out, case, path, before,
                                    'out->sect_num = (u8)((lba + 1) & 0xFFUL);')
    return build_media(out, '-j6', *settings)


def sector(lba):
    return bytes((i * 19 + (i >> 3) + lba * 7 + (lba >> 8)) & 255 for i in range(512))


def digest(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return f'{h:08x}'


def guest(media, cpu, disk, absent=False, second=None, bootargs=None, done="ATA-TEST done", keyboard=None, directory=None):
    work = (directory or media.elf.parent) / (cpu + ('-absent' if absent else ''))
    work.mkdir()
    iso = media.iso.resolve()
    serial = work / 'serial.log'
    args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-smp', '1',
            '-accel', 'tcg', '-bios', ROM, '-display', 'none', '-serial', 'file:' + str(serial),
            '-serial', 'null', '-monitor', 'none', '-no-reboot', '-nic', 'none',
            '-drive', f'file={iso},format=raw,if=ide,index=2,media=cdrom', '-boot', 'd',
            '-prom-env', r'boot-device=cd:,\PPCKERN.ELF', '-prom-env', 'auto-boot?=true']
    if not absent:
        guest_disk = work / 'disk.raw'
        shutil.copyfile(disk, guest_disk)
        args += ['-drive', f'file={guest_disk},format=raw,if=ide,index=0,media=disk,cache=writethrough']
    if second:
        shutil.copyfile(second, work / 'second.raw')
        args += ['-drive', f'file={work / "second.raw"},format=raw,if=ide,index=1,cache=writethrough']
    if bootargs:
        args += ['-prom-env', r'boot-command=boot cd:,\PPCKERN.ELF ' + bootargs]
    if keyboard:
        args += ['-usb', '-device', 'usb-kbd', '-icount', 'shift=auto', '-qmp', 'stdio']
    with (work / 'qemu.log').open('wb') as log, subprocess.Popen(
            args, stderr=log, stdout=subprocess.PIPE if keyboard else log,
            stdin=subprocess.PIPE if keyboard else None) as proc:
        try:
            deadline = time.monotonic() + 90
            if keyboard:
                keyboard(proc, serial, deadline)
            while proc.poll() is None and time.monotonic() < deadline:
                lines = serial.read_bytes().decode(errors='replace').splitlines() if serial.exists() else []
                if done in lines:
                    return lines
                time.sleep(.15)
            raise RuntimeError(f'guest stopped or timed out: {serial} evidence={work}')
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
    write_disk = work / 'install.raw'
    root_start = 16448
    with write_disk.open('wb') as stream:
        stream.truncate((root_start + 4000) * 512)
    hashes = set()
    cases = ('normal', 'write', 'deny')
    if args.mutate or args.build_only:
        cases += MUTANTS + WRITE_MUTANTS
    for case in cases:
        media = make_image(work, case)
        sha = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        assert sha not in hashes, case
        hashes.add(sha)
        print(case, sha, media, flush=True)
        if args.build_only:
            continue
        for cpu in ('7450', '7455'):
            medium = write_disk if case in WRITE_CASES and case != 'deny' else disk
            lines = guest(media, cpu, medium)
            if case in WRITE_CASES:
                expected = bytearray(medium.read_bytes())
                actual = (media.elf.parent / cpu / 'disk.raw').read_bytes()
                if case == 'deny':
                    assert 'ATA-TEST unreserved=-17,-17' in lines
                    assert 'ATA-TEST child=73 provider_calls=0' in lines, lines[-10:]
                else:
                    metadata = actual[:2048] + actual[root_start*512:]
                    assert f'ATA-TEST metadata={zlib.crc32(metadata):08x}' in lines
                    expected[:2048] = actual[:2048]
                    expected[root_start*512:] = actual[root_start*512:]
                    assert 'ATA-TEST end=0' in lines
                    assert 'ATA-TEST writes=0,0 range=-17' in lines, lines[-10:]
                    commands = 2 if case == 'write-count' else 1
                    assert f'ATA-TEST commands={commands}' in lines, lines[-10:]
                    payload = bytes((i * 37 + (i >> 8) + 13) & 255 for i in range(1024))
                    if case == 'write-word':
                        payload = bytes(payload[i ^ 1] for i in range(1024))
                    first = 97 if case == 'write-lba' else 96
                    expected[first*512:(first+1)*512] = payload[:512]
                    expected[64*512:66*512] = payload
                    for n in range(3):
                        equal = case != 'write-word' and not (case == 'write-lba' and n == 0)
                        assert f'ATA-TEST read={n} equal={int(equal)}' in lines
                assert actual == expected, case
                continue
            ident = 'ATA-TEST model=QEMU HARDDISK sectors=2048' in lines
            if case in ('normal', 'bar'):
                # mac99 probe: BAR0 0x80000000, first ata-3 reg offset 0x20000.
                expected = '80000000' if case == 'bar' else '80020000'
                assert f'ATA-TEST pa={expected}' in lines
            if case == 'normal':
                assert 'ATA-TEST wait reads=2 rc=0' in lines
                assert 'ATA-TEST present=1 registered=0' in lines
                assert ident and all(f'ATA-TEST lba={i} rc=0 hash={digest(sector(i))}' in lines
                                     for i in (0, 1024, 2047)), lines[-8:]
                assert 'ATA-TEST present=0 registered=0' in guest(media, cpu, disk, absent=True)
            elif case == 'lba':
                assert ident and f'ATA-TEST lba=0 rc=0 hash={digest(sector(1))}' in lines
            elif case == 'word':
                assert 'ATA-TEST model=EQUMH RADDSI K sectors=8' in lines
            elif case == 'wait':
                assert 'ATA-TEST wait reads=1 rc=-1' in lines
    print(f'ATA acceptance {"build-only" if args.build_only else "runtime"} OK ({len(hashes)} images)')


if __name__ == '__main__':
    raise SystemExit(main())
