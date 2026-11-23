#!/usr/bin/env python3
"""PowerMac C-GUI acceptance on both G4s, with separate resident shell images."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
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
from ppc_in2_cui import IN2_MUTATIONS
from in2_gui_trial import run_trial


# Each mutant must reach the preceding observation on this boot.
FAILURE_MARKERS = {
    'register': 'C-GUI READY', 'consume': 'Loading /sys/shell.bin',
    'select': 'Loading /bin/gshell.bin', 'no-key': 'C-GUI MAKE',
    'no-button': 'C-GUI BUTTON DOWN', 'no-focus': 'C-GUI FOCUS',
    'no-drag-end': 'C-GUI UP', 'no-release': 'C-GUI GONE',
}


D_MUTATIONS = {
    'closed': ('explicit', 'kapi/kapi_in2.c', '#include "platform_input_caps.h"',
               '#define IN2_PUBLIC_EXCLUSIVE 0\n#include "platform_input_caps.h"'),
    'background': ('explicit', 'input/hal/in2_excl.c',
                   'if (!owner || owner != av_focus_owner()) return OS32_ERR_BUSY;\n    unsigned f = irq_save();\n    if (owner != acq_owner',
                   'unsigned f = irq_save();\n    if (owner != acq_owner'),
    'drop-tab': ('explicit', 'input/hal/in2_excl.c', ' || sc == KEY_TAB', ''),
    'release-lost': ('explicit', 'input/hal/in2_excl.c', '    in2_ring_lost(IN2_LOST_EXCL);',
                     '    if (focus || owner) in2_ring_lost(IN2_LOST_EXCL);'),
    'keep-owner': ('focus', 'input/hal/in2_excl.c', 'if (acq_owner == old) transition(0, 0, 1);',
                   'if (acq_owner == old) transition(acq_owner, excl, 1);'),
    'keep-capture': ('explicit',),
    'keep-left': ('focus',),
    'keep-right': ('exit',),
}

D_FAILURES = {
    'closed': ('D-GUI explicit HELD', 'C-GUI foreground acquire rc=-10'),
    'background': ('D-GUI busy-Q BACKGROUND', 'C-GUI background acquire rc=0'),
    'drop-tab': ('D-GUI exclusive KEY 15 1', 'D-GUI RAW 15 1'),
    'release-lost': ('D-GUI exclusive UP', 'C-GUI missing release LOST_EXCL='),
    'keep-owner': ('D-GUI focus-Q FOREGROUND EXCLUSIVE', 'C-GUI foreground flags=3'),
    'keep-capture': ('D-GUI explicit HELD', 'C-GUI residual drag='),
    'keep-left': ('D-GUI focus HELD', 'C-GUI residual left='),
    'keep-right': ('D-GUI exit-P HELD', 'C-GUI residual right='),
}


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


def wm_mutant(folder, case):
    crate = folder / 'userland/gshell'
    shutil.copytree(ROOT / 'userland/gshell', crate, ignore=shutil.ignore_patterns('target'))
    for name in ('sdk', 'exec', 'include', 'net', 'userland/lib', 'rust-toolchain.toml'):
        (folder / name).symlink_to(ROOT / name)
    source = crate / 'src/input.rs'
    if case in ('keep-capture', 'keep-left', 'keep-right'):
        source = crate / 'src/excl.rs'
        before, after = 'input::cancel_capture(st);', ''
    elif case == 'no-focus':
        before = 'let old_front = st.front_id();\n    if changed {\n        st.bring_to_front(idx);'
        after = before.replace('st.bring_to_front(idx);', 'return;')
    else:
        before = 'crate::movecopy::move_window(st, idx, cx, cy);'
        after = 'return;'
    text = source.read_text()
    assert text.count(before) == 1
    source.write_text(text.replace(before, after))
    return 'PPC_GSHELL_DIR=' + str(crate)


def observe(media, cpu, folder, case, user, mutation=None):
    s = Session(media, cpu, folder, 240)
    s.usb_devices = (*s.usb_devices, '-device', 'usb-mouse,id=mouse')
    log = folder / 'serial-a.log'
    nm = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-nm'
    symbols = subprocess.check_output([nm, user / 'gshell.elf'], text=True)
    s.symbols['C_GUI_POINTER'] = int(re.search(r'^(\w+) \w C_GUI_POINTER$', symbols, re.M)[1], 16)

    def pointer():
        return tuple(s.word('C_GUI_POINTER', offset) for offset in (0, 4, 8, 12))

    def wait_pointer(x, y, buttons, *, effective=None):
        expected = (x, y, buttons, buttons if effective is None else effective)
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

    offset = 0

    def seen(marker):
        timeout = 15 if marker == FAILURE_MARKERS.get(mutation) else 60
        reader_step = {'no-key': 2, 'no-button': 6}.get(mutation)
        if reader_step is None or marker != FAILURE_MARKERS[mutation]:
            s.wait(lambda: marker in serial(log)[offset:], marker, timeout)
            return

        def reader_log():
            trace = serial(log)[offset:]
            if any(s in trace for s in ('C-GUI PROBE FAILED', ' returned ', 'Loading /')):
                raise AssertionError(f'probe exited during op={reader_step}')
            return trace

        try:
            s.wait(lambda: marker in reader_log(), marker, timeout)
        except RuntimeError as exc:
            if not str(exc).startswith(f'deadline {marker} evidence='):
                raise
            end = len(reader_log())
            empty = f'C-GUI empty op={reader_step}\n'
            s.wait(lambda: reader_log()[end:].replace('\r', '').count(empty) >= 2,
                   f'continued empty reads op={reader_step}')
            assert marker not in reader_log()
            raise

    def ordered(pattern, label):
        nonlocal offset
        def found():
            nonlocal offset
            match = re.search(pattern, serial(log)[offset:])
            if match:
                offset += match.end()
            return match
        s.wait(found, label)

    def event(kind, data):
        nonlocal offset
        offset = len(serial(log))
        s.q.call('input-send-event', {'events': [{'type': kind, 'data': data}]})

    def key(down):
        event('key', {'down': down, 'key': {'type': 'qcode', 'data': 'a'}})

    def move(dx, dy):
        nonlocal offset
        offset = len(serial(log))
        s.q.call('input-send-event', {'events': [
            {'type': 'rel', 'data': {'axis': axis, 'value': value}}
            for axis, value in [('x', dx), ('y', dy)]]})

    gui = 'Loading /bin/gshell.bin'
    cui = 'Loading /sys/shell.bin'
    try:
        s.start()
        if mutation == 'select':
            seen(cui)
        seen(gui)
        if mutation == 'register':
            seen('C-GUI REGISTER FAILED')
            seen('C-GUI INIT NOSYS')
        if case == 'normal':
            seen('C-GUI READY')
            x, y = s.word('px', unit='h'), s.word('py', unit='h')
            wait_pointer(x, y, 0)
            ids = re.search(r'C-GUI normal P=(\d+) Q=(\d+) owner=(\d+) kb=(\d+) mouse=(\d+)', serial(log))
            p, q, owner, kb, mouse = map(int, ids.groups())
            assert p != q and owner and kb and mouse and kb != mouse
            before, acquired, released = re.findall(
                r'C-GUI acquire (?:before|after|released)\r?\n.*?C-GUI WM sample (-?\d+) (\d+) (\d+) (\d+) (\d+) (\d+)',
                serial(log), re.S)
            generation = int(before[5])
            assert tuple(map(int, before[:5])) == (0, p, owner, 0, 0)
            assert tuple(map(int, acquired)) == (0, p, owner, 3, owner, generation + 1)
            assert tuple(map(int, released)) == (0, p, owner, 0, 0, generation + 2)
            mouse_dev = r'USB DEV addr=(\d+) vid:pid=0627:0001 class=03 subclass=01 protocol=02'
            mouse_addr = re.search(mouse_dev, serial(log))[1]

            def state(index):
                return s.word('C_GUI_POINTER', 4 * index)
            s.wait(lambda: (state(7), state(8), state(9), state(10)) == (p, owner, 1, 1),
                   'P foreground and open WM input')
            windows('ready', x, y, False)
            key(True)
            seen('USB KEY src=1 sc=1d down=1 usage=04')
            seen('C-GUI MAKE')
            seen('C-GUI RAW 1')
            seen('USB REPEAT src=1 sc=1d')
            key(False)
            seen('C-GUI RAW 0')
            seen('C-GUI BREAK')
            move(12, -7)
            seen('C-GUI MOVE')
            wait_pointer(x + 12, y - 7, 0)
            event('btn', {'button': 'left', 'down': True})
            seen('dx=0 dy=0 b=1')
            s.wait(lambda: state(2) == 1, 'WM physical left button')
            seen('C-GUI BUTTON DOWN')
            seen('C-GUI FOCUS')
            s.wait(lambda: state(7) == q, 'Q foreground')
            # The WM masks the held focus click until release.
            wait_pointer(x + 12, y - 7, 1, effective=0)
            windows('focus', x, y, True)
            event('btn', {'button': 'left', 'down': False})
            seen('C-GUI CLICK UP')
            wait_pointer(x + 12, y - 7, 0)
            event('btn', {'button': 'left', 'down': True})
            seen('C-GUI DRAG START')
            wait_pointer(x + 12, y - 7, 1)
            frame = state(5), state(6)
            assert state(4) != 0xffffffff
            move(10, 3)
            seen('C-GUI DRAG')
            s.wait(lambda: (state(5), state(6)) == (frame[0]+10, frame[1]+3), 'drag frame')
            wait_pointer(x + 22, y - 4, 1)
            event('btn', {'button': 'left', 'down': False})
            seen('C-GUI UP')
            wait_pointer(x + 22, y - 4, 0)
            move(12, -7)
            seen('C-GUI STABLE')
            wait_pointer(x + 34, y - 11, 0)
            key(True)
            delivery_start = offset
            seen('C-GUI FRONT KEY')
            key(False)
            seen('C-GUI FRONT BREAK')
            deliveries = re.findall(r'C-GUI KEY (\d+) ([01])', serial(log)[delivery_start:])
            assert deliveries == [(str(q), '1'), (str(q), '0')], deliveries
            event('btn', {'button': 'left', 'down': True})
            seen('C-GUI HELD')
            wait_pointer(x + 34, y - 11, 1)
            offset = len(serial(log))
            s.q.call('device_del', {'id': 'mouse'})
            ordered(rf'USB DETACH addr={mouse_addr}\b', 'original mouse detached')
            ordered(r'USB RELEASE kind=1 src=\d+\b', 'mouse release')
            if mutation == 'no-release':
                seen('C-GUI detach buttons=1')
                wait_pointer(x + 34, y - 11, 0)
            seen('C-GUI GONE')
            wait_pointer(x + 34, y - 11, 0)
            s.wait(lambda: state(4) == 0xffffffff, 'released drag')
            existing = {entry['index'] for entry in s.q.call('query-mice')}
            offset = len(serial(log))
            s.q.call('device_add', {'driver': 'usb-mouse', 'id': 'mouse2'})
            ordered(mouse_dev, 'added USB mouse')
            ordered(rf'C-GUI re-enumerated kb={kb} mouse=\d+\b', 'probe enumerated mouse')
            seen('C-GUI REATTACHED')
            new_mouse, = [item for item in s.q.call('query-mice') if item['index'] not in existing]
            s.q.call('human-monitor-command', {'command-line': f"mouse_set {new_mouse['index']}"})
            move(12, -7)
            seen('C-GUI FINAL MOVE')
            seen(f'C-GUI POINTER {x + 46} {y - 18} 0 0')
            seen('C-GUI DONE')
        if case in ('explicit', 'focus', 'exit'):
            class Trial:
                def trace(self):
                    return serial(log)

                @property
                def offset(self):
                    return offset

                def wait_match(self, pattern):
                    def match():
                        trace = self.trace()
                        if mutation in D_FAILURES and mutation != 'drop-tab':
                            previous, failure = D_FAILURES[mutation]
                            if previous in trace and failure in trace.partition(previous)[2]:
                                raise RuntimeError('observed mutation ' + mutation)
                        return re.search(pattern, trace[offset:])
                    s.wait(match, pattern)
                    return match()

                def seen(self, marker):
                    # Defer the mutant's TAB observations until the guest has completed
                    # the trial. A timeout cannot establish a missing WM delivery.
                    if mutation == 'drop-tab' and marker in ('D-GUI RAW 15 1', 'D-GUI RAW 15 0'):
                        return
                    self.wait_match(re.escape(marker))

                def key(self, code, down):
                    event('key', {'down': down, 'key': {'type': 'qcode', 'data': code}})

                def button(self, name, down):
                    event('btn', {'button': name, 'down': down})

            trial = Trial()
            trial.move = move
            run_trial(trial, case)
            seen("C-GUI completed " + case)
            if mutation == 'drop-tab':
                trace = serial(log).partition('D-GUI explicit RESUMED')[2]
                if all(f'D-GUI RAW 15 {down}' not in trace for down in (1, 0)):
                    raise RuntimeError('observed mutation drop-tab')
        seen(cui)
        history = re.findall(r'Loading /(bin/gshell|sys/shell)\.bin|'
                             r'/bin/gshell.bin returned (-?\d+)', serial(log))
        status = {'missing': -3, 'fault': -2}.get(case, 0)
        assert history == [('bin/gshell', ''), ('', str(status))] * (
            3 if case == 'fault' else 1) + [('sys/shell', '')], history
        s.wait(s.ready, 'CUI ready', 60)
        # Closing rshell observes the ESC press. Its USB release can arrive later.
        esc_start = len(serial(log))
        s.close_rshell()
        s.wait(lambda: 'USB KEY src=1 sc=00 down=0 usage=29' in serial(log)[esc_start:],
               'USB Escape release before cooked input')
        tap, type_keys = keyboard_actions(s.q, lambda: serial(log), s.wait, folder)
        type_keys('echo cooked')
        tap('ret')
        s.wait(lambda: re.search(r'(?m)^cooked *$', s.screen('cooked')),
               'physical keyboard cooked command response')
        print(f'{case} {cpu} runtime observed', flush=True)
    except (RuntimeError, AssertionError):
        print(f'{cpu} {case} serial tail:\n{serial(log)[-8192:]}', flush=True)
        raise
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--suite', choices=['all', 'c', 'd'], default='all')
    args = parser.parse_args()
    if not args.build_only:
        from ppc_shm import guest_available
        if not guest_available():
            print('SKIP: mac99 QEMU or loopback sockets unavailable')
            return 3
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    work = Path(tempfile.mkdtemp(prefix='ppc-gui-')).resolve()
    cases = []
    if args.suite != 'd':
        cases += ['normal', 'missing', 'fault']
        if args.mutate:
            cases.extend(FAILURE_MARKERS)
    if args.suite != 'c':
        cases += ['explicit', 'focus', 'exit']
        if args.mutate:
            cases.extend(D_MUTATIONS)
    hashes = set()
    for case in cases:
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        trial_name = D_MUTATIONS[case][0] if case in D_MUTATIONS else case
        wm_mutations = ('no-focus', 'no-drag-end', 'keep-capture', 'keep-left', 'keep-right')
        if case in wm_mutations:
            user = folder / 'user'
        elif trial_name in ('explicit', 'focus', 'exit'):
            user = work / ('user-' + trial_name)
        else:
            user = work / 'user'
        options = ['IMAGE=kboot-link-sh', 'PPC_C_GUI=1', 'KBOOT_USB_KEY_TRACE=1',
                   'KB_SH_OUT=' + str(user), 'KB_ROOT_BINS=' + str(user / 'c-gui/guiprob.bin')]
        if trial_name in ('explicit', 'focus', 'exit'):
            options += ['PPC_GUI_TRIAL=' + trial_name]
        if case in D_MUTATIONS and len(D_MUTATIONS[case]) > 1:
            options += mutant_settings(folder, case, *D_MUTATIONS[case][1:])
        if case in wm_mutations:
            options += [wm_mutant(folder, case)]
        if case == 'no-button':
            options += mutant_settings(folder, case, 'input/hal/in2_state.c',
                                       'if (!legacy_active()) input_emit_in2(&e);',
                                       'if (!legacy_active() && e.type != INPUT_EVENT_BUTTON) input_emit_in2(&e);')
        if case in ('no-key', 'no-release'):
            options += mutant_settings(folder, case, *IN2_MUTATIONS[case])
        if case == 'select':
            options += mutant_settings(folder, case, 'init/shell_loop.c',
                                       'ops->gui_default() ? SYS_GSHELL_BIN : SYS_SHELL_BIN', 'SYS_SHELL_BIN')
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

        assert payload('/BIN/GUIPROB.BIN;1') == (user / 'c-gui/guiprob.bin').read_bytes()
        assert payload('/SYS/SHELL.BIN;1') == (user / 'shell.bin').read_bytes()
        gui_image = payload('/BIN/GSHELL.BIN;1')
        if case == 'missing':
            assert not gui_image
        else:
            assert gui_image == (fault if case == 'fault' else user / 'gshell.bin').read_bytes()
        if case not in ('missing', 'fault'):
            role = trial_name if trial_name in ('explicit', 'focus', 'exit') else 'd-input'
            assert ('/bin/guiprob.bin ' + role + '\0').encode() in gui_image
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
                expected = FAILURE_MARKERS.get(case)
                try:
                    observe(media, cpu, folder / cpu, 'normal' if expected else trial_name, user, case)
                except RuntimeError as exc:
                    if case in D_FAILURES:
                        prev, failure = D_FAILURES[case]
                        trace = serial(folder / cpu / 'serial-a.log')
                        if prev not in trace:
                            raise
                        if case == 'drop-tab':
                            if str(exc) != 'observed mutation drop-tab':
                                raise
                        elif str(exc) != 'observed mutation ' + case or failure not in trace.partition(prev)[2]:
                            raise
                        print(f'RED {case} {cpu}: {failure}', flush=True)
                        continue
                    if not expected or not str(exc).startswith(f'deadline {expected} evidence='):
                        raise
                    trace = serial(folder / cpu / 'serial-a.log')
                    evidence = ('C-GUI INIT NOSYS' in trace.partition('C-GUI REGISTER FAILED')[2]
                                if case == 'register'
                                else {'consume': trace.count('Loading /bin/gshell.bin') >= 2,
                                      'select': 'Loading /sys/shell.bin' in trace,
                                      'no-key': 'USB KEY src=1 sc=1d down=1 usage=04' in trace,
                                      'no-button': 'dx=0 dy=0 b=1' in trace,
                                      'no-focus': 'C-GUI BUTTON DOWN' in trace,
                                      'no-drag-end': 'C-GUI unexpected released rect' in trace,
                                      'no-release': 'C-GUI detach buttons=1' in trace}[case])
                    if not evidence:
                        raise
                    print(f'RED {case} {cpu}: {expected}', flush=True)
                else:
                    if expected or case in D_FAILURES:
                        raise RuntimeError(f'{case} {cpu} survived')
    print(f'{len(hashes)} distinct C-GUI images')


if __name__ == '__main__':
    raise SystemExit(main())
