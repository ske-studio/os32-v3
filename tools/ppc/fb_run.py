#!/usr/bin/env python3
"""Check the resident terminal against the KCG glyphs and the INDEX8 geometry.

The first 64 scanlines belong to the boot display. Local command echo follows
cmd_rshell in userland/shell/rshell.c. Captured LAN output has its own sink.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import tempfile

from fb_pixel import Font, Terminal, read_ppm, reserved_unchanged
from kboot_accept import build, mutant_settings
from kboot_run import ROOT, ROM, require
from link_sh_run import Session
from usb_run import rsh_command


class PixelMismatch(AssertionError):
    pass


def check(condition, detail):
    if not condition:
        raise PixelMismatch(detail)


MUTATIONS = {
    'utf8-bytewise': ('kernel/console/console.c', '        cp = dec.codepoint;\n        p += dec.bytes_used;\n        remaining -= dec.bytes_used;',
                      '        cp = *p;\n        p += dec.bytes_used;\n        remaining -= dec.bytes_used;'),
    'trace-screen': ('platform/powermac/console_log.c', 'if (pm_log_stream_only) return;', ''),
    'colors': ('platform/powermac/rh_observe.c', '.foreground = 255, .background = 0',
               '.foreground = 0, .background = 255'),
    'top': ('platform/powermac/console_text.c', 'pm_console_attach(&target, &kcg_file_api, PM_RH_FB_ROWS)',
            'pm_console_attach(&target, &kcg_file_api, 0)'),
    'scroll-minus': ('kernel/console/fbcon.c', 'fbcon_scroll_n(c, rows, 1);', 'fbcon_scroll_n(c, rows - 1, 1);'),
    'scroll-plus': ('kernel/console/fbcon.c', 'fbcon_scroll_n(c, rows, 1);', 'fbcon_scroll_n(c, rows + 1, 1);'),
    'start-consumed': ('platform/powermac/input_boot.c',
                       'static int try_local_char(void) { return input_legacy_try(IL_LOCAL_CHAR); }',
                       'static int try_local_char(void) { static int lost; int k = input_legacy_try(IL_LOCAL_CHAR); '
                       'if (k >= 0 && !lost) { lost = 1; return -1; } return k; }'),
}


def observe(elf, out, cpu, case):
    s = Session(elf, cpu, out, 600)
    font = Font(ROOT/'assets/fonts/shino16.kcgfont')

    def shot(name):
        shadow = s.screen(name)
        pixels = read_ppm(out/(name+'.ppm'))
        require((pixels.width, pixels.height) == (800, 600), '800x600 console')
        return shadow, Terminal(pixels, font, (255, 255, 255), (0, 0, 0), cursor_top=14)

    def local(line):
        for c in line:
            s.key({' ': 'spc', '/': 'slash', '.': 'dot'}.get(c, c))
        s.key('ret')

    try:
        s.start()
        s.wait(s.ready, 'resident RSH READY', 90)
        initial, baseline = shot('boot')
        check('[sqlite] static rc=0' not in initial, 'trace-screen')
        rsh_command(s, 'clear', b'')
        local('echo first')
        s.wait(lambda: 'first' in s.screen('first'), 'USB first line', 30)
        text, pixels = shot('first')
        check('> echo first' in text, 'start-consumed')
        check(pixels.compare_text(0, 0, '> echo first') is None, 'colors/top')
        check(pixels.compare_text(1, 0, 'first') is None, 'USB output')
        rsh_command(s, 'echo ASCII ｶﾅ 日本語 > /tmp/fb', b'')
        rsh_command(s, 'clear', b'')
        local('cat /tmp/fb')
        s.wait(lambda: 'ASCII' in s.screen('text'), 'fd1 output', 30)
        _, pixels = shot('text')
        check(pixels.compare_text(1, 0, 'ASCII ｶﾅ 日本語') is None, 'utf8-bytewise')
        check(reserved_unchanged(baseline.image, pixels.image), 'top')
        rsh_command(s, 'clear', b'')
        for i in range(35):
            local(f'echo l{i:02d}')
        s.wait(lambda: 'l34' in s.screen('scroll'), 'USB scroll completion', 30)
        _, pixels = shot('scroll')
        for i in range(19, 35):
            row = (i - 19) * 2
            check(pixels.compare_text(row, 0, f'> echo l{i:02d}') is None, 'scroll')
            check(pixels.compare_text(row+1, 0, f'l{i:02d}') is None, 'scroll')
        check(reserved_unchanged(baseline.image, pixels.image), 'scroll')
        # Exit RSH locally. The resident prompt supports editing and displays CWD.
        s.key('esc')
        s.wait(lambda: s.word('rshell_active') == 0, 'RSH exit', 30)
        local('clear')
        prompt = 'OS32:/$ '
        s.wait(lambda: prompt in s.screen('prompt'), 'local prompt', 30)
        s.keys('echo ax')
        s.wait(lambda: prompt+'echo ax' in s.screen('cursor'), 'edited command', 30)
        _, pixels = shot('cursor')
        col = len(prompt+'echo ax')
        check(pixels.cursor_positions(0, len(prompt), 'echo ax  ') == [(0, col)], 'cursor')
        s.key('backspace')
        s.wait(lambda: prompt+'echo a ' in s.screen('backspace'), 'backspace', 30)
        s.key('ret')
        s.wait(lambda: 'a' in s.screen('edited').splitlines()[1].strip(), 'edited result', 30)
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    if not args.build_only and (not ROM.is_file() or not shutil.which('qemu-system-ppc')):
        print('SKIP ppc-fb: QEMU/OpenBIOS unavailable')
        return 3
    work = Path(tempfile.mkdtemp(prefix='fb-run-', dir=os.environ.get('TMPDIR')))
    seen = set()
    for case in ['normal'] + (list(MUTATIONS) if args.mutate else []):
        opts = mutant_settings(work, case, *MUTATIONS[case]) if case != 'normal' else []
        elf = build(work/case, 'IMAGE=kboot-link-sh', *opts)
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        require(digest not in seen, 'distinct framebuffer image')
        seen.add(digest)
        print(case, digest, flush=True)
        if args.build_only:
            continue
        for cpu in (['7450', '7455'] if args.cpu == 'both' else [args.cpu]) if case == 'normal' else ['7450']:
            try:
                observe(elf, work/(case+'-'+cpu), cpu, case)
            except PixelMismatch as exc:
                reason = {'colors': 'colors/top', 'top': 'colors/top', 'scroll-minus': 'scroll',
                          'scroll-plus': 'scroll'}.get(case, case)
                require(case != 'normal' and str(exc) == reason, f'{case}: {exc}')
                print('RED', case, cpu)
            else:
                require(case == 'normal', 'mutation survived: '+case)
                print('GREEN', case, cpu)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
