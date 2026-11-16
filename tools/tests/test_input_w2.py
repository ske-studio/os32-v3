#!/usr/bin/env python3
"""W[2] row validation, compiled directly without libc."""
CHECK = dict(name='input-w2', tier=1, budget=5, needs=[], covers=[
    'input/hal/input_policy.c', 'input/hal/input_policy.h',
    'tools/tests/input_w2_host.c', 'tools/tests/test_input_w2.py',
    'tools/tests/host32.py', 'include/types.h', 'sdk/include/os32/os32_kapi_shared.h'],
    basis=['INPUT_KEYMAP_DESIGN §5 §8 wire W'], kind='contract')
from pathlib import Path
from types import SimpleNamespace
import os
import shutil
import subprocess
import sys
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    source = (ctx.root/'input/hal/input_policy.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-w2-') as d:
        tmp = Path(d)
        def build(code):
            (tmp/'input_policy.c').write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                '-I'+str(ctx.root/'input/hal'), '-I'+str(ctx.root/'include'),
                '-I'+str(ctx.root/'sdk/include/os32'), str(tmp/'input_policy.c'),
                str(ctx.root/'tools/tests/input_w2_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(source) == 0
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
        qemu = shutil.which('qemu-ppc')
        if cc.is_file() and qemu:
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-stack-protector', '-nostdlib', '-static', '-DPPC_TEST',
                '-I'+str(ctx.root/'input/hal'), '-I'+str(ctx.root/'include'),
                '-I'+str(ctx.root/'sdk/include/os32'), str(ctx.root/'input/hal/input_policy.c'),
                str(ctx.root/'tools/tests/input_w2_host.c'), '-o', str(tmp/'ppc')], check=True)
            subprocess.run([qemu, str(tmp/'ppc')], check=True)
            print('PASS big-endian PowerPC runtime', flush=True)
        else:
            print('SKIP PowerPC runtime: compiler or qemu-ppc unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
        if ctx.mutate:
            for old, new in [('|| r->flags)', ')'),
                             ('(check->bits[key / 8] & (1u << (key % 8)))', '0')]:
                assert source.count(old) == 1
                assert build(source.replace(old, new)) != 0
                print('RED runtime:', old, flush=True)
            assert build(source) == 0
    print('PASS W[2] row validation', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
