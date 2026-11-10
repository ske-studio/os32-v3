#!/usr/bin/env python3
"""Exercise the diagnostic snapshot and its runtime mutation boundaries."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'platform/powermac/lan_diag.c'
CHECK = dict(name='ppc-lan-p24d', tier=1, budget=60, needs=[],
             covers=['platform/powermac/lan_diag.c'],
             inputs=['tools/ppc/test_lan_p24d.py', 'tools/ppc/lan_p24d_test.c',
                     'platform/powermac/lan_boot.h', 'drivers/net/gem/gem.h',
                     'platform/powermac/kboot_info.h', 'arch/ppc/ppc_pd.h'])
MUTATIONS = [
    ('range', 'status, 17, memory, 8, bytes, ranges, 2,', 'status, 17, memory, 8, bytes, ranges, 1,', 'range', 'H:A10 read-range'),
    ('hi-lo', 'status[offset] = (u32)(value >> 32);', 'status[offset] = (u32)value;', 'status', 'H:status'),
    ('offset', 'put32(72 + i * 4, memory[i])', 'put32(73 + i * 4, memory[i])', 'diag', 'H:diag'),
    ('irq', 'irq_restore(flags);', '(void)flags;', 'irq', 'H:irq'),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        path = Path(tmp)
        cases = [('baseline', original, None, None)]
        if args.mutate:
            for name, old, new, marker, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError('non-unique anchor: ' + name)
                cases.append((name, original.replace(old, new), marker, diagnostic))
        for name, source, marker, diagnostic in cases:
            (path / 'lan_diag.c').write_text(source)
            cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-I'+tmp, '-I.', '-Iplatform/powermac', '-Iarch/ppc', '-Iinclude', 'tools/ppc/lan_p24d_test.c', '-o', str(path/'test')]
            c = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=60)
            if c.returncode: raise RuntimeError(name+' compile: '+c.stderr)
            r = subprocess.run(['qemu-i386', str(path/'test')], capture_output=True, text=True, timeout=60)
            if marker is None:
                if r.returncode != 0 or not r.stdout.endswith('PASS\n'): raise RuntimeError(name+': '+r.stdout+r.stderr)
                print('baseline OK')
            else:
                if r.returncode != 1 or 'reached='+marker+'\n' not in r.stdout or not r.stdout.endswith(diagnostic+'\n') or r.stderr: raise RuntimeError(name+': '+r.stdout+r.stderr)
                print('RED '+name+': reached='+marker+' '+diagnostic)

if __name__ == '__main__':
    main()
