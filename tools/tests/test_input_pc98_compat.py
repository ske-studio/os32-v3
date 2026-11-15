#!/usr/bin/env python3
"""Pin the old PC-98 cooked mapping independently of the product tables."""
CHECK = dict(name='input-pc98-compat-host', tier=1, budget=5, needs=[], kind='characterization',
    basis=['INPUT_BIND_DESIGN §6 M0'], covers=['input/hal/kbd_merge.c', 'input/hal/in2_int.h',
    'drivers/input/kbd.h', 'include/**', 'sdk/include/os32/**', 'tools/tests/host_arch/**',
    'platform/pc98/platform_io.h', 'build/config.mk', 'tools/tests/kernel_includes.py',
    'tools/tests/host32.py', 'tools/tests/input_pc98_compat_host.c',
    'tools/tests/input_pc98_compat_fixture.h', 'tools/tests/test_input_pc98_compat.py'])
from pathlib import Path
import subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
def run(ctx):
    root = ctx.root
    source = (root/'input/hal/kbd_merge.c').read_text()
    old = subprocess.check_output(['git', 'show', 'ef67e51c:input/hal/kbd_merge.c'], cwd=root, text=True)
    old = old[old.index('static const u8 scancode_to_ascii['):old.index('void input_emit_legacy_repeat')]
    old = '#include "in2_int.h"\n#include "drivers/input/kbd.h"\n' + old.replace('input_emit_legacy_key(', 'pc98_old_emit(')
    with tempfile.TemporaryDirectory(prefix='pc98-compat-') as directory:
        tmp = Path(directory)
        (tmp/'old.c').write_text(old)
        def execute(code):
            (tmp/'current.c').write_text(code)
            cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                   '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                   '-nostdinc', '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                   '-I'+str(root/'tools/tests'), '-I'+str(root/'tools/tests/host_arch'),
                   '-I'+str(root/'input/hal'), *include_flags(root)]
            subprocess.run(cmd+[str(tmp/'current.c'), str(tmp/'old.c'), str(root/'tools/tests/input_pc98_compat_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        if ctx.mutate:
            for a,b in [("0x1B, '1', '2', '3'", "0x1B, 'X', '2', '3'"),
                        ('sc >= KEY_SHIFT && sc <= KEY_CTRL', 'sc >= KEY_SHIFT && sc <= KEY_CTRL && sc != KEY_KANA'),
                        ('if ((mods & SHIFT_CTRL) && ascii', 'if ((mods & SHIFT_GRPH) && ascii')]:
                assert source.count(a) == 1
                rc = execute(source.replace(a,b,1)); assert 0 < rc < 128, (a,rc)
                print('RED runtime:', a, flush=True)
            assert execute(source) == 0
        print('PASS PC-98 cooked literal and pinned old implementation', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
