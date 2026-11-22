#!/usr/bin/env python3
"""Exercise the five path KAPI slots over link-sh RSH on mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

EXPECTED = {
    'normal': (b'PATH OK\n\n', b'0\n'),
    'accept-invalid': (b'PATH invalid drive accepted\n', b'7\n'),
    'unset-root-drive': (b'PATH root drive failed\n', b'1\n'),
    'absent': (b'PATH root drive failed\n', b'1\n'),
}


def absent_bindings(folder, out):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = "PROVIDER_GROUPS = {\n"
    if source.count(anchor) != 1:
        raise RuntimeError('absent path mutation anchor must be unique')
    group = """    'path': (frozenset({
        'path_get_drive', 'path_get_cwd', 'path_set_drive', 'path_set_cwd', 'path_parse',
    }), frozenset()),
"""
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, anchor + group))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'stamp').touch()


def observe(media, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(media, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'path cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'path RSH mode')
        body = rsh_command(session, '/bin/pathprob.bin')
        status = rsh_command(session, 'echo $?')
        require((body, status) == EXPECTED[case], repr((case, body, status)))
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'path QEMU exit')
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

    from kboot_accept import build_media, mutant_settings
    from ppc_shm import guest_available

    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-path-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / 'pathprob.bin')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    for case in EXPECTED if args.mutate else ('normal',):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'accept-invalid':
            mutation = mutant_settings(folder, case, 'lib/path.c',
                                       '        if (!device_validator(name)) return -1;',
                                       '        if (!device_validator(name)) return 0;')
        elif case == 'unset-root-drive':
            mutation = mutant_settings(folder, case, 'platform/powermac/root_boot.c',
                                       '    rc = path_set_drive(vfs_devname("/"));',
                                       '    rc = 0;')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        media = build_media(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {media}')
        images[digest] = case
        print(f'{case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(media, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
