#!/usr/bin/env python3
"""PowerMac display acceptance. Stage 5c and docs/KAPI_SPEC.md specify OF geometry, circular scanout, word-rounded transfers and CUI redraw."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import zlib
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


def absent_bindings(folder, out, case, slots=None):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = 'PROVIDER_GROUPS = {\n'
    slots = ABSENT[case] if slots is None else slots
    if source.count(anchor) != 1 or any(repr(name) in source for name in slots):
        raise RuntimeError(f'{case}: expected unique provider table and published slots')
    group = f"    'display_mut': (frozenset({slots!r}), frozenset()),\n"
    generate_bindings(folder, out, source.replace(anchor, anchor + group))


def generate_bindings(folder, out, source):
    (folder / 'kapi_target_ppc.py').write_text(source)
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--feature-link', '1', '--feature-ide', '1',
                    '--feature-fep', '1', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'mutant.stamp').touch()


def read_head(body, status, suffix):
    from run import require

    match = re.fullmatch(rb'DISPLAY HEAD ((?:-?\d+ ){12}-?\d+)\n' + suffix, body)
    require(match is not None and status == b'0\n', repr((body, status)))
    return tuple(map(int, match[1].split()))


# Provider-side mutations keep the public probe and its pixel oracle identical.
PROVIDER = 'platform/powermac/display_provider.c'
WIDTH = 'return m ? (int)m->width : 0;'
GATE = ('return present_owner && present_owner == appslot_present_owner() &&\n'
        '           a && a->kind == GFX2_KIND_OF_FB && a->state == GFX2_AD_OK &&\n'
        '           of_fb_ready() && present_generation == a->gen;')
COPY_ENTRY = 'static int surface_copy(void *c, u32 head, const struct gfx2_surf *s)\n{'
PRESENT_MUTATIONS = {
    'scroll-off': ('platform/powermac/display.c', 'of_fb_scroll(y);', '(void)y;'),
    'scroll-owner': ('gfx/gfx_scroll.c', 'if (!DISPLAY_SCROLL_OK()) return;', ''),
    # Scope the misplaced gate to redraws by a running app. Boot and target
    # acquisition must succeed for the glyph comparison to exercise a flip.
    'redraw-gated': ('drivers/video/of_fb/of_fb_v2.c', COPY_ENTRY,
                     '#include "display_provider.h"\n#include "exec/appslot.h"\n' + COPY_ENTRY +
                     '\n    if (appslot_cur() && s && !head && adapter_for(c)->head[0].scan_sid == s->sid'
                     ' && !DISPLAY_V1_PRESENT_OK()) return 0;'),
    'count-commit': ('drivers/video/of_fb/backend_of_fb.c',
                     'if (!rc) gfx_counters.commits++;', '(void)rc;'),
    'count-bytes': ('drivers/video/of_fb/backend_of_fb.c',
                    'gfx_counters.present_bytes += (end - first);', '(void)end;'),
    'width-768': (PROVIDER, WIDTH, 'return m ? (int)m->width - 32 : 0;'),
    'width-640': (PROVIDER, WIDTH, 'return m ? 640 : 0;'),
    'gate-open': (PROVIDER, GATE, 'return a != 0;'),
    'owner-unchecked': (PROVIDER, 'present_owner == appslot_present_owner() &&', ''),
}
TARGET_FAILURE = ('platform/powermac/console_text.c', '\n    rc = open_target();',
                  '\n    rc = -1;')
PROBES = {'scroll-off': ('scroll',), 'scroll-owner': ('scroll',), 'redraw-gated': ('shutdown',), 'width-768': ('edge',), 'width-640': ('dirty', 'full'),
          'gate-open': ('shutdown',), 'target-failure': ('shutdown',),
          'target-gate-open': ('shutdown',), 'owner-unchecked': ('owner',),
          'framebuffer-absent': ('framebuffer',), 'count-bytes': ('framebuffer',),
          'count-commit': ('framebuffer',), 'init-absent': ('init',), 'lease-kept': ('shutdown',), 'normal': ('edge', 'dirty', 'full', 'init',
                                                'owner', 'shutdown', 'framebuffer', 'scroll', 'scroll-restore')}


def present_pixels(s, evidence, case, serial, modes):
    from fb_pixel import Font, Terminal, read_ppm
    from run import require
    from usb_run import keyboard_actions, rsh_command

    # The kernel symbol and the INDEX8 RAM shadow's P2V alias are both
    # identity-mapped. Read physical RAM even while the probe's root is active.
    def physical_word(address):
        reply = s.q.call('human-monitor-command', {'command-line': f'xp /1wx {address:#x}'})
        match = re.search(r':\s*0x([0-9a-fA-F]+)', reply)
        require(match is not None, f'unreadable physical word at {address:#x}: {reply}')
        return int(match[1], 16)

    read_log = lambda: (evidence / 'serial-a.log').read_text(errors='replace')
    fg = int(re.search(r'FB text ready generation=\d+ fg=(\d+)', serial)[1])
    require(fg in (0, 255), 'OF palette endpoints')
    s.wait(lambda: 'protocol=01' in read_log(), 'USB keyboard')
    key, _ = keyboard_actions(s.q, read_log, s.wait, evidence)
    black, white = bytes(3), bytes([255]) * 3
    gate_open = case in ('gate-open', 'target-gate-open')
    for probe in modes:
        mode = 'scroll' if probe == 'scroll-restore' else probe
        line = f'/bin/dispprb.bin v1-{probe} {fg}'
        if case in ('framebuffer-absent', 'count-commit', 'count-bytes'):
            missing = case == 'framebuffer-absent'
            rsh_command(s, line, b'DISPLAY framebuffer failed\n' if missing else b'DISPLAY counters failed\n')
            rsh_command(s, 'echo $?', b'50\n' if missing else b'52\n')
            continue
        if case == 'init-absent':
            rsh_command(s, line, b'DISPLAY init absent\n')
            rsh_command(s, 'echo $?', b'42\n')
            continue

        def sample():
            phases = {'scroll-restore': 'bupdctr', 'scroll': 'bupdc'}.get(probe, 'be')
            for phase in phases:
                def waiting():
                    base = physical_word(s.symbols['bb_b'])
                    return base and physical_word(base) == (
                        0x50524200 + ord(phase))
                s.wait(waiting, f'{mode} checkpoint {phase}')
                require(s.word('g_cur') == 2 and s.word('cur_res_owner') == 2 and
                        s.word('exec_nest_level') == 2 and s.word('g_cur_app') != 0,
                        'running display child owns slot 2')
                name = f'{probe}-{phase}'
                path = evidence / (name + '.ppm')
                if ((mode == 'shutdown' and not case.startswith('target-') and
                     (phase == 'b' or not gate_open)) or (mode == 'scroll' and phase == 't')):
                    rows = s.screen(name).splitlines()
                    prefix = '> ' + line
                    row = next(i for i, text in enumerate(rows) if text.startswith(prefix))
                    terminal = Terminal(read_ppm(path), Font(ROOT / 'assets/fonts/shino16.kcgfont'),
                                        white, black, cursor_top=14)
                    require(terminal.compare_text(row, 0, prefix) is None, 'restored command glyphs')
                else:
                    s.q.call('screendump', {'filename': str(path)})
                frame = read_ppm(path)
                require((frame.width, frame.height) == (800, 600), 'OF dimensions')
                if mode == 'scroll':
                    if phase == 'b':
                        protected = [frame.pixel(704, y) for y in range(64)]
                    require(protected == [frame.pixel(704, y) for y in range(64)], 'physical protected band')
                    if phase in 'tr':
                        if phase == 'r':
                            require(frame.pixel(704, 65) == white and frame.pixel(704, 66) == black,
                                    'reinitialized logical rows 65 and 66')
                        key(phase)
                        continue
                    offset = int((phase in 'up' and case != 'scroll-off') or
                                 (phase == 'c' and case == 'scroll-owner'))
                    for y in (64, 65, 299, 300, 598, 599):
                        row = (y + offset) % 600
                        bright = row % 7 in (1, 4)
                        if phase in 'pdc' and row == 300:
                            bright = True
                        require(frame.pixel(704, y) == (white if bright else black),
                                repr((case, phase, y, row)))
                    key(phase)
                    continue
                spans = {'edge': (768, 800), 'dirty': (704, 768), 'full': (704, 800),
                         'init': (704, 712), 'framebuffer': (704, 712)}
                left, right = spans.get(mode, (704, 704))
                if mode == 'shutdown' and phase == 'b':
                    message = ('FB off step=target rc=-1' if case.startswith('target-')
                               else 'FB text ready generation=')
                    require(message in read_log()[start:], 'console recovery path ' + case)
                    baseline = frame
                    require(any(frame.pixel(x, y) != white for y in range(560, 576)
                                for x in range(704, 712)), 'shadow poison must be visible')
                if phase == 'b' or case.startswith('width-'):
                    right = left
                elif gate_open or case == 'owner-unchecked':
                    left, right = 704, 800
                for y in range(560, 576):
                    for x in range(704, 800):
                        wanted = white if left <= x < right else black
                        if mode == 'shutdown' and not left <= x < right:
                            wanted = baseline.pixel(x, y)
                        require(frame.pixel(x, y) == wanted, repr((case, mode, phase, x, y)))
                key(phase)

        start = len(read_log())
        payload = rsh_command(s, line, during=sample)
        if probe == 'scroll-restore':
            timing = re.search(rb'DISPLAY scroll 4 calls ticks=(\d+) bytes=(\d+)', payload)
            require(timing and int(timing[2]) == 4 * 800 * (600 - 64), 'four scroll transfers')
            (evidence / 'scroll-time.txt').write_bytes(timing[0] + b'\n')
        rsh_command(s, 'echo $?', b'0\n')


def observe_osri(session, evidence, case):
    """KAPI_SPEC §4-16: same valid input for normal, absent and swapped slots."""
    from run import require
    from usb_run import rsh_command

    from fb_pixel import Font, Terminal, read_ppm

    def rows_visible():
        session.wait(lambda: session.word('osri_visible') == 1, 'dynamic OSRI visible')
        def pixels_match():
            path = evidence / 'osri-rows.ppm'
            session.q.call('screendump', {'filename': str(path)})
            image = read_ppm(path)
            return all(image.pixel(x, y) == bytes([255 if y > 64 or x % 8 == 0 else 0]) * 3
                       for y in (64, 65, 463) for x in (0, 1, 7, 8, 639))

        session.wait(lambda: session.word('osri_visible') == 1 and pixels_match(),
                     'dynamic OSRI row palette update')

    if case in ('osri-absent', 'osri-slot-swap'):
        rsh_command(session, '/bin/dispprb.bin osri', b'OSRI NOSYS\n')
        rsh_command(session, 'echo $?', b'0\n')
        return

    from usb_run import keyboard_actions
    read_log = lambda: (evidence / 'serial-a.log').read_text(errors='replace')
    session.wait(lambda: 'protocol=01' in read_log(), 'OSRI USB keyboard')
    key, _ = keyboard_actions(session.q, read_log, session.wait, evidence)

    marker = 'osri-terminal-ready'
    rsh_command(session, 'clear')
    rsh_command(session, 'echo ' + marker, marker.encode() + b'\n')
    before = session.screen('osri-before').splitlines()
    row = next(i for i, line in enumerate(before) if marker in line)
    column = before[row].index(marker)
    require(row < 25 and column + len(marker) <= 80, 'marker inside 640x400 OSRI')
    font = Font(ROOT / 'assets/fonts/shino16.kcgfont')

    def glyphs(name):
        image = read_ppm(evidence / (name + '.ppm'))
        terminal = Terminal(image, font, bytes([255]) * 3, bytes(3))
        return terminal.compare_text(row, column, marker) is None

    require(glyphs('osri-before'), 'marker glyphs before OSRI')

    def check_restore():
        rows_visible()
        require(not glyphs('osri-rows'), 'OSRI covered marker glyphs')
        session.wait(lambda: session.word('osri_active') == 0, 'dynamic OSRI returned')
        session.q.call('screendump', {'filename': str(evidence / 'osri-after.ppm')})
        require(glyphs('osri-after') == (case != 'osri-restore-skipped'),
                'marker glyphs at original position after OSRI')
        key('r')

    rsh_command(session, '/bin/dispprb.bin osri', b'OSRI DONE\n', during=check_restore)
    rsh_command(session, 'echo $?', b'0\n')
    if case == 'osri-restore-skipped':
        return

    def fixed_pixels():
        session.wait(lambda: session.word('osri_visible') == 1, 'fixed OSRI visible')
        start = session.word('tick_count')
        session.wait(lambda: (session.word('tick_count') - start) % 2**32 >= 4,
                     'fixed image beyond 1 ms request')
        path = evidence / 'osri-fixed.ppm'
        session.q.call('screendump', {'filename': str(path)})
        image = read_ppm(path)
        for y in (64, 65, 463):
            for x in (0, 1, 7, 8, 639):
                color = bytes([255 if x % 8 == 0 else 0]) * 3
                require(image.pixel(x, y) == color, f'fixed OSRI pixel {x},{y}')
        key('esc')

    rsh_command(session, '/bin/dispprb.bin osri-fixed', b'OSRI ESC\n', during=fixed_pixels)
    rsh_command(session, 'echo $?', b'0\n')
    fault = rsh_command(session, '/bin/dispprb.bin osri-fault')
    require(b'[Process crashed]' in fault, 'OSRI caller fault')
    rsh_command(session, 'echo $?', b'139\n')


def observe(media, cpu, evidence, case, modes=(), osri=False):
    from link_sh_run import Session
    from kboot_run import serial as read_serial, validate_root
    from run import require
    from usb_run import rsh_command

    session = Session(media, cpu, evidence, 240)
    try:
        session.start()
        session.wait(lambda: validate_root(read_serial(evidence/'serial-a.log'), read_serial(evidence/'serial-b.log')),
                     'rd0 registration and page accounting', 60)
        session.wait(session.ready, 'display cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'display RSH mode')
        serial = (evidence / 'serial-a.log').read_text(errors='replace')
        require('FB text ready' in serial, 'INDEX8 framebuffer console attached')
        if osri:
            observe_osri(session, evidence, case)
            session.q.call('quit')
            session.p.wait(timeout=5)
            require(session.p.returncode == 0, 'OSRI QEMU exit')
            print(('GREEN ' if case == 'normal' else 'RED ') + case + ' ' + cpu, flush=True)
            return
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
        if modes and case in ('normal', 'redraw-gated', 'lease-kept'):
            from fb_pixel import Font, Terminal, read_ppm
            from usb_run import keyboard_actions
            read_log = lambda: (evidence / 'serial-a.log').read_text(errors='replace')
            session.wait(lambda: 'protocol=01' in read_log(), 'USB keyboard ready')
            key, _ = keyboard_actions(session.q, read_log, session.wait, evidence)
            marker = 'GFX2 display restored'

            def glyphs_match(name, text):
                rows = session.screen(name).splitlines()
                row = next((i for i, line in enumerate(rows) if text in line), None)
                if row is None:
                    return False
                terminal = Terminal(read_ppm(evidence / (name + '.ppm')),
                                    Font(ROOT / 'assets/fonts/shino16.kcgfont'),
                                    bytes([255]) * 3, bytes(3), cursor_top=14)
                return terminal.compare_text(row, rows[row].index(text), text) is None

            def check_glyphs(name, text, visible=True):
                # The cells can be ahead of the pixels (the last glyph may still
                # be under the cursor): wait for the drawn glyphs, bounded.
                if visible:
                    session.wait(lambda: glyphs_match(name, text), 'console glyphs for ' + text, 10)
                else:
                    require(not glyphs_match(name, text), 'console glyphs for ' + text)

            def release():
                def printed():
                    rows = session.screen('restored').splitlines()
                    return any(marker in row for row in rows)
                session.wait(printed, 'live probe printed after shutdown')
                check_glyphs('restored', marker, visible=case != 'redraw-gated')
                key('r')

            rsh_command(session, '/bin/gfx2prob.bin shutdown', during=release)
            rsh_command(session, 'echo $?', b'0\n')
            rsh_command(session, 'echo gfx2-ready', b'gfx2-ready\n')
            if case == 'lease-kept':
                # A9d mutant: the CLIENT lease is not revoked, so the store succeeds.
                body = rsh_command(session, '/bin/gfx2prob.bin lease')
                require(b'lease released\n' in body and b'[Process crashed]' not in body,
                        'lease-kept: store after shutdown completed')
                rsh_command(session, 'echo $?', b'0\n')
            if case == 'normal':
                check_glyphs('shell-after-shutdown', '> echo gfx2-ready')
                body = rsh_command(session, '/bin/gfx2prob.bin lease')
                require(b'lease released\n' in body and b'[Process crashed]' in body,
                        'lease store reached after shutdown and probe faulted')
                rsh_command(session, 'echo $?', b'139\n')
                rsh_command(session, 'echo lease-ready', b'lease-ready\n')
                check_glyphs('shell-after-lease', 'lease released')
            rsh_command(session, '/bin/gfx2prob.bin owner')
            rsh_command(session, 'echo $?', b'0\n')
            rsh_command(session, '/bin/gfx2prob.bin owned-child')
            rsh_command(session, 'echo $?', b'0\n')
        if modes and case not in ('redraw-gated', 'lease-kept'):
            present_pixels(session, evidence, case, serial, modes)
        # target-failure / target-gate-open: the display target cannot be
        # re-acquired, so gfx_init in a gfx2 probe fails before the gates
        # ("KCG unavailable -> CUI", exit 1). Their gate RED stays on the v1
        # present pixels; the gfx2 gate RED is taken by gate-open below.
        # A9a/A9c (5c confirmed P2): the gfx2 gates' RED is taken with gfx2 calls,
        # not only the v1 present. shutdown-off returns 34 when mode_set, flip
        # or present is not BUSY after shutdown; owned-child returns 36 when a
        # non-owner child's gfx2_present is not BUSY.
        if case == 'gate-open' and modes:
            rsh_command(session, '/bin/gfx2prob.bin shutdown-off')
            rsh_command(session, 'echo $?', b'34\n')
        if case == 'owner-unchecked' and modes:
            rsh_command(session, '/bin/gfx2prob.bin owned-child')
            rsh_command(session, 'echo $?', b'36\n')
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'gfx2 QEMU exit')
        print(('GREEN ' + case if case in ('normal', 'target-failure') else 'RED ' + case) + ' ' + cpu, flush=True)
    finally:
        session.close()


def osri_fixture(path, fixed=False):
    """OSRI v2 layout in gfx/gfx_osri.h, one palette update on row 1."""
    size = 128096 if fixed else 128096 + 8
    data = bytearray(size)
    struct.pack_into('<4s4H2BH6I4H', data, 0, b'OSRI', 2, 64, 640, 400,
                     4, 1, 0, size, 64, 96, 128000, 128096, int(not fixed), 8, 0, 1, 0)
    struct.pack_into('<H', data, 66, 0xfff)
    data[96:96 + 32000] = bytes([0x80]) * 32000
    if not fixed:
        struct.pack_into('<HBBHH', data, 128096, 1, 0, 0, 0xfff, 0)
    struct.pack_into('<I', data, 48, zlib.crc32(data))
    path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--osri', action='store_true', help='exercise slot 257 and OF scanout')
    group.add_argument('--present', action='store_true', help='exercise public drawing and display recovery')
    parser.add_argument('--case', action='append', help='build selected mutation and its normal control')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if any(not os.access(cross / 'bin' / ('powerpc-elf-' + tool), os.X_OK)
           for tool in ('gcc', 'ld', 'ar', 'nm', 'objcopy', 'objdump')):
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3

    from kboot_accept import build_media, mutant_settings
    from ppc_shm import guest_available

    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-display-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / (app + '.bin')) for app in ('hello', 'dispprb', 'gfx2prob')]
    if args.osri:
        fixture = work / 'rows.osr'
        osri_fixture(fixture)
        fixed = work / 'fixed.osr'
        osri_fixture(fixed, fixed=True)
        bins.extend(map(str, (fixture, fixed)))
    options = ['IMAGE=kboot-link-sh', 'KBOOT_USB_KEY_TRACE=1', 'PPC_USER_FLOAT=', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    images = {}
    cases = ('normal', *EXPECTED, EXIT_MODE_CASE) if args.mutate else ('normal',)
    if args.present and args.mutate:
        cases += ('framebuffer-absent', *PRESENT_MUTATIONS, 'target-failure', 'target-gate-open')
        cases += ('init-absent', 'lease-kept')
    if args.osri:
        cases = ('normal', 'osri-absent', 'osri-slot-swap', 'osri-restore-skipped') if args.mutate else ('normal',)
    if args.case:
        unknown = set(args.case) - set(cases)
        if unknown:
            parser.error('unavailable cases: ' + ', '.join(sorted(unknown)))
        cases = tuple(case for case in cases if case == 'normal' or case in args.case)
    for case in cases:
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        if case in ABSENT:
            absent_bindings(folder, kernel / 'gen', case)
        if case == 'osri-absent':
            absent_bindings(folder, kernel / 'gen', case, ('gfx_osri_show',))
        if case == 'init-absent':
            absent_bindings(folder, kernel / 'gen', case, ('gfx_init_200',))
        if case == 'framebuffer-absent':
            absent_bindings(folder, kernel / 'gen', case, ('gfx_get_framebuffer',))
        mutation = []
        if case == 'osri-slot-swap':
            init = (ROOT / 'exec/exec_kapi_init.inc').read_text()
            for slot, other in [('gfx_osri_show', 'sys_set_mtime'), ('sys_set_mtime', 'gfx_osri_show')]:
                assignment = f'kapi->{slot} = wrap_{slot};'
                assert init.count(assignment) == 1
                init = init.replace(assignment, f'kapi->{slot} = wrap_{other};')
            (folder / 'swapped_init.inc').write_text(init)
            mutation = mutant_settings(folder, case, 'exec/exec.c',
                                       '#include "exec_kapi_init.inc"', '#include "swapped_init.inc"')
        if case == 'osri-restore-skipped':
            # transfer() in this include copies the saved terminal rows back.
            include = '#include "of_fb_osri.inc"'
            mutation = mutant_settings(folder, case, 'drivers/video/of_fb/backend_of_fb.c',
                                       include, '#define transfer(...) ((void)src)\n' + include +
                                       '\n#undef transfer')
        if case in PRESENT_MUTATIONS:
            mutation = mutant_settings(folder, case, *PRESENT_MUTATIONS[case])
        if case in ('target-failure', 'target-gate-open'):
            mutation = mutant_settings(folder, case, *TARGET_FAILURE)
            if case == 'target-gate-open':
                extra = mutant_settings(folder, 'gate-open', *PRESENT_MUTATIONS['gate-open'])
                mutation.extend(arg.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1) for arg in extra)
        if case == 'lease-kept':
            # The display's surface code is an .inc; mutate the revoke itself.
            mutation = mutant_settings(folder, case, 'exec/lease.c',
                                       '        if (a && a->as && a->as->pd_phys) failed += lease_revoke_sid(a->as, sid);',
                                       '        (void)a; (void)sid;')
        if case == EXIT_MODE_CASE:
            anchor = '    int poison = 0;'
            mutation = mutant_settings(folder, case, 'gfx/v2/owner_release.c', anchor,
                anchor + '\n    struct gfx2_adapter *active = gfx2_active();'
                         '\n    if (active) active->head[0].mode_id ^= 1u;')
        # Compare identical RAM-root bytes even when the ISO builder timestamps files.
        if case != 'normal':
            iso = str(normal_media.elf.parent / 'root.iso')
            mutation.extend(['ROOT_ISO=' + iso, '--old-file=' + iso])
        media = build_media(kernel, *options, *mutation, 'kb-user', 'kernel')
        if case == 'normal':
            normal_media = media
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {media}')
        images[digest] = case
        print(f'{case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                modes = PROBES.get(case, ()) if args.present else ()
                observe(media, cpu, folder / cpu, case, modes, args.osri)
                if case != 'normal':
                    observe(normal_media, cpu, folder / (cpu + '-control'), 'normal', modes, args.osri)
    print(f'{len(images)} distinct link-sh ELF images', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
