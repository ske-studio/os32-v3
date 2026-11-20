#!/usr/bin/env python3
"""Exercise the four PCI KAPI slots over link-sh RSH on mac99."""
import argparse
import hashlib
import re
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

CASES = ('normal', 'bus-shift', 'absent')


def devices(entries):
    """Configuration scan order within one host bridge."""
    for dev in sorted(entries, key=lambda d: (d['slot'], d['function'])):
        yield (dev['bus'], dev['slot'], dev['function'], dev['id']['vendor'], dev['id']['device'])
        yield from devices(dev.get('pci_bridge', {}).get('devices', []))


def rows(matches):
    return [tuple(int(field, 10 if i < 3 else 16) for i, field in enumerate(match))
            for match in matches]


def check_domain(session, body):
    from run import require
    domains = [list(devices(root['devices'])) for root in session.q.call('query-pci')]
    gmac_id = (0x106b, 0x0021)
    domain, = [group for group in domains if any(row[3:] == gmac_id for row in group)]
    # UniNorth type 0 starts at slot 11. Other roots can reuse the same BDF.
    root_bus = next(row[0] for row in domain if row[3:] == gmac_id)
    expected = [row for row in domain if row[0] != root_bus or row[1] >= 11]
    gmac, = [row for row in expected if row[3:] == gmac_id]
    # pm_gmac_boot.bdf follows the three clock words and five root/bus words.
    require(session.word('lan_boot', 32) == (gmac[0] << 16 | gmac[1] << 11 | gmac[2] << 8),
            'Open Firmware GMAC BDF')
    serial = (session.out / 'serial-a.log').read_text(errors='replace')
    boot = rows(re.findall(r'\[pci\] (\d+):(\d+)\.(\d+) ([0-9a-fA-F]+):([0-9a-fA-F]+)', serial))
    probe = rows(re.findall(rb'^PCI (\d+) (\d+) (\d+) ([0-9a-fA-F]+) ([0-9a-fA-F]+)$', body, re.M))
    require('[pci] uni-north ok' in serial and f'[pci] {len(expected)} devices' in serial,
            'UniNorth probe and count')
    require(boot == probe == expected, repr((expected, boot, probe)))


def observe(elf, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'PCI cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'PCI RSH mode')
        body = rsh_command(session, '/bin/pciprob.bin')
        status = rsh_command(session, 'echo $?')
        if case == 'normal':
            require(b'PCI OK\n' in body and status == b'0\n', repr((body, status)))
            check_domain(session, body)
            session.keys('echo usb\n')
            session.wait(lambda: re.search(r'(?m)^usb *$', session.screen('usb')),
                         'USB keyboard after PCI probe', 15)
            rsh_command(session, 'echo lan', b'lan\n')
        else:
            marker, code = ((b'PCI config failed\n', b'3\n') if case == 'bus-shift'
                            else (b'PCI get failed\n', b'2\n'))
            require(marker in body and status == code, repr((body, status)))
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'PCI QEMU exit')
        print(('GREEN' if case == 'normal' else 'RED ' + case) + ' ' + cpu, flush=True)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if any(not os.access(cross / 'bin' / ('powerpc-elf-' + tool), os.X_OK)
           for tool in ('gcc', 'ld', 'ar', 'nm', 'objcopy', 'objdump')):
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3

    from kboot_accept import build, mutant_settings
    from ppc_shm import guest_available

    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-pci-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / 'pciprob.bin')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    for case in CASES if args.mutate else ('normal',):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'bus-shift':
            mutation = mutant_settings(folder, case, 'kernel/dev/device_pci.c',
                                       'return pci_cfg_read32(bus, dev, fn, reg);',
                                       'return pci_cfg_read32(bus + 1, dev, fn, reg);')
        elif case == 'absent':
            from test_kapi_binding import generate
            kernel.mkdir()
            generate(folder, 'pci-absent').rename(kernel / 'gen')
            (kernel / 'gen/stamp').touch()
        elf = build(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {elf}')
        images[digest] = case
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
