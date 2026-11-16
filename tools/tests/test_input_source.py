#!/usr/bin/env python3
"""Input source allocation and native event layout contract."""
CHECK = dict(name='input-source-host', tier=1, budget=5, needs=[], covers=[
    'input/hal/in2_int.h', 'input/hal/in2_dev.c', 'input/hal/keymap.h', 'include/types.h',
    'include/io.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/input_source_host.c', 'tools/tests/test_input_source.py',
    'tools/tests/host32.py'], basis=['INPUT_HAL_DESIGN §3'], kind='contract')
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32

ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    header = (ctx.root / 'input/hal/in2_int.h').read_text()
    source = (ctx.root / 'input/hal/in2_dev.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-source-') as td:
        tmp = Path(td)
        (tmp / "keymap.h").write_text((ctx.root / "input/hal/keymap.h").read_text())
        (tmp / 'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned x) { (void)x; }\n')
        def build(h, s):
            (tmp / 'in2_int.h').write_text(h)
            (tmp / 'in2_dev.c').write_text(s)
            cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                   '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')]
            subprocess.run(cmd + [str(tmp/'in2_dev.c'), str(ctx.root/'tools/tests/input_source_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(header, source) == 0
        print('PASS x86 source contract', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        if cc.is_file() and os.access(cc, os.X_OK):
            for f in [tmp/'in2_dev.c', ctx.root/'tools/tests/input_source_host.c']:
                subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-DPPC_COMPILE', '-I'+str(tmp), '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32'), '-c', str(f), '-o', str(tmp/(f.stem+'.o'))], check=True)
            print('PASS PowerPC cross compile', flush=True)
        else:
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
        if ctx.mutate:
            full = source.replace('sources[IN2_MAX_DEV]', 'sources[IN2_MAX_DEV + 1]').replace('i < IN2_MAX_DEV', 'i <= IN2_MAX_DEV')
            assert build(header, full) == 3
            print('RED runtime: 17th source accepted', flush=True)
            for old, new in [('sources[i].generation++;', '(void)0;'),
                             ('sources[src - 1].generation == generation', '(sources[src - 1].generation == generation || 1)')]:
                assert source.count(old) == 1
                assert build(header, source.replace(old, new)) != 0
                print('RED runtime: ' + old, flush=True)
            old = 'u8 src, type;'
            assert header.count(old) == 1
            mutant = header.replace(old, 'u8 type, src;').replace('#define IN2_OFFSET(field, value) STATIC_ASSERT(__builtin_offsetof(InputEvent, field) == value, input_event_##field)', '#define IN2_OFFSET(field, value)')
            assert build(mutant, source) == 1
            print('RED runtime: event field order', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
