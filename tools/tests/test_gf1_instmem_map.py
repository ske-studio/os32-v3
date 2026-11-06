"""BAR2 mapping lifecycle and production paging adapter."""
CHECK = dict(name='gf1-instmem-map-host', tier=1,
             covers=['tools/tests/gf_guard_fake.h', 'hwdev/gf/instmem_map.c', 'hwdev/gf/foundation.h',
                     'tools/tests/gf1_instmem_map_host.c', 'tools/tests/test_gf1_instmem_map.py',
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
    with tempfile.TemporaryDirectory(prefix='gf1-map-') as td:
        exe = Path(td) / 'host'
        source = (ROOT / 'hwdev/gf/instmem_map.c').read_text()
        def build(text):
            path = Path(td) / 'instmem_map.c'
            for inc in ['foundation.h', '../../mm/paging.h', '../../include/memmap.h', '../../drivers/video/geforce/geforce_module.h']:
                text = text.replace('"' + inc + '"', '"' + str((ROOT / 'hwdev/gf' / inc).resolve()) + '"')
            path.write_text(text)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fno-pie', '-nostdlib', '-static', '-no-pie', *include_flags(ROOT), str(ROOT / 'tools/tests/gf1_instmem_map_host.c'), str(path), '-o', str(exe)], check=True)
        build(source)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            assert host32.run([str(exe)], runner=runner).returncode == 0
        print('PASS mapping, refusal, release and restart', flush=True)
        if '--mutate' in sys.argv:
            old = 'base + bytes - 1'
            assert source.count(old) == 1
            build(source.replace(old, 'base + bytes', 1))
            assert host32.run([str(exe)], runner='qemu').returncode != 0
            print('MUTATE RED inclusive end', flush=True)
if __name__ == '__main__': main()
def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf1_instmem_map.py', *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
