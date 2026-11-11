#!/usr/bin/env python3
"""Exercise C17 with real page tables and a runtime mutant."""
CHECK = dict(name='gfx2-map-audit-host', tier=1, needs=[], budget=3,
    covers=['gfx/v2/map_audit.c', 'gfx/v2/gfx2_int.h', 'mm/paging.h',
            'tools/tests/gfx2_map_audit_host.c', 'tools/tests/test_gfx2_map_audit.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]

def observe(source, work):
    src = work / 'map_audit.c'
    src.write_text(source)
    exe = work / 'audit'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        *include_flags(ROOT), '-I' + str(ROOT / 'gfx/v2'), str(src),
        str(ROOT / 'tools/tests/gfx2_map_audit_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/map_audit.c').read_text()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        assert observe(source, work) == 0
        if args.mutate:
            anchor = 'return have == want ? 0 : -1;'
            assert source.count(anchor) == 1
            assert observe(source.replace(anchor, 'return have >= want ? 0 : -1;'), work) == 3
            print('RED extra present PTE: runtime rc=3')
    print('PASS C17 mapping audit')

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
