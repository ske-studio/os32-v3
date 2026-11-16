#!/usr/bin/env python3
"""Exercise S0b key evaluation without libc."""
CHECK = dict(name='keymap-s0b', tier=1, budget=5, needs=[], covers=[
    'input/hal/keymap_eval.c', 'input/hal/keymap.h', 'input/keymaps/hid_jp.c',
    'include/types.h', 'include/callconv.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/keymap_s0b_host.c', 'tools/tests/test_keymap_s0b.py',
    'tools/tests/host32.py'], basis=['INPUT_KEYMAP_DESIGN §2-4, §8, H0b'], kind='contract')
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32

ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    source = (ctx.root / 'input/hal/keymap_eval.c').read_text()
    with tempfile.TemporaryDirectory(prefix='keymap-s0b-') as td:
        tmp = Path(td)
        def build(src):
            (tmp / 'eval.c').write_text(src)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                            '-static', '-no-pie', '-I'+str(ctx.root), '-I'+str(ctx.root/'input/hal'),
                            '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32'),
                            str(tmp/'eval.c'), str(ctx.root/'input/keymaps/hid_jp.c'),
                            str(ctx.root/'tools/tests/keymap_s0b_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert (code := build(source)) == 0, code
        print('PASS S0b evaluation', flush=True)
        if ctx.mutate:
            for old, new in [
                ('result.sym = cell->sym;', 'result.sym = ctrl_char(cell->sym);'),
                ("(result.sym >= 'A' && result.sym <= 'Z')", "(result.sym >= '!' && result.sym <= 'Z')"),
                ('!!(state->mods & KM_MOD_MODE) ^ !!(state->locks & KM_LOCK_ALT)',
                 '!!(state->mods & KM_MOD_MODE) | !!(state->locks & KM_LOCK_ALT)'),
                ('col ^= 1;', 'col ^= 0;'),
                ('result.effect != 0 ||', '0 ||'),
                ('result.sym <= 0xf111', 'result.sym <= 0xf1ff'),
            ]:
                assert source.count(old) == 1
                assert build(source.replace(old, new)) != 0
                print('RED runtime:', old, flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
