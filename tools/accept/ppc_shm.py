#!/usr/bin/env python3
"""Link-sh/RSH acceptance for shared memory and child owner reclamation."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

EXPECTED = {
    'normal': (b'SHM OK\n\n', b'0\n'),
    'skip-free': (b'SHM capacity after free\n', b'3\n'),
    'absent': (b'SHM initial capacity\n', b'1\n'),
}


def absent_bindings(folder, out):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = "    'pointer': (frozenset({"
    if source.count(anchor) != 1:
        raise RuntimeError('absent SHM mutation anchor must be unique')
    group = """    'shm': (frozenset({
        'sys_shm_alloc', 'sys_shm_lock', 'sys_shm_free',
    }), frozenset(['shm.h'])),
"""
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, group + anchor))
    generator = ROOT / 'sdk/gen_kapi.py'
    # Use the normal generator with a private provider module and object tree.
    env = dict(os.environ, PYTHONPATH=str(folder))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(generator),
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=env, check=True)
    (out / 'stamp').touch()


def observe(elf, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'SHM cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'SHM RSH mode')
        for _ in range(2 if case == 'normal' else 1):
            body = rsh_command(session, '/bin/shmprobe.bin')
            status = rsh_command(session, 'echo $?')
            require((body, status) == EXPECTED[case], repr((case, body, status)))
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'SHM QEMU exit')
        print(('GREEN' if case == 'normal' else 'RED-' + case) + ' ' + cpu, flush=True)
    finally:
        session.close()


def guest_available():
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

    from kboot_accept import build, mutant_settings

    work = Path(tempfile.mkdtemp(prefix='ppc-shm-', dir=os.environ.get('TMPDIR')))
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('hello', 'fault', 'shmprobe')]
    options = ['KBOOT_SH=1', 'KBOOT_LINK=1', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    cases = tuple(EXPECTED) if args.mutate else ('normal',)
    for case in cases:
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'skip-free':
            mutation = mutant_settings(folder, case, 'mm/shm.c',
                                       '        shm_state[idx + i] = SHM_FREE;',
                                       '        /* Leave the block allocated. */')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        # Build the RAM root inputs before linking, including on a fresh tree.
        elf = build(kernel, *options, *mutation, 'kb-user', 'kernel')
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
