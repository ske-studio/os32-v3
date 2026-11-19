#!/usr/bin/env python3
"""Exercise the resource-free display slots over link-sh RSH on mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

EXPECTED = {
    'owner-absent': (b'DISPLAY owner failed\n', b'1\n'),
    'vblank-absent': (b'DISPLAY vblank validation failed\n', b'3\n'),
    'controls-absent': (b'DISPLAY ownership checks failed\n', b'5\n'),
}

EXIT_MODE_CASE = 'exit-mode-changed'

ABSENT = {
    'owner-absent': ('gfx_screen_owner',),
    'vblank-absent': ('gfx2_vblank_wait',),
    'controls-absent': ('gfx2_palette', 'gfx2_gamma', 'gfx2_cursor_move',
                        'gfx2_cursor_show', 'gfx2_dpms'),
}


def absent_bindings(folder, out, case):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = 'PROVIDER_GROUPS = {\n'
    slots = ABSENT[case]
    if source.count(anchor) != 1 or any(repr(name) in source for name in slots):
        raise RuntimeError(f'{case}: expected unique provider table and published slots')
    group = f"    'display_mut': (frozenset({slots!r}), frozenset()),\n"
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, anchor + group))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'stamp').touch()


def read_head(body, status, suffix):
    from run import require

    match = re.fullmatch(rb'DISPLAY HEAD ((?:-?\d+ ){12}-?\d+)\n' + suffix, body)
    require(match is not None and status == b'0\n', repr((body, status)))
    return tuple(map(int, match[1].split()))


def observe(elf, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'display cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'display RSH mode')
        serial = (evidence / 'serial-a.log').read_text(errors='replace')
        require('FB text ready' in serial, 'INDEX8 framebuffer console attached')
        body = rsh_command(session, '/bin/dispprb.bin')
        status = rsh_command(session, 'echo $?')
        if case in EXPECTED:
            require((body, status) == EXPECTED[case], repr((case, body, status)))
        else:
            before = read_head(body, status, b'DISPLAY OK\n\n')
            # This child reads the OF head after the first child's teardown.
            body = rsh_command(session, '/bin/dispprb.bin head')
            status = rsh_command(session, 'echo $?')
            after = read_head(body, status, b'\n')
            expected = before
            if case == EXIT_MODE_CASE:
                expected = (before[0] ^ 1, *before[1:])
            require(after == expected, repr((case, 'head before/after exit', before, after)))
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

    work = Path(tempfile.mkdtemp(prefix='ppc-display-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('hello', 'fault', 'dispprb')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    cases = ('normal', *EXPECTED, EXIT_MODE_CASE) if args.mutate else ('normal',)
    for case in cases:
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        if case in ABSENT:
            absent_bindings(folder, kernel / 'gen', case)
        mutation = []
        if case == EXIT_MODE_CASE:
            anchor = 'int exec_gfx_owner_release(struct addrspace *as) { (void)as; return 0; }'
            mutation = mutant_settings(folder, case, 'exec/absent/exec_gfx.c', anchor,
                '#include "../../gfx/v2/gfx2_int.h"\n'
                'int exec_gfx_owner_release(struct addrspace *as)\n'
                '{\n'
                '    struct gfx2_adapter *a = gfx2_adapter_get(0);\n'
                '    (void)as;\n'
                '    if (a) a->head[0].mode_id ^= 1u;\n'
                '    return 0;\n'
                '}')
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
