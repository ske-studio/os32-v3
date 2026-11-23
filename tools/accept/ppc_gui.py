#!/usr/bin/env python3
"""PowerMac C-GUI acceptance on both G4s, with separate resident shell images."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build_media, mutant_settings
from kboot_run import serial
import root_pair
from fb_pixel import read_ppm
from link_sh_run import Session
from usb_run import keyboard_actions


def without_register(folder, out):
    original = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = 'PROVIDER_GROUPS = {\n'
    assert original.count(anchor) == 1
    group = "    'register': (frozenset({'gui_register'}), frozenset()),\n"
    (folder / 'kapi_target_ppc.py').write_text(original.replace(anchor, anchor + group))
    subprocess.run([sys.executable, '-B', '-c',
                    'import runpy,sys,kapi_target_ppc; sys.path.insert(0,"sdk"); '
                    'runpy.run_path(sys.argv.pop(1),run_name="__main__")',
                    'sdk/gen_kapi.py', '--target', 'ppc-powermac', '--feature-link', '1', '--feature-ide', '1',
                    '--feature-fep', '1', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'mutant.stamp').touch()


def observe(media, cpu, folder, case, user):
    s = Session(media, cpu, folder, 240)
    s.usb_devices = (*s.usb_devices, '-device', 'usb-mouse,id=mouse')
    log = folder / 'serial-a.log'
    nm = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-nm'
    symbols = subprocess.check_output([nm, user / 'gshell.elf'], text=True)
    s.symbols['C_GUI_POINTER'] = int(re.search(r'^(\w+) \w C_GUI_POINTER$', symbols, re.M)[1], 16)

    def pointer():
        return tuple(s.word('C_GUI_POINTER', offset) for offset in (0, 4, 8))

    def wait_pointer(x, y, buttons):
        expected = (x, y, buttons)
        s.wait(lambda: pointer() == expected, f'WM pointer {expected}')

    def windows(name, x, y, switched):
        path = folder / (name + '.ppm')
        s.q.call('screendump', {'filename': str(path.resolve())})
        frame = read_ppm(path)
        active, inactive = bytes((0, 0, 170)), bytes((218, 218, 170))
        for wx, wy, color in [(x - 30, y - 16, active if switched else inactive),
                              (x + 160, y + 40, inactive if switched else active)]:
            assert frame.pixel(wx, wy) == bytes(3), 'window border'
            observed = frame.pixel(wx + 100, wy + 9)
            assert all(abs(a-b) <= 3 for a, b in zip(observed, color)), 'window title background'
            assert any(frame.pixel(wx + dx, wy + dy) == bytes([255]) * 3
                       for dx in range(6, 40) for dy in range(3, 19)), 'title glyphs'

    def seen(marker):
        s.wait(lambda: marker in serial(log), marker, 60)

    def event(kind, data):
        s.q.call('input-send-event', {'events': [{'type': kind, 'data': data}]})

    def key(down):
        event('key', {'down': down, 'key': {'type': 'qcode', 'data': 'a'}})

    def move(dx, dy):
        s.q.call('input-send-event', {'events': [
            {'type': 'rel', 'data': {'axis': axis, 'value': value}}
            for axis, value in [('x', dx), ('y', dy)]]})

    gui = 'Loading /bin/gshell.bin'
    cui = 'Loading /sys/shell.bin'
    try:
        s.start()
        seen(gui)
        if case == 'normal':
            seen('C-GUI READY')
            x, y = s.word('px', unit='h'), s.word('py', unit='h')
            wait_pointer(x, y, 0)
            windows('ready', x, y, False)
            key(True)
            seen('C-GUI MAKE')
            seen('USB REPEAT src=1 sc=1d')
            key(False)
            seen('C-GUI BREAK')
            move(12, -7)
            seen('C-GUI MOVE')
            wait_pointer(x + 12, y - 7, 0)
            event('btn', {'button': 'left', 'down': True})
            seen('C-GUI FOCUS')
            wait_pointer(x + 12, y - 7, 1)
            windows('focus', x, y, True)
            event('btn', {'button': 'left', 'down': False})
            seen('C-GUI CLICK UP')
            wait_pointer(x + 12, y - 7, 0)
            event('btn', {'button': 'left', 'down': True})
            seen('C-GUI DRAG START')
            wait_pointer(x + 12, y - 7, 1)
            move(10, 3)
            seen('C-GUI DRAG')
            wait_pointer(x + 22, y - 4, 1)
            event('btn', {'button': 'left', 'down': False})
            seen('C-GUI UP')
            wait_pointer(x + 22, y - 4, 0)
            move(12, -7)
            seen('C-GUI STABLE')
            wait_pointer(x + 34, y - 11, 0)
            key(True)
            seen('C-GUI FRONT KEY')
            key(False)
            seen('C-GUI DONE')
        seen(cui)
        history = re.findall(r'Loading /(bin/gshell|sys/shell)\.bin|'
                             r'/bin/gshell.bin returned (-?\d+)', serial(log))
        status = {'normal': 0, 'missing': -3, 'fault': -2}[case]
        assert history == [('bin/gshell', ''), ('', str(status))] * (
            3 if case == 'fault' else 1) + [('sys/shell', '')], history
        s.wait(s.ready, 'CUI ready', 60)
        s.close_rshell()
        tap, type_keys = keyboard_actions(s.q, lambda: serial(log), s.wait, folder)
        type_keys('echo cooked')
        tap('ret')
        s.wait(lambda: re.search(r'(?m)^cooked *$', s.screen('cooked')),
               'physical keyboard cooked command response')
        print(f'{case} {cpu} runtime observed', flush=True)
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if not args.build_only:
        from ppc_shm import guest_available
        if not guest_available():
            print('SKIP: mac99 QEMU or loopback sockets unavailable')
            return 3
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    work = Path(tempfile.mkdtemp(prefix='ppc-gui-')).resolve()
    user = work / 'user'
    cases = ['normal', 'missing', 'fault']
    if args.mutate:
        cases += ['register', 'consume']
    hashes = set()
    for case in cases:
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        options = ['IMAGE=kboot-link-sh', 'PPC_C_GUI=1', 'KBOOT_USB_KEY_TRACE=1',
                   'KB_SH_OUT=' + str(user), 'KB_ROOT_BINS=' + str(user / 'guiprob.bin')]
        if case == 'missing':
            options += ['KB_GUI_BINS=']
        if case == 'fault':
            # Keep the packaged resident header and replace its entry with a PPC trap.
            data = bytearray((user / 'gshell.bin').read_bytes())
            header, = struct.unpack_from('<I', data, 4)
            entry, = struct.unpack_from('<I', data, 16)
            struct.pack_into('>I', data, header + entry, 0x7fe00008)
            fault = folder / 'gshell.bin'
            fault.write_bytes(data)
            options += ['KB_GUI_BINS=' + str(fault)]
        if case == 'register':
            without_register(folder, kernel / 'gen')
        if case == 'consume':
            options += mutant_settings(folder, case, 'init/shell_loop.c',
                                       'cur_shell = consumed;', '/* keep current shell */')
        media = build_media(kernel, *options, 'kb-user', 'kernel')
        def payload(path):
            return subprocess.check_output(['isoinfo', '-i', kernel / 'root.iso', '-x', path])

        assert payload('/SYS/SHELL.BIN;1') == (user / 'shell.bin').read_bytes()
        gui_image = payload('/BIN/GSHELL.BIN;1')
        if case == 'missing':
            assert not gui_image
        else:
            assert gui_image == (fault if case == 'fault' else user / 'gshell.bin').read_bytes()
        elf = media.elf.read_bytes()
        wire, _, _, _ = root_pair.wire(elf)
        fep = (kernel / 'fep/fep.mod').read_bytes()
        assert payload('/BOOT/FEP.MOD;1') == root_pair.bind_module(fep, zlib.crc32(wire))
        digest = hashlib.sha256(elf).hexdigest()
        if digest in hashes:
            raise RuntimeError(f'duplicate {case} image')
        hashes.add(digest)
        print(f'{case} {digest} {media.elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                expected = {'register': 'C-GUI READY', 'consume': 'Loading /sys/shell.bin'}.get(case)
                try:
                    observe(media, cpu, folder / cpu, 'normal' if expected else case, user)
                except RuntimeError as exc:
                    if not expected or not str(exc).startswith(f'deadline {expected} evidence='):
                        raise
                    trace = serial(folder / cpu / 'serial-a.log')
                    evidence = ('C-GUI INIT NOSYS' in trace.partition('C-GUI REGISTER FAILED')[2]
                                if case == 'register'
                                else trace.count('Loading /bin/gshell.bin') >= 2)
                    if not evidence:
                        raise
                    print(f'RED {case} {cpu}: {expected}', flush=True)
                else:
                    if expected:
                        raise RuntimeError(f'{case} {cpu} survived')
    print(f'{len(hashes)} distinct C-GUI images')


if __name__ == '__main__':
    raise SystemExit(main())
