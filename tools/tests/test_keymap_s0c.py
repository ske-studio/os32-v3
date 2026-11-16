#!/usr/bin/env python3
"""Exercise S0c state directly, without libc."""
CHECK = dict(name='keymap-s0c', tier=1, budget=5, needs=[], covers=[
    'input/hal/keymap_state.c', 'input/hal/keymap.h', 'input/hal/in2_int.h',
    'include/types.h', 'include/callconv.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/keymap_s0c_host.c', 'tools/tests/test_keymap_s0c.py',
    'tools/tests/host32.py'], basis=['INPUT_KEYMAP_DESIGN §8, H0b'], kind='contract')
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32

ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    product = (ctx.root / 'input/hal/keymap_state.c').read_text()
    with tempfile.TemporaryDirectory(prefix='keymap-s0c-') as td:
        tmp = Path(td)
        def build(source):
            (tmp / 'state.c').write_text(source)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                            '-nostdlib', '-static', '-no-pie',
                            '-I' + str(ctx.root / 'input/hal'), '-I' + str(ctx.root / 'include'),
                            '-I' + str(ctx.root / 'sdk/include/os32'), str(tmp / 'state.c'),
                            str(ctx.root / 'tools/tests/keymap_s0c_host.c'), '-o', str(tmp / 'test')], check=True)
            return host32.run([str(tmp / 'test')], runner='qemu').returncode
        assert build(product) == 0
        print('PASS S0c state and SHIFTA')
        if ctx.mutate:
            for old, new in [
                ('key->down && !old_down', 'key->down'),
                ('mods |= saved[i].mods;', 'mods = saved[i].mods;'),
                ('shared_locks ^= KM_LOCK_ALT;', 'shared_locks |= KM_LOCK_ALT;')]:
                assert product.count(old) == 1
                assert build(product.replace(old, new)) != 0
                print('RED runtime:', old)
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
