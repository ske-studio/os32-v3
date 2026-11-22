#!/usr/bin/env python3
"""A1 (PPC_KAPI_LOOP_EXT2_DESIGN §4-3): the 163840-byte RAW fixture has
320 sectors of 512 bytes. The public status reports 1 while attached and 0
after detach. A loop-disabled EXEC image returns NOSYS and preserves outputs."""
import argparse
import hashlib
import shutil
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

EXPECTED = {
    'normal': b'LOOP 0 1 320 512 0',
    'total': b'LOOP 0 1 321 512 0',
    'loop-absent': b'LOOP -10 -10 77 88 -10',
    'disabled': b'LOOP -10 -10 77 88 -10',
}


def observe(media, folder, case, host_root):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(media, '7450', folder / 'guest', 240)
    try:
        session.agent.file_root = str(host_root)
        session.start()
        session.wait(session.ready, 'loop cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'loop RSH mode')
        rsh_command(session, '/bin/loopprb.bin', EXPECTED[case] + b'\n\n')
        rsh_command(session, 'echo $?', b'0\n')
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'loop QEMU exit')
    finally:
        session.close()
    verdict = 'GREEN' if case in ('normal', 'disabled') else 'RED'
    print(f'{verdict} {case} {EXPECTED[case].decode()}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()

    from kboot_accept import build_media, mutant_settings
    from ppc_shm import guest_available
    from test_kapi_binding import generate

    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file():
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3
    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-loop-', dir=os.environ.get('TMPDIR'))).resolve()
    print('evidence=' + str(work), flush=True)
    sample = work / 'sample.img'
    host_root = work / 'host'
    host_root.mkdir()
    digests = set()
    cases = ('normal', 'disabled', 'total', 'loop-absent') if args.mutate else ('normal', 'disabled')
    for case in cases:
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        disabled = case == 'disabled'
        user = work / 'sh-user'
        options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                   'KB_ROOT_BINS=' + str(user / 'loopprb.bin')]
        if case == 'normal':
            # Make generates the RAW fixture before staging it for the read-only hostfs export.
            options.extend(['PPC_LOOP_SAMPLE=' + str(sample), str(sample)])
        settings = []
        if case == 'total':
            settings = mutant_settings(folder, case, 'drivers/block/loop_dev.c',
                                       '*out_total = s->total_lba;', '*out_total = s->total_lba + 1;')
        elif case == 'loop-absent':
            kernel.mkdir()
            (generate(kernel, case) / 'stamp').touch()
        elif disabled:
            # Keep the product shell and select the absent provider in this control only.
            profile = folder / 'loop-disabled.mk'
            profile.write_text('include build/images/ppc-flags.mk\noverride FEATURE_LOOP := 0\n')
            settings = ['PPC_FLAGS_MK=' + str(profile)]
        media = build_media(kernel, *options, *settings, 'kb-user', 'kernel')
        if case == 'normal':
            shutil.copyfile(sample, host_root / 'loop.img')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        if digest in digests:
            raise RuntimeError(f'{case} ELF is identical to a previous image: {media}')
        digests.add(digest)
        print(f'{case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            observe(media, folder, case, host_root)
    print(f"{len(digests)} distinct loop images")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
