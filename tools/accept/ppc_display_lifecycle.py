#!/usr/bin/env python3
"""Exercise PowerMac fullscreen ownership, drawing and console recovery."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import tempfile

from ppc_display import ROOT, absent_bindings
from fb_pixel import Font, Terminal, read_ppm
from kboot_accept import build, mutant_settings
from link_sh_run import Session
from ppc_shm import guest_available
from run import require
from usb_run import rsh_command


def image(folder, user, case):
    kernel = folder / 'kernel'
    changes = []
    if case == 'no-return':
        changes = mutant_settings(folder, case, 'gfx/gfx_core.c',
                                  '    display_provider_console_return();', '')
    elif case == 'no-exit':
        source = 'exec/exec_finish.inc'
        original = (ROOT / source).read_text()
        hook = '    exec_gfx_owner_exit(id);'
        require(original.count(hook) == 1, 'unique exit hook')
        (folder / 'exec_finish.inc').write_text(original.replace(hook, ''))
        changes = mutant_settings(folder, case, 'exec/exec.c', '#include "exec_finish.inc"',
                                  '#include "' + str(folder / 'exec_finish.inc') + '"')
    elif case == 'any-owner':
        changes = mutant_settings(folder, case, 'platform/powermac/console_text.c',
                                  'id == appslot_gfx_owner() && ', '(void)id, ')
    elif case == 'absent':
        absent_bindings(folder, kernel / 'gen', case, ('gfx2_surface_create',))
    bins = [str(user / (app + '.bin')) for app in ('hello', 'dispprb')]
    return build(kernel, 'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                 'KB_ROOT_BINS=' + ' '.join(bins), *changes, 'kb-user', 'kernel')


def observe(elf, folder, case):
    s = Session(elf, '7450', folder, 240)
    serial = folder / 'serial-a.log'

    def generations():
        return re.findall(rb'FB text ready generation=(\d+)', serial.read_bytes())

    def probe(mode, status, marker):
        body = rsh_command(s, '/bin/dispprb.bin ' + mode)
        result = rsh_command(s, 'echo $?')
        require(result == str(status).encode() + b'\n' and marker in body,
                repr((mode, body, result)))
        return body

    def check_echo(mode):
        word = 'restored' + mode
        s.keys('echo ' + word + '\n')
        output_row = None

        def prompt_after_output():
            nonlocal output_row
            rows = s.screen(mode).splitlines()
            for row, (line, following) in enumerate(zip(rows, rows[1:])):
                if line.rstrip() == word and following.rstrip() == '>':
                    output_row = row
                    return True
            return False

        s.wait(prompt_after_output, mode + ' echo output and prompt')
        pixels = Terminal(read_ppm(folder / (mode + '.ppm')),
                          Font(ROOT / 'assets/fonts/shino16.kcgfont'),
                          (255, 255, 255), (0, 0, 0), cursor_top=14)
        require(pixels.compare_text(output_row, 0, word) is None, 'keyboard and console pixels')

    try:
        s.start()
        s.wait(s.ready, 'shell ready', 60)
        require(generations() and s.agent.operator.ready['mode'] == 'cui', 'boot console')
        if case == 'absent':
            probe('draw', 22, b'tile provider absent')
        elif case == 'any-owner':
            probe('draw', 30, b'child changed generation')
        else:
            modes = {'no-return': ('draw',), 'no-exit': ('leave',)}.get(
                case, ('shutdown', 'draw', 'leave', 'fault'))
            for mode in modes:
                if mode == 'fault':
                    capacity = probe('capacity', 0, b'free tiles ')
                before = generations()
                probe(mode, 139 if mode == 'fault' else 0,
                      {'shutdown': b'shutdown only', 'draw': b'tile drawn', 'leave': b'tile left', 'fault': b'tile allocated'}[mode])
                after = generations()
                if case == 'normal':
                    require(after[:-1] == before and after[-1] != before[-1], mode + ' recovery')
                    if mode == 'fault':
                        require(capacity == probe('capacity', 0, b'free tiles '), 'surface recovery')
                    check_echo(mode)
                else:
                    require(after == before, case + ' unexpectedly restored')
        s.q.call('quit')
        s.p.wait(timeout=5)
        require(s.p.returncode == 0, 'QEMU exit')
        print(('GREEN ' if case == 'normal' else 'RED ') + case, flush=True)
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file() or (not args.build_only and not guest_available()):
        print('SKIP: PowerMac acceptance environment unavailable')
        return 3
    work = Path(tempfile.mkdtemp(prefix='ppc-lifecycle-', dir=os.environ.get('TMPDIR'))).resolve()
    print('evidence=' + str(work), flush=True)
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    hashes = set()
    cases = ('normal', 'no-return', 'no-exit', 'any-owner', 'absent') if args.mutate else ('normal',)
    for case in cases:
        folder = work / case
        folder.mkdir()
        elf = image(folder, work / 'user', case)
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        require(digest not in hashes, 'duplicate image ' + case)
        hashes.add(digest)
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            observe(elf, folder / 'guest', case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
