#!/usr/bin/env python3
"""Link-sh/RSH acceptance for shared memory and child owner reclamation."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
import ppc_resource_owner

EXPECTED = {
    'normal': (b'SHM locked store\nSHM OK\n\n', b'0\n'),
    'skip-free': (b'SHM capacity after free\n', b'3\n'),
    'absent': (b'SHM initial capacity\n', b'1\n'),
}


def absent_bindings(folder, out):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    # Anchor on the table head: provider groups disappear as slots are published.
    anchor = "PROVIDER_GROUPS = {\n"
    if source.count(anchor) != 1:
        raise RuntimeError('absent SHM mutation anchor must be unique')
    group = """    'shm': (frozenset({
        'sys_shm_alloc', 'sys_shm_lock', 'sys_shm_free',
    }), frozenset(['shm.h'])),
"""
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, anchor + group))
    generator = ROOT / 'sdk/gen_kapi.py'
    # Use the normal generator with a private provider module and object tree.
    env = dict(os.environ, PYTHONPATH=str(folder))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(generator),
                    '--target', 'ppc-powermac', '--feature-link', '1', '--feature-ide', '1',
                    '--feature-fep', '1', '--out-dir', str(out)],
                   cwd=ROOT, env=env, check=True)
    (out / 'mutant.stamp').touch()


def observe(media, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(media, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'SHM cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'SHM RSH mode')
        for _ in range(2 if case == 'normal' else 1):
            body = rsh_command(session, '/bin/shmprobe.bin')
            status = rsh_command(session, 'echo $?')
            require((body, status) == EXPECTED[case], repr((case, body, status)))
        if case == 'normal':
            ppc_resource_owner.verify(session)
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'SHM QEMU exit')
        print(('GREEN' if case == 'normal' else 'RED-' + case) + ' ' + cpu, flush=True)
    finally:
        session.close()


def guest_available():
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file():
        return False
    if any(not shutil.which(tool) for tool in ('qemu-system-ppc', 'genisoimage')):
        return False
    if not Path('/usr/share/qemu/openbios-ppc').is_file():
        return False
    try:
        for kind in (socket.SOCK_DGRAM, socket.SOCK_STREAM):
            with socket.socket(socket.AF_INET, kind) as probe:
                probe.bind(('127.0.0.1', 0))
    except OSError:
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not args.build_only and not guest_available():
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ': SHM guest tools/socket unavailable')
        return int(required)

    from kboot_accept import build_media, mutant_settings

    work = Path(tempfile.mkdtemp(prefix='ppc-shm-', dir=os.environ.get('TMPDIR')))
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('shmprobe',)]
    options = ['KBOOT_SH=1', 'KBOOT_LINK=1', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    cases = tuple(EXPECTED) if args.mutate else ('normal',)
    images = {}
    for case in cases:
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'normal':
            mutation = ppc_resource_owner.instrument(
                folder, 'mm/shm.c', 'shm_block_owner[start + i] = res_owner_get();',
                'shm_block_owner[start + i]')
        elif case == 'skip-free':
            # Keep the boot-time kernel SHM round trip intact. The guest
            # must reach its own free before the mutation affects capacity.
            mutation = mutant_settings(folder, case, 'mm/shm.c',
                                       '\n        shm_state[idx + i] = SHM_FREE;',
                                       '\n        if (ring3_call_from_user()) return 0;'
                                       '\n        shm_state[idx + i] = SHM_FREE;')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        # Build the RAM root inputs before linking, including on a fresh tree.
        media = build_media(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} image is identical to {images[digest]}: {media}')
        images[digest] = case
        print(f'{case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(media, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
