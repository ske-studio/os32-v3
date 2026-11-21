#!/usr/bin/env python3
"""PowerMac root pair design, stage 4: CRC precedes rd0 registration.
docs/design/platform/powermac/root_pair.md: entry rejects a foreign kernel pair.
Allocation failure, invalid offset, short decode and damaged ISO data must leave
rd0 unregistered and release every reserved page (PPC_RAM_ROOT_W6_PLAN).
A failed decode releases its output and stops before the shell. Returning the staging area increases the free pool by its page count.
Only registered rd0 output remains charged to the boot owner.
"""
import argparse
import hashlib
import os
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]


from root_lz4_layout import PAGE, STAGING_SIZE


def absent_bindings(folder, out):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = "PROVIDER_GROUPS = {\n"
    if source.count(anchor) != 1:
        raise RuntimeError('absent boot-info mutation anchor must be unique')
    group = "    'boot_info': (frozenset({'boot_image_info'}), frozenset()),\n"
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, anchor + group))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'stamp').touch()


def unverified_image(elf, objcopy, iso):
    """Invalidate the expected ISO CRC, keeping the normalized entry pair."""
    record = elf.with_name('record.bin')
    subprocess.run([objcopy, '--dump-section', f'.root_record={record}',
                    elf, elf.with_name('dump.elf')], check=True)
    damaged = bytearray(record.read_bytes())
    if int.from_bytes(damaged[4:8], 'little') != zlib.crc32(iso):
        raise RuntimeError('linked record differs from the source ISO')
    damaged[4] ^= 1
    record.write_bytes(damaged)
    target = elf.with_name('unverified.elf')
    subprocess.run([objcopy, '--update-section', f'.root_record={record}',
                    elf, target], check=True)
    return target


ROOT_FAILURES = {
    'allocation-failed': -2,
    'invalid-offset': -3,
    'short-output': -3,
    'data-crc': -4,
    'unverified': -4,
    'truncated': -3,
    'decode-failed': -3,
}


def negative_image(case, original, target, iso, donor):
    from root_pair import compress, wire

    data = bytearray(original.read_bytes())
    payload, rec, off, frame = wire(data)
    pair, crc, packed, length = struct.unpack_from('<4I', data, rec)
    assert pair == zlib.crc32(payload) and crc == zlib.crc32(iso) and length == len(iso)
    if case == 'foreign-pair':
        foreign, *_ = wire(donor.read_bytes())
        other = zlib.crc32(foreign)
        assert other != pair
        struct.pack_into('<I', data, rec, other)
    elif case == 'allocation-failed':
        # Valid signed length, larger than the entire guest RAM.
        struct.pack_into('<I', data, rec + 12, 256 << 20)
    elif case == 'invalid-offset':
        block = bytes.fromhex('10 41 00 00')
        struct.pack_into('<I', data, rec + 8, len(block))
        data[off:off + len(block)] = block
    elif case == 'short-output':
        struct.pack_into('<I', data, rec + 12, length + 1)
    elif case == 'data-crc':
        damaged = bytearray(iso)
        damaged[-1] ^= 1
        block = compress(bytes(damaged))
        assert len(block) <= frame
        data[off:off + frame] = block.ljust(frame, b'\0')
        struct.pack_into('<I', data, rec + 8, len(block))
    target.write_bytes(data)
    return target


def observe(elf, cpu, evidence, case, expected, exit_status):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, cpu, evidence, 240)
    try:
        session.start()
        if case in ('foreign-pair', 'overlap'):
            stop = 'ppc_kernel_entry_stop' if case == 'foreign-pair' else 'overlap_rejected'
            def at_entry_stop():
                regs = session.q.call('human-monitor-command', {'command-line': 'info registers'})
                pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                return pc and int(pc[1], 16) == session.symbols[stop]
            session.wait(at_entry_stop, f'{case} stopped at {stop}', 60)
        elif case in ROOT_FAILURES or case in ('release-skipped', 'release-twice'):
            log = evidence / 'serial-a.log'
            session.wait(lambda: log.exists() and expected in log.read_bytes(),
                         f'{case} pre-mount boot observation', 60)
        else:
            session.wait(session.ready, 'boot info cui READY', 60)
            require(session.agent.operator.ready['mode'] == 'cui', 'boot info RSH mode')
            body = rsh_command(session, '/bin/bootprob.bin')
            status = rsh_command(session, 'echo $?')
            require((body.rstrip(b'\n'), status) == (expected, exit_status),
                    repr((case, body, status)))
        if case == 'verified' or case in ROOT_FAILURES:
            log = evidence / 'serial-a.log'
            session.wait(lambda: log.exists() and re.search(rb'free_after=\d+\r?\n', log.read_bytes()),
                         'root page accounting', 10)
            raw = log.read_bytes()
            boot = re.search(rb'KBOOT ROOT rc=(-?\d+) registered=(\d+) bytes=(\d+) staging_pages=(\d+)', raw)
            account = re.search(rb'KBOOT ROOT owner_before=(\d+) owner_after=(\d+) free_before=(\d+) free_after=(\d+)', raw)
            require(boot and account, 'missing root observations')
            rc, registered, length, frame = map(int, boot.groups())
            before, after, free_before, free_after = map(int, account.groups())
            require(frame == free_after - free_before and frame == STAGING_SIZE // PAGE and registered == (case == 'verified'),
                    'root frame or registration')
            require(after == before - frame + registered * ((length + 4095) // 4096),
                    f'root owner pages {before} -> {after}')
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'boot info QEMU exit')
        verdict = 'RED' if case in ('false-valid', 'no-write', 'absent') else 'GREEN'
        print(f'{verdict} {case} {cpu}', flush=True)
    finally:
        session.close()


