#!/usr/bin/env python3
"""C4 surface lifetimes; real product TU, no libc, runtime mutation evidence."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source):
    path = work / 'display.c'
    path.write_text(source)
    exe = work / 'display'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(ROOT / 'gfx'), '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'gfx/v2'),
        '-I' + str(ROOT / 'mm'), '-I' + str(ROOT / 'lib'),
        '-I' + str(ROOT / 'arch/x86'), '-I' + str(ROOT / 'sdk/include/os32'),
        str(path), str(ROOT / 'tools/tests/gfx2_display_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/display.c').read_text()
    mutations = [
        ('pending reference', 'gfx2_scan_ref_get(s);\n    a->head[head].pend_sid = sid;', 'a->head[head].pend_sid = sid;'),
        ('palette bounds', 'count > 256 - first', 'count > 257 - first'),
        ('dirty retained on failure', 'if (rc) return OS32_ERR_IO;\n    s->dirty', 'if (rc) { s->dirty.w = 0; return OS32_ERR_IO; }\n    s->dirty'),
    ]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        rc = observe(work, source)
        assert rc == 0, f'baseline rc={rc}'
        print('GREEN surface lifetime, rollback, aliases, states and deferred free', flush=True)
        if args.mutate:
            for name, anchor, replacement in mutations:
                assert source.count(anchor) == 1, (name, source.count(anchor))
                mutated = source.replace(anchor, replacement)
                rc = observe(work, mutated)
                assert rc > 0, f'{name}: expected runtime RED, rc={rc}'
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
