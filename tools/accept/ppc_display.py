#!/usr/bin/env python3
"""PowerMac display acceptance. Stage 5c expects fixed OF geometry, word-rounded transfers and CUI redraw after shutdown."""
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
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'stamp').touch()


def read_head(body, status, suffix):
    from run import require

    match = re.fullmatch(rb'DISPLAY HEAD ((?:-?\d+ ){12}-?\d+)\n' + suffix, body)
    require(match is not None and status == b'0\n', repr((body, status)))
    return tuple(map(int, match[1].split()))


# Provider-side mutations keep the public probe and its pixel oracle identical.
PROVIDER = 'platform/powermac/display_provider.c'
WIDTH = 'return m ? (int)m->width : 0;'
GATE = ('return present_owner && present_owner == appslot_cur() &&\n'
        '           a && present_generation == a->gen;')
COPY_ENTRY = 'static int surface_copy(void *c, u32 head, const struct gfx2_surf *s)\n{'
PRESENT_MUTATIONS = {
    # Scope the misplaced gate to redraws by a running app. Boot and target
    # acquisition must succeed for the glyph comparison to exercise a flip.
    'redraw-gated': ('drivers/video/of_fb/of_fb_v2.c', COPY_ENTRY,
                     '#include "display_provider.h"\n#include "exec/appslot.h"\n' + COPY_ENTRY +
                     '\n    if (appslot_cur() && s && !head && adapter_for(c)->head[0].scan_sid == s->sid'
                     ' && !DISPLAY_V1_PRESENT_OK()) return 0;'),
    'count-commit': ('drivers/video/of_fb/backend_of_fb.c',
                     'if (!rc) gfx_counters.commits++;', '(void)rc;'),
    'count-bytes': ('drivers/video/of_fb/backend_of_fb.c',
                    'if (bottom > y) gfx_counters.present_bytes +=', 'if (0) gfx_counters.present_bytes +='),
    'width-768': (PROVIDER, WIDTH, 'return m ? (int)m->width - 32 : 0;'),
    'width-640': (PROVIDER, WIDTH, 'return m ? 640 : 0;'),
    'gate-open': (PROVIDER, GATE, 'return a != 0;'),
    'owner-unchecked': (PROVIDER, 'present_owner == appslot_cur() &&', ''),
}
TARGET_FAILURE = ('platform/powermac/console_text.c', '\n    rc = open_target();',
                  '\n    rc = -1;')
PROBES = {'redraw-gated': ('shutdown',), 'width-768': ('edge',), 'width-640': ('dirty', 'full'),
          'gate-open': ('shutdown',), 'target-failure': ('shutdown',),
          'target-gate-open': ('shutdown',), 'owner-unchecked': ('owner',),
          'framebuffer-absent': ('framebuffer',), 'count-bytes': ('framebuffer',),
          'count-commit': ('framebuffer',), 'init-absent': ('init',), 'lease-kept': ('shutdown',), 'normal': ('edge', 'dirty', 'full', 'init',
                                                'owner', 'shutdown', 'framebuffer')}


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
    for mode in modes:
        line = f'/bin/dispprb.bin v1-{mode} {fg}'
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
            for phase in ('b', 'e'):
                def waiting():
                    base = physical_word(s.symbols['bb_b'])
                    return base and physical_word(base) == (
                        0x50524200 + ord(phase))
                s.wait(waiting, f'{mode} checkpoint {phase}')
                name = f'{mode}-{phase}'
                path = evidence / (name + '.ppm')
                if (mode == 'shutdown' and not case.startswith('target-') and
                        (phase == 'b' or not gate_open)):
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
        rsh_command(s, line, during=sample)
        rsh_command(s, 'echo $?', b'0\n')


def observe(elf, cpu, evidence, case, modes=()):
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
        if case == 'target-failure' and modes:
            rsh_command(session, '/bin/gfx2prob.bin shutdown-off')
            rsh_command(session, 'echo $?', b'0\n')
        # A9a/A9c (5c confirmed P2): the gfx2 gates' RED is taken with gfx2 calls,
        # not only the v1 present. shutdown-off returns 34 when mode_set, flip
        # or present is not BUSY after shutdown; owned-child returns 36 when a
        # non-owner child's gfx2_present is not BUSY.
        if case in ('gate-open', 'target-gate-open') and modes:
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--present', action='store_true', help='exercise public drawing and display recovery')
    parser.add_argument('--case', action='append', help='build selected mutation and its normal control')
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
    # This acceptance invokes these probes only. The build also includes sh.
    bins = [str(user / (app + '.bin')) for app in ('dispprb', 'gfx2prob')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    images = {}
    cases = ('normal', *EXPECTED, EXIT_MODE_CASE) if args.mutate else ('normal',)
    if args.present and args.mutate:
        cases += ('framebuffer-absent', *PRESENT_MUTATIONS, 'target-failure', 'target-gate-open')
        cases += ('init-absent', 'lease-kept')
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
        if case == 'init-absent':
            absent_bindings(folder, kernel / 'gen', case, ('gfx_init_200',))
        if case == 'framebuffer-absent':
            absent_bindings(folder, kernel / 'gen', case, ('gfx_get_framebuffer',))
        mutation = []
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
                modes = PROBES.get(case, ()) if args.present else ()
                observe(elf, cpu, folder / cpu, case, modes)
                if case != 'normal':
                    observe(normal_elf, cpu, folder / (cpu + '-control'), 'normal', modes)
    print(f'{len(images)} distinct link-sh ELF images', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
