#!/usr/bin/env python3
"""Exercise the font lease and Unicode KAPI slots over mac99 link-sh RSH."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

from kboot_accept import build_media, mutant_settings
from ppc_shm import guest_available
from test_kapi_binding import generate

CASES = {'normal': (0, 0), 'range-end': (0, 15), 'font-absent': (10, 10)}


def observe(media, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(media, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'font probe CUI READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'font probe RSH mode')
        for args, expected in zip((' leak', ''), CASES[case]):
            body = rsh_command(session, '/bin/fontprob.bin' + args)
            status = rsh_command(session, 'echo $?')
            code = str(expected).encode()
            require(body == b'fontprob=' + code + (b'\n' if expected else b'\n\n') and
                    status == code + b'\n', repr((case, args, body, status)))
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'font probe QEMU exit')
        print(('GREEN' if case == 'normal' else 'RED') + f' {case} {cpu}', flush=True)
    finally:
        session.close()


def settings(work):
    # Keep the product font for fbcon. The lease fixture only needs the two
    # JIS rows spanning the nonzero offset of the reference glyph.
    from gen_font16 import build_ebdt_payload
    payload = build_ebdt_payload(ROOT / 'assets/fonts/JF-Dot-Shinonome16.ttf')
    row_bytes = 94 * 32
    first_row = (0x2f - 0x21) * row_bytes
    font = work / 'jisrows.bin'
    font.write_bytes(payload[first_row:first_row + 2 * row_bytes])
    manifest = json.loads((ROOT / 'build/ppc-ram-root.json').read_text())
    # The image rule tracks the resident shell selected by KB_SH_OUT.
    del manifest['/sys/shell.bin']
    manifest['/fonts/jisrows.bin'] = str(font)
    manifest_path = work / 'root.json'
    manifest_path.write_text(json.dumps(manifest))

    user = work / 'user'
    # The probe formats integer status codes only.
    return ['IMAGE=kboot-link-sh', 'PPC_USER_FLOAT=', f'KB_SH_OUT={user}',
            f'KB_ROOT_BINS={user / "fontprob.bin"}',
            f'ROOT_MANIFEST={manifest_path}',
            'ROOT_DEPS=' + ' '.join(manifest.values())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--case', choices=CASES)
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not os.access(cross / 'bin/powerpc-elf-gcc', os.X_OK):
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3
    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-font-')).resolve()
    print(f'evidence={work}', flush=True)
    options = settings(work)
    images = {}
    cases = CASES if args.mutate else ('normal',)
    for case in ((args.case,) if args.case else cases):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'range-end':
            mutation = mutant_settings(
                folder, case, 'fs/vfs_fd.c',
                'if (offset > f->size || len > f->size - offset) return VFS_ERR_INVAL;',
                'if (offset >= f->size || len > f->size - offset) return VFS_ERR_INVAL;')
        elif case == 'font-absent':
            kernel.mkdir()
            gen = generate(kernel, case)
            (gen / 'stamp').touch()
        media = build_media(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}')
        images[digest] = case
        print(f'BUILT {case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(media, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
