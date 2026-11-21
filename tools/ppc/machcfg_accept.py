"""MACHCFG_DESIGN §7 and P-I: 7450 follows the mounted file, 7455 keeps HID0.NAP.
A 1024-byte file is rejected in full. A missing root uses the CPU defaults.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from kboot_accept import build, mutant_settings
from run import ROOT, ROM


def observe(elf, directory, cpu, file, value, mounted, nap7450):
    directory.mkdir()
    cd = directory / 'cd'
    cd.mkdir()
    shutil.copy(elf, cd)
    iso = directory / 'boot.iso'
    subprocess.run(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop',
                    '-o', iso, cd], check=True)
    serial = directory / 'serial.log'
    args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M',
            '-accel', 'tcg', '-bios', ROM, '-display', 'none', '-nic', 'none',
            '-serial', 'file:' + str(serial), '-serial', 'null', '-monitor', 'none',
            '-no-reboot', '-cdrom', iso, '-boot', 'd', '-prom-env',
            r'boot-device=cd:,\PPCKERN.ELF', '-prom-env', 'auto-boot?=true']
    with (directory / 'qemu.log').open('wb') as log, subprocess.Popen(args, stderr=log, stdout=log) as proc:
        try:
            deadline = time.monotonic() + 60
            while proc.poll() is None and time.monotonic() < deadline:
                data = serial.read_bytes().replace(b'\r', b'').decode(errors='replace') if serial.exists() else ''
                hid = re.search(r'^MACHCFG HID0 pvr=0x([\da-fA-F]+) hid0=0x([\da-fA-F]+) nap=([01])\n', data, re.M)
                root = re.search(r'^KBOOT ROOT rc=(-?\d+) registered=([01]) ', data, re.M)
                if hid and (mounted is None or root):
                    break
                time.sleep(.1)
            else:
                raise RuntimeError(f'missing boot evidence: {serial}')
        finally:
            proc.kill()
    if mounted is not None:
        assert (int(root[1]) == 0) == mounted and (not mounted or root[2] == '1'), root[0]
    assert 'KBOOT CLOCK hz=' in data and 'KBOOT ALIVE tick_delta=' in data
    pvr, raw, bit = (int(hid[i], 16) for i in (1, 2, 3))
    assert pvr >> 16 == (0x8000 if cpu == '7450' else 0x8001)
    assert bit == bool(raw & 0x00400000) and bit == (nap7450 if cpu == '7450' else 1)
    nap = '0/n-a' if cpu == '7455' else f'{value}/' + ('file' if file == 'ok' else 'def')
    line = f'MACHCFG powermac cpu=0x{hid[1]} seed=cpu file={file} perf=0/def nap={nap}'
    assert data[:hid.start()].splitlines().count(line) == 1, (line, serial)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='machcfg-accept-'))
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    template = json.loads((ROOT / 'build/ppc-ram-root.json').read_text())
    hashes = {}
    def check(case, file, value, settings, mounted=True, nap7450=None):
        elf = build(work / ('base' if case in ('zero', 'one', 'none', 'big') else case),
                    '-j6', *settings)
        sha = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert sha not in hashes.values(), case
        hashes[case] = sha
        print(case, sha, flush=True)
        if mounted is not None:
            iso = elf.parent / 'root.iso'
            listing = subprocess.check_output(['isoinfo', '-i', iso, '-f']).splitlines()
            cfg_path = b'/ETC/MACHINE.CFG;1'
            configured = case not in ('none',)
            assert (cfg_path in listing) == configured, (case, listing)
            if configured:
                data = subprocess.check_output(['isoinfo', '-i', iso, '-x', cfg_path])
                assert data == cfg.read_bytes(), case
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, work / (case + '-' + cpu), cpu, file, value, mounted,
                        value if nap7450 is None else nap7450)

    check('core', 'nofs', 0, ['IMAGE=kboot-core'], None)
    for case, value in [('zero', 0), ('one', 1), ('none', 0), ('big', 1)]:
        entries = template.copy()
        if case != 'none':
            cfg = work / (case + '.cfg')
            data = f'ppc7450.nap={value}\n'.encode()
            cfg.write_bytes(data.ljust(1024, b' ') if case == 'big' else data)
            entries['/etc/machine.cfg'] = str(cfg)
        manifest = work / (case + '.json')
        manifest.write_text(json.dumps(entries))
        # Older selections must invalidate the ISO through the selection stamp alone.
        os.utime(manifest, (946684800, 946684800))
        if case != 'none':
            os.utime(cfg, (946684800, 946684800))
        settings = ['IMAGE=kboot-link-sh', 'ROOT_MANIFEST=' + str(manifest),
                    'ROOT_DEPS=' + ' '.join(entries.values())]
        file = case if case in ('none', 'big') else 'ok'
        check(case, file, value if file == 'ok' else 0, settings)
        if args.mutate or args.build_only:
            if case == 'one':
                ignore = mutant_settings(work, 'ignore-nap', 'arch/ppc/kernel_cpu.c',
                                         'if (nap7450)', 'if (!nap7450)')
                check('ignore-nap', 'ok', 1, settings + ignore, nap7450=0)
                wrong = mutant_settings(work, 'host-path', 'kernel/machcfg_root.c',
                                        'sysconfig_temp_begin(SYS_MACHINE_CFG)',
                                        'sysconfig_temp_begin("/host/etc/machine.cfg")')
                check('host-path', 'none', 0, settings + wrong)
                failed = mutant_settings(work, 'mount-failed', 'init/boot_root.c',
                                         'vfs_mount("/", "rd0", "iso9660")',
                                         'vfs_mount("/", "rd0", "absent")')
                check('mount-failed', 'nofs', 0, settings + failed, False)
            if case == 'big':
                trunc = mutant_settings(work, 'short-read', 'init/sysconfig.c',
                                        'vfs_read(path, sc_buf, SYSCONFIG_MAX);',
                                        'vfs_read(path, sc_buf, SYSCONFIG_MAX - 1);')
                check('short-read', 'ok', 1, settings + trunc)
    print('PASS ' + ('build-only' if args.build_only else 'mac99') + f' {len(hashes)} distinct images')


if __name__ == '__main__':
    main()
