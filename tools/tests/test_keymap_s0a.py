#!/usr/bin/env python3
"""S0a registry contract, linked without libc."""
CHECK = dict(name='keymap-s0a', tier=1, budget=10, needs=[], covers=[
    'input/hal/keymap_registry.c', 'input/hal/keymap.h', 'input/hal/in2_int.h',
    'input/keymaps/hid_jp.c', 'include/types.h', 'include/io.h',
    'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/keymap_s0a_host.c',
    'tools/tests/test_keymap_s0a.py', 'tools/tests/host32.py'],
    basis=['INPUT_KEYMAP_DESIGN §8, H0b'], kind='contract')
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32

ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    source = (ctx.root / 'input/hal/keymap_registry.c').read_text()
    with tempfile.TemporaryDirectory(prefix='keymap-s0a-') as td:
        tmp = Path(td)
        for name in ('keymap.h', 'in2_int.h'):
            (tmp / name).write_text((ctx.root / 'input/hal' / name).read_text())
        (tmp / 'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned f) { (void)f; }\n')
        files = [tmp/'registry.c', ctx.root/'input/keymaps/hid_jp.c', ctx.root/'tools/tests/keymap_s0a_host.c']
        includes = ['-I'+str(tmp), '-I'+str(ctx.root), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')]
        common = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding'] + includes
        def build(s):
            (tmp/'registry.c').write_text(s)
            subprocess.run(['gcc', '-m32', *common, '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', *map(str, files), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(source) == 0
        print('PASS x86 registry and source lifecycle', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
        if cc.is_file() and os.access(cc, os.X_OK):
            objects = []
            for i, path in enumerate(files):
                obj = tmp / ('ppc%d.o' % i)
                subprocess.run([str(cc), *common, '-DPPC_COMPILE', '-c', str(path), '-o', str(obj)], check=True)
                objects.append(str(obj))
            subprocess.run([str(cc), '-nostdlib', '-r', *objects, '-o', str(tmp/'ppc.o')], check=True)
            print('PASS PowerPC registry link', flush=True)
        else:
            print('SKIP PowerPC link: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
        if ctx.mutate:
            for old, new in [
                ('bindings[src - 1].generation == generation)', '(generation || !generation))'),
                ('if (maps[map->id - 1])', 'if (0)')]:
                assert source.count(old) == 1
                assert build(source.replace(old, new)) != 0
                print('RED runtime: ' + old, flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
