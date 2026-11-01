"""NV44 instance allocation and aperture access."""
CHECK = dict(name='gf1-instmem-host', tier=1,
             covers=['hwdev/gf/instmem.c', 'hwdev/gf/foundation.h',
                     'hwdev/gf/internal.h', 'tools/tests/gf1_instmem_host.c',
                     'tools/tests/test_gf1_instmem.py', 'tools/tests/host32.py',
                     'tools/tests/kernel_includes.py'], needs=[], budget=30.0)
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]

def main():
    with tempfile.TemporaryDirectory(prefix='gf1-instmem-') as td:
        exe = Path(td) / 'host'
        source = (ROOT / 'hwdev/gf/instmem.c').read_text()
        def build(text):
            path = Path(td) / 'instmem.c'
            path.write_text(text.replace('#include "foundation.h"',
                '#include "' + str(ROOT / 'hwdev/gf/foundation.h') + '"'))
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie', *include_flags(ROOT),
                            str(ROOT / 'tools/tests/gf1_instmem_host.c'), str(path), '-o', str(exe)], check=True)
        build(source)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            assert host32.run([str(exe)], runner=runner).returncode == 0
        print('PASS instmem allocation and access', flush=True)
        if '--mutate' in sys.argv:
            old = '*v = *(volatile u32 *)(f->bar2 + obj->off + off);'
            assert source.count(old) == 1
            build(source.replace(old, '*v = *(volatile u32 *)(f->bar2 + obj->off + off + f->imem.vram_bytes - f->imem.reserved);', 1))
            assert host32.run([str(exe)], runner='qemu').returncode != 0
            print('MUTATE RED aperture-relative address', flush=True)

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf1_instmem.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
