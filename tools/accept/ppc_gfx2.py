#!/usr/bin/env python3
"""Exercise the nine gfx2 query slots over link-sh RSH on mac99."""
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
    'normal': (b'GFX2 OK\n\n', b'0\n'),
    'mode-width': (b'GFX2 mode dimensions failed\n', b'6\n'),
    'absent': (b'GFX2 adapter count failed\n', b'1\n'),
}


def absent_bindings(folder, out):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = "    'gfx2': (frozenset({\n"
    if source.count(anchor) != 1:
        raise RuntimeError('absent gfx2 mutation anchor must be unique')
    queries = """        'gfx2_adapter_count', 'gfx2_adapter_info', 'gfx2_adapter_gen',
        'gfx2_caps', 'gfx2_head_info', 'gfx2_connector_info',
        'gfx2_connector_edid', 'gfx2_mode_count', 'gfx2_mode_get',
"""
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, anchor + queries))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'stamp').touch()


def observe(elf, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'gfx2 cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'gfx2 RSH mode')
        serial = (evidence / 'serial-a.log').read_text(errors='replace')
        require('FB text ready' in serial, 'INDEX8 framebuffer console attached')
        body = rsh_command(session, '/bin/gfx2prob.bin')
        status = rsh_command(session, 'echo $?')
        require((body, status) == EXPECTED[case], repr((case, body, status)))
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'gfx2 QEMU exit')
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

    work = Path(tempfile.mkdtemp(prefix='ppc-gfx2-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('hello', 'fault', 'gfx2prob')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    for case in EXPECTED if args.mutate else ('normal',):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'mode-width':
            mutation = mutant_settings(folder, case, 'gfx/v2/enum.c',
                                       '    if (!rc) *out = v;',
                                       '    if (!rc) { v.w++; *out = v; }')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        elf = build(kernel, *options, *mutation, 'kb-user', 'kernel')
        if case == 'normal':
            normal_elf = elf
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {elf}')
        images[digest] = case
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case)
                if case != 'normal':
                    observe(normal_elf, cpu, folder / (cpu + '-control'), 'normal')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