def main(on_image=None):
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

    work = Path(tempfile.mkdtemp(prefix='ppc-boot-info-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('hello', 'fault', 'bootprob')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    cases = ['verified', 'unverified']
    if args.mutate:
        cases += ['false-valid', 'no-write', 'absent', 'truncated', 'decode-failed', 'release-skipped', 'release-twice', 'overlap']
        cases += ['allocation-failed', 'invalid-offset', 'short-output', 'data-crc', 'foreign-pair']
    images = {}
    for case in cases:
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'false-valid':
            mutation = mutant_settings(folder, case, 'platform/powermac/root_image.c',
                                       '    struct boot_common info = {0};',
                                       '    struct boot_common info = {.image_valid = 1};')
        elif case == 'no-write':
            mutation = mutant_settings(folder, case, 'init/bootinfo.c',
                                       '    kmemset(out, 0, sizeof(*out));',
                                       '    return 0;')
        elif case in ('truncated', 'decode-failed'):
            mutation = mutant_settings(folder, case, 'platform/powermac/root_image.c',
                                       'lz4_decode(__root_image_start, packed, dst, length)',
                                       ('lz4_decode(__root_image_start, packed - 1, dst, length)'
                                        if case == 'truncated' else
                                        'lz4_decode(__root_image_start, packed, dst, 1)'))
        elif case in ('release-skipped', 'release-twice'):
            call = 'pgalloc_free_n_owner(LEDGER_OWNER_BOOT, frame_pfn, pages)'
            mutation = mutant_settings(folder, case, 'platform/powermac/root_image.c',
                                       call, '((void)frame_pfn, 1)' if case == 'release-skipped' else
                                       '(' + call + ' && ' + call + ')')
        elif case == 'overlap':
            mutation = mutant_settings(folder, case, 'platform/powermac/kernel_boot.c',
                                       'int reserve_rc = pm_kboot_reserve(info);',
                                       'info->root_frame_bytes += 4096;\n'
                                       '    int reserve_rc = pm_kboot_reserve(info);\n'
                                       '    if (reserve_rc == PM_KBOOT_RESERVE_IMAGE_AVAILABLE)\n'
                                       '        __asm__ volatile("overlap_rejected: b overlap_rejected");')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        derived = case in ('allocation-failed', 'invalid-offset', 'short-output', 'data-crc', 'foreign-pair')
        if derived:
            kernel = work / 'verified/kernel'
            elf = kernel / 'PPCKERN.ELF'
        else:
            elf = build(kernel, *options, *mutation, 'kb-user', 'kernel')
        iso = (kernel / 'root.iso').read_bytes()
        exit_status = b'0\n'
        if derived:
            elf = negative_image(case, elf, folder / (case + '.elf'), iso,
                                 work / 'no-write/kernel/PPCKERN.ELF')
            expected = None
            if case in ROOT_FAILURES:
                expected = f'KBOOT ROOT rc={ROOT_FAILURES[case]} registered=0 bytes=0'.encode()
        elif case in ('unverified', 'false-valid'):
            elf = unverified_image(elf, cross / 'bin/powerpc-elf-objcopy', iso)
            expected = {
                'unverified': b'KBOOT ROOT rc=-4 registered=0 bytes=0',
                'false-valid': b'BOOT 0 1 0 00000000',
            }[case]
        elif case == 'no-write':
            expected = b'BOOT 255 255 1 00000001'
        elif case in ('truncated', 'decode-failed'):
            expected = b'KBOOT ROOT rc=-3 registered=0 bytes=0'
        elif case in ('release-skipped', 'release-twice'):
            expected = b'root page release'
        elif case == 'overlap':
            expected = None  # Observed by stop PC before firmware detaches.
        elif case == 'absent':
            expected = b'BOOT info error -10'
            exit_status = b'1\n'
        else:
            expected = f'BOOT 0 1 {len(iso)} {zlib.crc32(iso):08x}'.encode()
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {elf}')
        images[digest] = case
        print(f'{case} sha256={digest} {elf}', flush=True)
        if on_image is not None:
            on_image(case, elf, kernel / 'root.iso')
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case, expected, exit_status)
    print(f'{len(images)} distinct boot-info ELF images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
