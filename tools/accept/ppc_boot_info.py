#!/usr/bin/env python3
"""Exercise boot_image_info over link-sh RSH on mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]


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
    """Flip a byte in the ISO system area, outside the filesystem metadata."""
    embedded = elf.with_name('embedded.iso')
    subprocess.run([objcopy, '--dump-section', f'.rodata.boot_root={embedded}',
                    elf, elf.with_name('dump.elf')], check=True)
    if embedded.read_bytes() != iso:
        raise RuntimeError('linked root image differs from the source ISO')
    damaged = bytearray(iso)
    damaged[0] ^= 1
    embedded.write_bytes(damaged)
    target = elf.with_name('unverified.elf')
    subprocess.run([objcopy, '--update-section', f'.rodata.boot_root={embedded}',
                    elf, target], check=True)
    return target


def observe(elf, cpu, evidence, case, expected, exit_status):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'boot info cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'boot info RSH mode')
        body = rsh_command(session, '/bin/bootprob.bin')
        status = rsh_command(session, 'echo $?')
        require((body.rstrip(b'\n'), status) == (expected, exit_status),
                repr((case, body, status)))
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'boot info QEMU exit')
        verdict = 'RED' if case in ('false-valid', 'no-write', 'absent') else 'GREEN'
        print(f'{verdict} {case} {cpu}', flush=True)
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

    work = Path(tempfile.mkdtemp(prefix='ppc-boot-info-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / 'bootprob.bin')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    cases = ['verified', 'unverified']
    if args.mutate:
        cases += ['false-valid', 'no-write', 'absent']
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
                                       '    if (!s_common.image_valid) return 0;\n'
                                       '    kmemset(out, 0, sizeof(*out));')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        elf = build(kernel, *options, *mutation, 'kb-user', 'kernel')
        iso = (kernel / 'root.iso').read_bytes()
        exit_status = b'0\n'
        if case in ('unverified', 'false-valid', 'no-write'):
            elf = unverified_image(elf, cross / 'bin/powerpc-elf-objcopy', iso)
            expected = {
                'unverified': b'BOOT 0 0 0 00000000',
                'false-valid': b'BOOT 0 1 0 00000000',
                'no-write': b'BOOT 255 255 1 00000001',
            }[case]
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
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case, expected, exit_status)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
