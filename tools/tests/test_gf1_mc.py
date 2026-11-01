"""NV44 MC initialization sequence and I/O failure propagation."""
CHECK = dict(name='gf1-mc-host', tier=1,
             covers=['hwdev/gf/mc.c', 'hwdev/gf/foundation.h',
                     'hwdev/gf/production.h', 'hwdev/gf/internal.h',
                     'tools/tests/gf1_mc_host.c', 'tools/tests/test_gf1_mc.py',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
             needs=[], budget=30.0)
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]

def main():
    with tempfile.TemporaryDirectory(prefix='gf1-mc-') as td:
        exe = Path(td) / 'host'
        source = (ROOT / 'hwdev/gf/mc.c').read_text()
        def build(text):
            path = Path(td) / 'mc.c'
            path.write_text(text.replace('#include "foundation.h"',
                '#include "' + str(ROOT / 'hwdev/gf/foundation.h') + '"').replace(
                '#include "production.h"', '#include "' + str(ROOT / 'hwdev/gf/production.h') + '"'))
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-nostdlib', '-static', '-no-pie', *include_flags(ROOT),
                            str(ROOT / 'tools/tests/gf1_mc_host.c'), str(path), '-o', str(exe)], check=True)
        build(source)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            assert host32.run([str(exe)], runner=runner).returncode == 0
            print('PASS MC sequence, repeated initialization and each I/O failure', flush=True)
        if '--mutate' in sys.argv:
            old = 'GF_MC_WIDTH, amount))'
            assert source.count(old) == 1
            build(source.replace(old, 'GF_MC_WIDTH, 0))', 1))
            assert host32.run([str(exe)], runner='qemu').returncode != 0
            print('MUTATE RED final limit value', flush=True)

if __name__ == '__main__':
    main()

def run(ctx):
    import shlex
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf1_mc.py',
                            *shlex.split('--mutate' if ctx.mutate else '')],
                           cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
