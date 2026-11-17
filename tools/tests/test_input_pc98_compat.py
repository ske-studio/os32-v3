#!/usr/bin/env python3
"""Pin the old PC-98 cooked mapping independently of the product tables."""
CHECK = dict(name='input-pc98-compat-host', tier=1, budget=5, needs=[], kind='characterization',
    basis=['INPUT_BIND_DESIGN §6 M0'], covers=['tools/tests/input_s3_differences.tsv', 'docs/decisions/INPUT_BIND_DESIGN.md', 'tools/tests/input_pc98_compat_host.c', 'tools/tests/kernel_includes.py', 'input/hal/legacy_keymap.c', 'input/hal/keymap.h', 'input/hal/keymap_eval.c', 'input/keymaps/pc98_jp.c', 'tools/tests/keymap_s3_oracle.py', 'tools/tests/input_pc98_bsd_fixture.kbd', 'tools/tests/test_input_pc98_bsd.py', 'input/hal/in2_int.h',
    'drivers/input/kbd.h', 'include/**', 'sdk/include/os32/**', 'tools/tests/host_arch/**',
    'platform/pc98/platform_io.h', 'build/config.mk', 'tools/tests/kernel_includes.py',
    'tools/tests/host32.py', 'tools/tests/input_pc98_compat_host.c',
    'tools/tests/input_pc98_compat_fixture.h', 'tools/tests/test_input_pc98_compat.py'])
from pathlib import Path
import subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from keymap_s3_oracle import c_oracle
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
def run(ctx):
    root = ctx.root
    source = (root/'input/hal/legacy_keymap.c').read_text()
    old = subprocess.check_output(['git', 'show', 'ef67e51c:input/hal/kbd_merge.c'], cwd=root, text=True)
    old = old[old.index('static const u8 scancode_to_ascii['):old.index('void input_emit_legacy_repeat')]
    old = '#include "in2_int.h"\n#include "drivers/input/kbd.h"\n' + old.replace('input_emit_legacy_key(', 'pc98_old_emit(')
    with tempfile.TemporaryDirectory(prefix='pc98-compat-') as directory:
        tmp = Path(directory)
        (tmp/'old.c').write_text(old)
        (tmp/'s3_expected.h').write_text(c_oracle(root))
        def execute(code):
            (tmp/'current.c').write_text(code)
            cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                   '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                   '-nostdinc', '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                   '-I'+str(tmp), '-I'+str(root/'tools/tests'), '-I'+str(root/'tools/tests/host_arch'),
                   '-I'+str(root/'input/hal'), *include_flags(root)]
            subprocess.run(cmd+[str(tmp/'current.c'), str(tmp/'old.c'), str(root/'input/hal/keymap_eval.c'), str(root/'input/keymaps/pc98_jp.c'), str(root/'tools/tests/input_pc98_compat_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        if ctx.mutate:
            for a,b in [('if (mods & SHIFT_CAPS)', 'if (0 && (mods & SHIFT_CAPS))'),
                        ('result->is_modifier ||', '0 ||'),
                        ('sc == KEY_STOP && input_key_pressed(KEY_CTRL)', 'sc == KEY_STOP && 0')]:
                assert source.count(a) == 1
                rc = execute(source.replace(a,b,1)); assert 0 < rc < 128, (a,rc)
                print('RED runtime:', a, flush=True)
            assert execute(source) == 0
        print('PASS PC-98 BSD fallback and pinned old literal implementation', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
