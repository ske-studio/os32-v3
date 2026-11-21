#!/usr/bin/env python3
"""Stage 5c A7/A8: OF fixed mode, INDEX8 shadow transfer and SYSMEM scanout."""
import argparse
import hashlib
import os
import re
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


def drawing(session, evidence, serial, fallback):
    from fb_pixel import read_ppm
    from run import require
    from usb_run import keyboard_actions, rsh_command

    fg = int(re.search(r'FB text ready generation=\d+ fg=(\d+)', serial)[1])
    require(fg in (0, 255), 'OF palette endpoints')
    read_log = lambda: (evidence / 'serial-a.log').read_text(errors='replace')
    session.wait(lambda: 'protocol=01' in read_log(), 'USB keyboard ready')
    key, _ = keyboard_actions(session.q, read_log, session.wait, evidence)

    def physical_word(address):
        response = session.q.call('human-monitor-command', {'command-line': f'xp /1wx {address:#x}'})
        return int(re.search(r':\s*0x([0-9a-fA-F]+)', response)[1], 16)

    def sample():
        spans = {'b': (700, 700), 'p': (705, 712), 'e': (705, 719),
                 'm': (700, 724), 'f': (700, 700)}
        for phase in ('b' if fallback else 'bpemf'):
            def waiting():
                base = physical_word(session.symbols['bb_b'])
                return base and physical_word(base) == 0x50524200 + ord(phase)
            session.wait(waiting, 'gfx2 checkpoint ' + phase)
            path = evidence / ('draw-' + phase + '.ppm')
            session.q.call('screendump', {'filename': str(path)})
            frame = read_ppm(path)
            require((frame.width, frame.height) == (800, 600), 'OF dimensions')
            left, right = spans[phase]
            for y in range(560, 562):
                for x in range(700, 724):
                    want = bytes([255 if left <= x < right else 0]) * 3
                    require(frame.pixel(x, y) == want, repr((phase, x, y)))
            key(phase)

    rsh_command(session, f'/bin/gfx2prob.bin draw {fg}',
                b'GFX2 present counters failed\n' if fallback else b'GFX2 drawing OK\n\n', during=sample)
    rsh_command(session, 'echo $?', b'24\n' if fallback else b'0\n')


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
        require((body, status) == EXPECTED['normal' if case == 'shadow-fallback' else case], repr((case, body, status)))
        if case in ('normal', 'shadow-fallback'):
            drawing(session, evidence, serial, case == 'shadow-fallback')
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
    bins = [str(user / 'gfx2prob.bin')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    for case in (*EXPECTED, 'shadow-fallback') if args.mutate else ('normal',):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'mode-width':
            mutation = mutant_settings(folder, case, 'gfx/v2/enum.c',
                                       '    if (!rc) *out = v;',
                                       '    if (!rc) { v.w++; *out = v; }')
        elif case == 'shadow-fallback':
            mutation = mutant_settings(folder, case, 'gfx/v2/display.c',
                                       'rc = DISPLAY_V2_SHADOW_PUSH(l, t, r - l, b - t);',
                                       'rc = OS32_ERR_NOSYS;')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        if case != 'normal':
            iso = str(normal_elf.parent / 'root.iso')
            mutation.extend(['ROOT_ISO=' + iso, '--old-file=' + iso])
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
    print(f'{len(images)} distinct link-sh ELF images', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
