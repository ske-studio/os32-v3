#!/usr/bin/env python3
"""Real INDEX8 sh acceptance: USB startup, UTF-8 script input and QMP pixels.

All expected positions come from the fixed script, never from the screenshot.
Only stream framing is instrumented; renderer, shell and input remain real.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

from fb_pixel import Font, Terminal, read_ppm, reserved_unchanged
from kboot_accept import build, mutant_settings
from kboot_run import ROOT, ROM, QMP, command, require, serial, validate

from usb_run import product_settings, stream_payload, USB_QEMU_CLOCK


class VerdictError(RuntimeError):
    pass


def check(ok, label):
    if not ok:
        raise VerdictError(label)


def accept(elf, cpu, out, mode):
    out.mkdir()
    cd = out/'cd'; cd.mkdir(); shutil.copyfile(elf, cd/'PPCKERN.ELF')
    iso = out/'boot.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    a, b = out/'serial-a.log', out/'serial-b.log'
    argv = ['qemu-system-ppc', '-g', '800x600x8', '-M', 'mac99', '-cpu', cpu,
            '-m', '128M', '-smp', '1', '-accel', 'tcg', '-bios', str(ROM),
            '-drive', f'file={iso},media=cdrom', '-boot', 'd',
            '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF', '-prom-env', 'auto-boot?=true',
            '-display', 'none', '-serial', 'file:'+str(a), '-serial', 'file:'+str(b),
            '-monitor', 'none', '-qmp', 'stdio', '-no-reboot', '-nic', 'none',
            '-usb', *USB_QEMU_CLOCK]
    (out/'argv.json').write_text(json.dumps(argv, indent=2))
    err = (out/'qemu.stderr').open('w')
    process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=err, bufsize=0)
    q = QMP(process, time.monotonic()+60)

    def wait(predicate, seconds, label):
        end = time.monotonic()+seconds
        while time.monotonic() < end:
            validate(serial(a), serial(b))
            require(process.poll() is None, 'QEMU alive')
            if predicate(): return
            time.sleep(.01)
        raise RuntimeError(label+' timeout; evidence='+str(out))
    font = Font(ROOT/'assets/fonts/shino16.kcgfont')
    baseline = None
    expected = {}
    # color! endpoints recorded as WRITTEN=4, fg=255/bg=0. QEMU's
    # display DAC presents these programmed endpoints as black and white.
    fg, bg = (255, 255, 255), (0, 0, 0)

    def stream():
        return stream_payload(a.read_bytes().decode('latin1')).encode('latin1').decode('utf-8', errors='replace').replace('\r', '')

    def shot(name, lines=None, cursor=()):
        nonlocal baseline
        q.deadline = time.monotonic()+30
        path = out/(name+'.ppm')
        previous, stable = None, 0
        end = time.monotonic()+10
        while stable < 2:
            require(time.monotonic() < end, 'frame did not settle: '+name)
            q.call('stop')
            try:
                q.call('screendump', {'filename': str(path.resolve())})
                img = read_ppm(path)
            finally:
                q.call('cont')
            stable = stable+1 if img == previous else 0
            previous = img
            time.sleep(.1)
        check((img.width, img.height) == (800, 600), '800x600 geometry')
        t = Terminal(img, font, fg, bg, cursor_top=14)
        if baseline is None:
            baseline = img
        check(reserved_unchanged(baseline, img), name+': reserved 64 scanlines changed')
        check(t.unexpected_color() is None, name+': unexpected RGB')
        if lines is not None:
            expected[name] = dict(lines=lines, cursor=cursor)
            (out/'expected.json').write_text(json.dumps(expected, ensure_ascii=False, indent=2))
            for row in range(33):
                text = lines[row] if row < len(lines) else ''
                width = sum(font.glyph(c)[0]//8 for c in text)
                mismatch = t.compare_text(row, 0, text+' '*(100-width), cursor)
                check(mismatch is None, name+': '+str(mismatch))
            check(t.bottom_line(lines[32] if len(lines)>32 else '', cursor) is None,
                  name+': bottom line')
        return t

    def key(code):
        q.deadline = time.monotonic()+30
        released = serial(a).count('down=0')
        q.call('send-key', {'keys':[{'type':'qcode','data':code}], 'hold-time':80})
        wait(lambda: serial(a).count('down=0') > released, 10, 'USB release '+code)

    def usb(text):
        for c in text:
            key({'\n': 'ret', ' ': 'spc', '.': 'dot', '/': 'slash'}.get(c, c))

    def usb_command(text, wanted):
        start = len(stream())
        previous = stream().count('sh> ')
        usb(text+'\n')
        if mode == 'mirror' and text == 'echo hi':
            # The mutated byte mirror also removes the prompt. fd1 still
            # supplies an independent positive command-completion boundary;
            # a missing prompt by timeout is never accepted as RED.
            wait(lambda: stream()[start:].endswith('hi\n'), 10, 'fd1 echo boundary')
            shot('mirror-boundary')
        else:
            wait(lambda: stream().count('sh> ') >= previous + (1 if previous else 2)
                 and stream()[start:].endswith('sh> '), 10, 'USB command')
        block = stream()[start:]
        banner = ('================================\n OS32 External Shell Started\n'
                  '================================\nsh> ') if not previous else ''
        check(block == banner+text+'\n'+wanted+'sh> ', 'USB exact echo/fd1: '+repr(block))

    def before_open():
        wait(lambda: 'Press a USB key or connect LAN to start sh\n' in stream(),
             30, 'waiting message')
        require('FB text ready' in serial(a), 'real INDEX8 attach')
        require('FB full_rc=0 color_rc=4' in serial(a), 'color! recorded before detach')
        match = re.search(r'FB text ready generation=1 fg=(\d+) bg=(\d+)', serial(a))
        require(match is not None, 'guest color indices recorded')
        # The oracle follows the color! contract; swapping the recorded indices
        # must still disagree with the expected white foreground pixels.
        (out/'colors.json').write_text(json.dumps(dict(color_rc=4,
            foreground=int(match[1]), background=int(match[2]),
            palette={'0': [0, 0, 0], '255': [255, 255, 255]})))
        t = shot('waiting')
        check(t.compare_text(1, 0, 'Press a USB key or connect LAN to start sh') is None,
              'waiting: fixed guide row 1')
        q.call('device_add', {'driver': 'usb-kbd', 'id': 'kbd'})
        wait(lambda: 'protocol=01' in serial(a), 10, 'USB HID enumeration')
        usb_command('echo hi', 'hi\n')
        usb_command('clear', '\x1b[2J\x1b[H')
        shot('usb-clear', ['sh> '], [(0, 4)])

    try:
        before_open()
        usb_command('clear', '\x1b[2J\x1b[H')
        shot('clear', ['sh> '], [(0, 4)])
        cmd = 'source /fb.sh'
        usb_command(cmd, 'ASCII ｶﾅ 日本語\n')
        shot('text', ['sh> '+cmd, 'ASCII ｶﾅ 日本語', 'sh> '], [(2, 4)])
        usb_command('clear', '\x1b[2J\x1b[H')
        usb('echo ax')
        wait(lambda: stream().endswith('echo ax'), 10, 'cursor input')
        t = shot('cursor', ['sh> echo ax'], [(0, 11)])
        check(t.cursor_positions(0, 0, 'sh> echo ax  ') == [(0, 11)], 'exact inverted cursor')
        key('backspace')
        # Cooked redraw sends BS-space-BS; screenshot after the complete redraw.
        wait(lambda: stream().endswith('\b \b'), 10, 'BS stream redraw')
        shot('bs', ['sh> echo a'], [(0, 10)])
        key('ret')
        wait(lambda: stream().endswith('sh> '), 10, 'BS command prompt')
        usb_command('clear', '\x1b[2J\x1b[H')
        # 35 numbered output lines, plus each command echo and prompt: 71
        # lines total. Scroll leaves command/output pairs 19..34 and the prompt.
        for i in range(35):
            usb_command('echo l%02d' % i, 'l%02d\n' % i)
        lines = []
        for i in range(19, 35):
            lines += ['sh> echo l%02d' % i, 'l%02d' % i]
        lines += ['sh> ']
        shot('scroll', lines, [(32, 4)])
        # The entire acceptance runs with no NIC and USB input only.
        usb_command('clear', '\x1b[2J\x1b[H')
        usb_command('echo usb', 'usb\n')
        shot('usb-only', ['sh> echo usb', 'usb', 'sh> '], [(2, 4)])
        (out/'stream.txt').write_text(stream())
        q.call('quit'); process.wait(timeout=2)
        require(process.returncode == 0, 'QEMU quit rc=0')
    finally:
        (out/'stream.txt').write_text(stream())
        q.close()
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=2)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        err.close()


MUTATIONS = [
    ('trace-screen', 'platform/powermac/console_log.c',
     'if (pm_log_stream_only) return;', ''),
    ('colors', 'platform/powermac/rh_observe.c', '.foreground = 255, .background = 0',
     '.foreground = 0, .background = 255'),
    ('top', 'init/ppc_kernel.c', 'pm_console_attach(&target, &kcg_file_api, PM_RH_FB_ROWS)',
     'pm_console_attach(&target, &kcg_file_api, 0)'),
    ('mirror', 'kernel/console/console.c',
     '    if (CONSOLE_PUTCHAR_MIRROR && (rshell_active || boot_serial_mirror)) con_output_putchar(ch);', ''),
    ('scroll-minus', 'kernel/console/fbcon.c', 'fbcon_scroll_n(c, rows, 1);', 'fbcon_scroll_n(c, rows - 1, 1);'),
    ('scroll-plus', 'kernel/console/fbcon.c', 'fbcon_scroll_n(c, rows, 1);', 'fbcon_scroll_n(c, rows + 1, 1);'),
    ('start-consumed', 'platform/powermac/shell_boot.c',
     '(void)pm_terminal_trygetkey(); /* Consume the first OPEN cancellation. */',
     '(void)pm_terminal_trygetkey(); (void)pm_input_usb_take();'),
]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mutate', action='store_true')
    p.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    args = p.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross/'bin/powerpc-elf-gcc').is_file() or not ROM.is_file() or any(
            shutil.which(x) is None for x in ['qemu-system-ppc', 'genisoimage']):
        print('SKIP ppc-fb: PPC toolchain/QEMU/OpenBIOS unavailable')
        return 0
    os.environ['PPC_FB'] = '1'
    work = Path(tempfile.mkdtemp(prefix='fb-run-', dir=os.environ['TMPDIR']))
    print('evidence='+str(work), flush=True)
    for mode, source, old, new in [('normal', None, None, None)] + (MUTATIONS if args.mutate else []):
        script = work/'fb.sh'; script.write_text('echo ASCII ｶﾅ 日本語\n')
        manifest = json.loads((ROOT/'build/ppc-ram-root.json').read_text())
        manifest['/fb.sh'] = str(script)
        manifest_path = work/'root.json'; manifest_path.write_text(json.dumps(manifest))
        opts = product_settings(work, mode) + ['ROOT_MANIFEST='+str(manifest_path),
            'ROOT_DEPS=assets/fonts/shino16.kcgfont assets/fonts/JF-Dot-Shinonome16-LICENSE.txt build/out/unicode.bin '+str(script)]
        if source:
            opts += [x.replace('KB_MUT_', 'KB_MUT_EXTRA_') for x in
                     mutant_settings(work, mode, source, old, new)]
        elf = build(work/mode, 'PPC_CROSS_DIR='+str(cross), *opts)
        hashes = {str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path):
                  dict(size=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                  for path in [elf, script, manifest_path, ROOT/'assets/fonts/shino16.kcgfont',
                               ROOT/'assets/fonts/JF-Dot-Shinonome16.ttf',
                               ROOT/'assets/fonts/JF-Dot-Shinonome16-LICENSE.txt',
                               ROOT/'build/out/unicode.bin']}
        (work/(mode+'-inputs.json')).write_text(json.dumps(hashes, indent=2))
        for cpu in (['7450', '7455'] if args.cpu == 'both' else [args.cpu]) if mode == 'normal' else ['7450']:
            try:
                accept(elf, cpu, work/(mode+'-'+cpu), mode)
            except VerdictError as exc:
                if mode == 'normal': raise
                print('RED runtime '+mode+': '+str(exc), flush=True)
            else:
                require(mode == 'normal', 'mutant survived: '+mode)
                print('GREEN INDEX8 pixels '+cpu, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
