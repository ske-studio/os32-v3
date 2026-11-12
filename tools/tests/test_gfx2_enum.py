#!/usr/bin/env python3
"""C11 enumeration boundary and mutation test."""
import argparse
from pathlib import Path
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'gfx/v2/enum.c'

def observe(work, source):
    src = work / 'enum.c'
    src.write_text(source)
    exe = work / 'test'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie']
    cmd += ['-I' + str(ROOT / p) for p in ('gfx/v2', 'lib', 'include', 'sdk/include/os32', 'mm', 'arch/x86', 'platform/pc98')]
    host32.build(cmd + [str(src), str(ROOT / 'tools/tests/gfx2_enum_host.c'), str(ROOT / 'lib/kstring_c.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], timeout=30).returncode

def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args = p.parse_args()
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix='gfx2-enum-') as td:
        work = Path(td)
        assert observe(work, source) == 0
        print('GREEN enumeration', flush=True)
        if args.mutate:
            old = 'if (i >= (u32)count) return OS32_ERR_INVAL;'
            assert source.count(old) == 1
            rc = observe(work, source.replace(old, 'if (i > (u32)count) return OS32_ERR_INVAL;'))
            assert rc > 0, rc
            print(f'RED boundary: rc={rc}', flush=True)
            old = 'if (a->head[head].conn_mask) v.connector_mask = a->head[head].conn_mask;'
            assert source.count(old) == 1
            rc = observe(work, source.replace(old, 'v.connector_mask = a->head[head].conn_mask;'))
            assert rc > 0, rc
            print(f'RED connector mask: rc={rc}', flush=True)
            assert observe(work, source) == 0
            print('GREEN restored', flush=True)
if __name__ == '__main__': main()
