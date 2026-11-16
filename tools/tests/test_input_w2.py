#!/usr/bin/env python3
"""W[2] row validation, compiled directly without libc."""
CHECK = dict(name='input-w2', tier=1, budget=20, needs=[], covers=[
    'input/hal/input_policy.c', 'input/hal/input_policy.h', 'input/hal/keymap.h', 'include/io.h', 'include/app_ids.h',
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

MUTATIONS = [('row->context == context && context != 0', '0'),
                             ('row->context == 0) common = row;', '0) common = row;'),
                             ('|| r->flags)', ')'),
                             ('(check->bits[key / 8] & (1u << (key % 8)))', '0'),
                             ('+ r->index) * 16 + slot', '+ r->index) * 16'),
                             ('r->kind == 5 && (check->term_bytes[group * 32 + r->value / 8] & (1u << (r->value % 8)))', '0')]

def run(ctx):
    source = (ctx.root/'input/hal/input_policy.c').read_text()
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
    qemu = shutil.which('qemu-ppc')
    arches = ['x86']
    if cc.is_file() and qemu:
        arches.append('ppc')
    else:
        print('SKIP PowerPC runtime: compiler or qemu-ppc unavailable', flush=True)
        if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
    with tempfile.TemporaryDirectory(prefix='input-w2-') as d:
        tmp = Path(d)
        (tmp/'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned f) { (void)f; }\n')
        def build(code, arch):
            (tmp/'input_policy.c').write_text(code)
            cmd = ['gcc', '-m32', '-fno-pie', '-no-pie'] if arch == 'x86' else [str(cc), '-DPPC_TEST']
            cmd += ['-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                '-fno-stack-protector', '-nostdlib', '-static',
                '-I'+str(tmp), '-I'+str(ctx.root/'input/hal'), '-I'+str(ctx.root/'include'),
                '-I'+str(ctx.root/'sdk/include/os32'), str(tmp/'input_policy.c'),
                str(ctx.root/'tools/tests/input_w2_host.c'), '-o', str(tmp/'test')]
            subprocess.run(cmd, check=True)
            return subprocess.run([qemu, str(tmp/'test')]).returncode if arch == 'ppc' else host32.run([str(tmp/'test')], runner='qemu').returncode
        for arch in arches:
            assert build(source, arch) == 0
            print('PASS w2', arch, flush=True)
        if ctx.mutate:
            for old, new in MUTATIONS:
                assert source.count(old) == 1
                for arch in arches:
                    rc = build(source.replace(old, new), arch)
                    assert 0 < rc < 128, (arch, old, rc)
                    print('RED runtime:', arch, old, flush=True)
            for arch in arches: assert build(source, arch) == 0
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
