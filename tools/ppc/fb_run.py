#!/usr/bin/env python3
"""Check the resident terminal against the KCG glyphs and the INDEX8 geometry.

The first 64 scanlines belong to the boot display. Local command echo follows
cmd_rshell in userland/shell/rshell.c. Per docs/design/lib/kprintf.md,
PowerMac kernel diagnostics go to the log sinks. The text-ready diagnostic
on console restoration must reach serial while staying off the local screen.
"""
import argparse
import hashlib
import os
import re
from pathlib import Path
import shutil
import tempfile

from fb_pixel import Font, Terminal, read_ppm, reserved_unchanged
from kboot_accept import build_media, mutant_settings
from kboot_run import ROOT, ROM, require, serial, validate_root
from link_sh_run import Session, text_fixture
from usb_run import before_shell_input


class PixelMismatch(AssertionError):
    pass


def check(condition, detail):
    if not condition:
        raise PixelMismatch(detail)


MUTATIONS = {
    'utf8-bytewise': ('kernel/console/console.c', '        cp = dec.codepoint;\n        p += dec.bytes_used;\n        remaining -= dec.bytes_used;',
                      '        cp = *p;\n        p += dec.bytes_used;\n        remaining -= dec.bytes_used;'),
    'trace-screen': ('platform/powermac/console_text.c', '\n    kprintf(KBOOT_ATTR, "FB text ready',
                     '\n    console_printf(KBOOT_ATTR, "FB text ready generation=%u\\n", target.generation);'
                     '\n    kprintf(KBOOT_ATTR, "FB text ready'),
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


def observe(media, out, cpu, case):
    s = Session(media, cpu, out, 600)
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
        s.agent_enabled = False
        s.start()
        s.wait(lambda: 'protocol=01' in serial(out/'serial-a.log'), 'keyboard enumeration', 60)
        s.key('esc')
        s.agent_enabled = True
        if case == 'start-consumed':
            s.wait(s.ready, 'RSH READY after consumed ESC', 60)
            check('[Remote shell closed]' in s.screen('preloaded'), 'start-consumed')
        s.close_rshell(preloaded=True)
        s.wait(lambda: validate_root(serial(out/'serial-a.log'), serial(out/'serial-b.log')),
               'rd0 registration and page accounting', 60)
        prompt = 'OS32:/$ '
        def finished(text, result):
            # The command is done when its whole result line is on screen
            # and a fresh prompt follows it (no snapshot mid-output).
            lines = [l.rstrip() for l in text.splitlines()]
            rows = [i for i, l in enumerate(lines) if re.fullmatch(result, l)]
            return bool(rows) and any(l.startswith(prompt.rstrip()) for l in lines[rows[-1]+1:])
        # TIME completes after the HELLO-established digit has been drawn.
        # Link-up alone precedes both HELLO digits in the reserved band.
        s.wait(lambda: s.word('pm_link_time_ok') != 0, 'HELLO display before baseline', 60)
        initial, baseline = shot('boot')
        check('[sqlite] static rc=0' not in initial, 'trace-screen')
        local('clear')
        local('echo first')
        s.wait(lambda: finished(s.screen('first'), r'first'), 'USB first line', 30)
        text, pixels = shot('first')
        check(prompt+'echo first' in text, 'start-consumed')
        check(pixels.compare_text(0, 0, prompt+'echo first') is None, 'colors/top')
        check(pixels.compare_text(1, 0, 'first') is None, 'USB output')
        local('clear')
        local('cat /bin/sample.txt')
        s.wait(lambda: finished(s.screen('text'), r'ASCII .+'), 'fd1 output', 30)
        _, original = shot('text')
        check(original.compare_text(1, 0, 'ASCII ｶﾅ 日本語') is None, 'utf8-bytewise')
        check(reserved_unchanged(baseline.image, original.image), 'top')
        local('clear')
        for i in range(35):
            local(f'echo l{i:02d}')
        # A broken scroll never shows 'l34' followed by a prompt; do not turn
        # that into a timeout — photograph after the deadline and let the
        # pixel comparison below decide (positive mismatch evidence).
        try:
            s.wait(lambda: finished(s.screen('scroll'), r'l34'), 'USB scroll completion', 30)
        except RuntimeError:
            pass
        _, pixels = shot('scroll')
        for i in range(19, 35):
            row = (i - 19) * 2
            check(pixels.compare_text(row, 0, prompt+f'echo l{i:02d}') is None, 'scroll')
            check(pixels.compare_text(row+1, 0, f'l{i:02d}') is None, 'scroll')
        check(reserved_unchanged(baseline.image, pixels.image), 'scroll')
        local('clear')
        s.wait(lambda: prompt in s.screen('prompt'), 'local prompt', 30)
        s.keys('echo ax')
        edited = prompt + 'echo ax'
        col = len(edited)

        def cursor_drawn():
            # shell_putchar updates the cell before it moves the fbcon underline.
            # Require both in the same stopped frame, including the blank tail.
            shadow, pixels = shot('cursor')
            return (shadow.splitlines()[0].startswith(edited)
                    and pixels.compare_text(0, 0, edited + '  ', ((0, col),)) is None)

        s.wait(cursor_drawn, 'edited command and cursor', 30)
        s.key('backspace')
        s.wait(lambda: prompt+'echo a ' in s.screen('backspace'), 'backspace', 30)
        s.key('ret')
        s.wait(lambda: finished(s.screen('edited'), r'a'), 'edited result', 30)

        def generations():
            return re.findall(r'FB text ready generation=(?:[^\n]*\n)??(\d+) fg=', serial(out/'serial-a.log'))

        before = generations()
        require(bool(before), 'initial text generation')
        local('source /bin/leave.sh')
        s.wait(lambda: generations()[:-1] == before and generations()[-1] != before[-1],
               'tile owner exit restores a new generation', 30)
        s.wait(lambda: finished(s.screen('leave'), r'0'), 'tile leave status and prompt', 30)
        check('FB text ready generation=' not in s.screen('leave'), 'trace-screen')
        local('clear')
        local('cat /bin/sample.txt')

        def restored():
            text, frame = shot('restored')
            return (finished(text, r'ASCII .+') and
                    frame.compare_text(1, 0, 'ASCII ｶﾅ 日本語') is None)

        s.wait(restored, 'restored UTF-8 result and prompt', 30)
        _, frame = shot('restored')
        for y in range(original.top + 16, original.top + 32):
            for x in range(16 * 12):
                check(original.image.pixel(x, y) == frame.image.pixel(x, y), 'restored glyph pixels')
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
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    leave = work/'leave.sh'
    leave.write_text('/bin/dispprb.bin leave\necho $?\n')
    user = work/'user'
    bins = ' '.join((text_fixture(work), str(leave), str(user/'dispprb.bin')))
    seen = set()
    for case in ['normal'] + (list(MUTATIONS) if args.mutate else []):
        opts = mutant_settings(work, case, *MUTATIONS[case]) if case != 'normal' else []
        media = build_media(work/case, 'IMAGE=kboot-link-sh', 'KB_SH_OUT='+str(user), 'KB_ROOT_BINS='+bins,
                    *before_shell_input(work), *opts)
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        require(digest not in seen, 'distinct framebuffer image')
        seen.add(digest)
        print(case, digest, flush=True)
        if args.build_only:
            continue
        for cpu in (['7450', '7455'] if args.cpu == 'both' else [args.cpu]) if case == 'normal' else ['7450']:
            try:
                observe(media, work/(case+'-'+cpu), cpu, case)
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
