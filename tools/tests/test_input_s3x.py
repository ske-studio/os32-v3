#!/usr/bin/env python3
"""S3x: real pinned deliver/capture/legacy reads, and physical rescue under remap."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
from test_input_native import function
from test_input_s2x import PATHS, WRAP
CHECK = dict(
    name='input-s3x-host',
    tier=1,
    budget=45,
    needs=[],
    kind='characterization',
    basis=['INPUT_KEYMAP_DESIGN §4, §7 S3x, §8 本体 S3x', 'INPUT_BIND_DESIGN §2 M3'],
    covers=['tools/tests/input_s3_differences.tsv',
 'docs/decisions/INPUT_BIND_DESIGN.md',
 'tools/tests/input_pc98_compat_host.c',
 'tools/tests/kernel_includes.py',
 'drivers/input/kbd_hal.c',
 'drivers/input/kbd.c',
 'drivers/input/*.h',
 'platform/pc98/input_keymap.c',
 'input/keymaps/*.c',
 'input/hal/*.c',
 'input/hal/*.h',
 'include/**',
 'sdk/include/os32/**',
 'tools/tests/host_arch/**',
 'platform/pc98/platform_io.h',
 'build/config.mk',
 'tools/tests/kernel_includes.py',
 'tools/tests/host32.py',
 'tools/tests/test_input_s2x.py',
 'tools/tests/input_s2x_host.c',
 'tools/tests/test_input_native.py',
 'tools/tests/input_native_host.c',
 'tools/tests/keymap_s3_oracle.py',
 'tools/tests/input_pc98_bsd_fixture.kbd',
 'tools/tests/test_input_pc98_bsd.py',
 'tools/tests/test_input_s3x.py',
 'tools/tests/input_s3x_host.c',
 'tools/tests/test_input_s2x.py'],
)
ROOT = Path(__file__).resolve().parents[2]
BASE = '5e197b8d'
KBD = 'drivers/input/kbd.c'

def run(ctx):
    root = ctx.root
    paths = PATHS + [KBD]
    current = {p: (root/p).read_text() for p in paths}
    previous = {p: subprocess.check_output(['git', 'show', BASE+':'+p], cwd=root, text=True)
                for p in paths}
    with tempfile.TemporaryDirectory(prefix='input-s3x-') as td:
        tmp = Path(td)
        def build(texts, remap=False):
            files = []
            for i, p in enumerate(PATHS):
                def relocate(m):
                    target = root / Path(p).parent / m[1]
                    return '#include "'+str(target.resolve())+'"' if target.is_file() else m[0]
                path = tmp/(str(i)+'.c')
                path.write_text(re.sub(r'^#include "([^"]+)"', relocate, texts[p], flags=re.M))
                files.append(str(path))
            (tmp/'s2x_deliver.inc').write_text(function(texts[KBD], 'kbd_deliver'))
            exe = tmp/'s3x'
            cmd = ['gcc', '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                   '-static', '-no-pie', '-I'+str(tmp), '-I'+str(root/'tools/tests/host_arch'),
                   '-I'+str(root/'input/hal'), '-I'+str(root), *include_flags(root)]
            if remap: cmd.append('-DS3X_REMAP')
            subprocess.run(cmd + files + [str(root/'tools/tests/input_s3x_host.c')] +
                           ['-Wl,--wrap='+n for n in WRAP] + ['-o', str(exe)], check=True)
            return exe
        def execute(texts, remap=False, lock=0):
            return host32.run([str(build(texts, remap))], runner='qemu',
                              input=bytes([lock, 0]), capture_output=True)
        # Unmodified baseline files from the requested SHA, no oracle rewriting.
        for lock in (0, 2, 4, 6):
            old = execute(previous, lock=lock)
            new = execute(current, lock=lock)
            assert old.returncode == new.returncode == 0
            assert old.stdout == new.stdout, ('baseline trace mismatch', lock)
        assert execute(current, True).returncode == 0
        print('PASS S3x: real 5e197b8d trace identical; 128 scans x 32 mods x 4 BIOS locks, CUI/raw/repeat/V86/STOP/XFER/NFER; remapped physical rescue', flush=True)
        if ctx.mutate:
            anchor = 'scancode == KEY_STOP && input_key_pressed(KEY_CTRL)'
            for replacement in (
                'scancode == KEY_STOP && ({ KmState s; km_state_snapshot(&s); s.mods & KM_MOD_CTRL; })',
                'scancode == KEY_STOP && !input_key_pressed(KEY_CTRL)'):
                assert current[KBD].count(anchor) == 1
                mutant = dict(current)
                mutant[KBD] = mutant[KBD].replace(anchor, replacement)
                result = execute(mutant, True)
                assert 0 < result.returncode < 128, result.returncode
                print('RED runtime S3x:', replacement, 'rc=', result.returncode, flush=True)
            assert execute(current, True).returncode == 0
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
