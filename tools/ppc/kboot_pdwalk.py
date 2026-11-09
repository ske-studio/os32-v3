#!/usr/bin/env python3
"""NOTEST: standalone C1 product walker acceptance and runtime mutations."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('pde-present', 'if (!(pde & PTE_PRESENT))', 'if (pde & PTE_PRESENT)'),
    ('pte-present', 'if (!(pte & PTE_PRESENT))', 'if (pte & PTE_PRESENT)'),
    ('permissions-or', 'u32 effective = pde & pte;', 'u32 effective = pde | pte;'),
    ('cache-priority',
     'if (pte & PTE_PCD)\n        cache = PD_I | PD_G;\n    else if (pte & PTE_PWT)\n        cache = PD_W;',
     'if (pte & PTE_PWT)\n        cache = PD_W;\n    else if (pte & PTE_PCD)\n        cache = PD_I | PD_G;'),
    ('pp-encoding', 'PD_PP_USER_RW = 2', 'PD_PP_USER_RW = 3'),
    ('pat-accepted', 'if (pte & PTE_PS)', 'if (0)'),
    ('ps-accepted', 'if (pde & PTE_PS)', 'if (0)'),
    ('root-unaligned-accepted', '(root & ~PTE_FRAME_MASK)', '0'),
]


def execute(source, output):
    subprocess.run(['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-m32', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                    '-D__cdecl=', '-I.', '-Iinclude', '-Ilib', '-Iarch/ppc',
                    str(source), 'tools/ppc/kboot_pdwalk_host.c', '-o', str(output)],
                   cwd=ROOT, check=True)
    return subprocess.run(['qemu-i386', str(output)], cwd=ROOT, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    product = ROOT / 'arch/ppc/pd_walk.c'
    with tempfile.TemporaryDirectory(prefix='pdwalk-', dir=os.environ.get('TMPDIR')) as tmp:
        tmp = Path(tmp)
        result = execute(product, tmp / 'baseline')
        print('baseline runtime rc=' + str(result.returncode))
        if result.returncode:
            raise SystemExit(result.stderr)
        if args.mutate:
            original = product.read_text()
            for name, anchor, replacement in MUTATIONS:
                if original.count(anchor) != 1:
                    raise SystemExit(f'{name}: nonunique mutation anchor')
                changed = tmp / f'{name}.c'
                changed.write_text(original.replace(anchor, replacement))
                result = execute(changed, tmp / name)
                if result.returncode != 1:
                    raise SystemExit(f'{name}: expected runtime assertion RED, got {result}')
                print(f'RED {name}: runtime rc={result.returncode}')


if __name__ == '__main__':
    main()
