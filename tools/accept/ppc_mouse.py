#!/usr/bin/env python3
"""Exercise the PowerMac USB pointer over link-sh RSH on mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

CASES = ('normal', 'invert-x', 'ignore-bounds', 'absent')


def absent_bindings(folder, out):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = "    'db': (frozenset({"
    if source.count(anchor) != 1:
        raise ValueError('pointer provider mutation anchor')
    group = """    'absent_mouse': (frozenset({
        'mouse_available', 'mouse_poll', 'mouse_set_bounds', 'mouse_cursor_show',
        'mouse_cursor_hide', 'mouse_cursor_set_mode',
    }), frozenset()),
"""
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, group + anchor))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'stamp').touch()


def observe(elf, cpu, evidence, case):
    from kboot_run import serial
    from link_sh_run import Session
    from run import require
    from usb_run import keyboard_actions, rsh_command

    s = Session(elf, cpu, evidence, 240)
    s.qemu_clock = [*s.qemu_clock, '-device', 'usb-mouse,id=mouse']
    log = evidence / 'serial-a.log'

    def probe(action, rc=0, during=None):
        line = '/bin/mousprob.bin ' + action
        output = f'{rc}\n'.encode() + (b'' if rc else b'\n')
        rsh_command(s, line, output, during=during)
        rsh_command(s, 'echo $?', f'{rc}\n'.encode())

    def sample(name, width):
        # HMP reads big-endian words. Align before extracting a byte or halfword.
        offset = s.symbols[name] % 4
        word = s.word(name, -offset)
        return (word >> (8 * (4 - offset - width))) & ((1 << (8 * width)) - 1)

    try:
        s.start()
        s.wait(s.ready, 'RSH READY', 60)
        s.wait(lambda: 'protocol=02' in serial(log) and 'protocol=01' in serial(log),
               'mouse and keyboard enumeration', 30)
        probe('start', 1 if case == 'absent' else 0)
        if case != 'absent':
            expected = (319, 199) if case == 'ignore-bounds' else (80, 160)
            require((sample('px', 2), sample('py', 2)) == expected, 'set_bounds clamps position')
        if case in ('normal', 'invert-x'):
            s.q.call('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': 12}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': -7}},
                {'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
            s.wait(lambda: 'dx=12 dy=-7 b=1' in serial(log), 'relative report')
            expected = (72, 153, 0xfff4) if case == 'invert-x' else (88, 153, 12)
            require((sample('px', 2), sample('py', 2), sample('old_x', 2)) == expected,
                    'relative x/y and accumulated x delta')
            require(sample('old_y', 2) == 0xfff9 and sample('buttons', 1) == 1,
                    'relative y delta and left button')
        if case == 'normal':
            probe('cursor')
            require(s.word('cursor_display_mode') == 1, 'TEXT mode selected')
            key, _ = keyboard_actions(s.q, lambda: serial(log), s.wait)
            probe('key', during=lambda: key('a'))
            s.q.call('device_del', {'id': 'mouse'})
            s.wait(lambda: 'USB RELEASE kind=1' in serial(log), 'held mouse detached')
            probe('gone')
            s.q.call('device_add', {'driver': 'usb-mouse', 'id': 'mouse2'})
            s.wait(lambda: serial(log).count('protocol=02') >= 2, 'mouse reattached')
            probe('present')
            require(sample('buttons', 1) == 0 and sample('old_x', 2) == 0 and
                    sample('old_y', 2) == 0, 'reattach starts released with no stale motion')
        s.q.call('quit')
        s.p.wait(timeout=5)
        require(s.p.returncode == 0, 'QEMU exit')
        print(('GREEN' if case == 'normal' else 'RED ' + case) + ' ' + cpu, flush=True)
    finally:
        s.close()


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

    work = Path(tempfile.mkdtemp(prefix='ppc-mouse-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('hello', 'fault', 'mousprob')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    for case in CASES if args.mutate else ('normal',):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case == 'invert-x':
            mutation = mutant_settings(folder, case, 'platform/powermac/usb_platform.c',
                                       'mouse_ext_move(dx, dy, 0);', 'mouse_ext_move(-dx, dy, 0);')
        elif case == 'ignore-bounds':
            mutation = mutant_settings(folder, case, 'input/hal/pointer_state.c',
                                       '    xmin = x0;', '    return;\n    xmin = x0;')
        elif case == 'absent':
            absent_bindings(folder, kernel / 'gen')
        elf = build(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {elf}')
        images[digest] = case
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
