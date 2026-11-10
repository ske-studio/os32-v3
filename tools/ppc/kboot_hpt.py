#!/usr/bin/env python3
"""NOTEST: standalone C2 product HPT acceptance and runtime mutations."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

# Standalone stage check; deliberately outside tools/checks discovery.
CHECK = dict(name='kboot-hpt', tier=1,
             covers=['arch/ppc/kernel_hpt.c', 'arch/ppc/ppc_hpt.h',
                     'arch/ppc/ppc_frame.h', 'arch/ppc/ppc_oea.h',
                     'arch/ppc/ppc_boot.h', 'arch/ppc/ppc_pd.h', 'arch/ppc/pd_walk.c',
                     'include/types.h', 'include/callconv.h', 'mm/paging.h',
                     'tools/ppc/kboot_hpt.py', 'tools/ppc/kboot_hpt_host.c'],
             needs=[], budget=5.0)

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('secondary-h', 'u32 hi = key | (secondary ? PPC_PTE_H : 0);', 'u32 hi = key;'),
    ('old-va-offset', '(page << PPC_PAGE_SHIFT)', '((page ^ 1) << PPC_PAGE_SHIFT)'),
    ('old-va-segment', '((vsid & 15) << 28)', '((vsid & 7) << 28)'),
    ('hi-before-lo',
     'ppc_hpt_write(&hpt[free_slot + 1], (t->pa & PPC_PAGE_MASK) | t->attr);\n'
     '    ppc_eieio();\n    ppc_hpt_write(&hpt[free_slot], free_hi | PPC_PTE_V);',
     'ppc_hpt_write(&hpt[free_slot], free_hi | PPC_PTE_V);\n'
     '    ppc_eieio();\n    ppc_hpt_write(&hpt[free_slot + 1], (t->pa & PPC_PAGE_MASK) | t->attr);'),
    ('protection-fill', '(f->dsisr & ~PPC_DSISR_STORE) != PPC_DSISR_MISS',
     '(f->dsisr & PPC_DSISR_MISS) == 0'),
    ('duplicate-fill', 'if (old == (hi | PPC_PTE_V))', 'if (0)'),
    ('htabmask', 'HPT_HTABMASK = 1', 'HPT_HTABMASK = 0'),
    ('hash-ten-bits', 'HPT_HASH_MASK = 0x7ff', 'HPT_HASH_MASK = 0x3ff'),
    ('clear-short', 'i < HPT_BYTES / sizeof(u32)', 'i < HPT_BYTES / sizeof(u32) - 1'),
    ('invalidate-current', 'ppc_hpt_tlbie(old_va);', 'ppc_hpt_tlbie(old_va ^ 0x1000);'),
]


def execute(source, output):
    subprocess.run(['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-m32', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                    '-DPPC_HPT_HOST_TEST', '-D__cdecl=', '-I.', '-Iinclude', '-Ilib',
                    '-Iarch/ppc', str(source), 'arch/ppc/pd_walk.c',
                    'tools/ppc/kboot_hpt_host.c', '-o', str(output)], cwd=ROOT, check=True)
    return subprocess.run(['qemu-i386', str(output)], cwd=ROOT, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    product = ROOT / 'arch/ppc/kernel_hpt.c'
    with tempfile.TemporaryDirectory(prefix='hpt-', dir=os.environ.get('TMPDIR')) as tmp:
        tmp = Path(tmp)
        result = execute(product, tmp / 'baseline')
        print(f'baseline runtime rc={result.returncode}', flush=True)
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
                print(f'RED {name}: runtime rc={result.returncode}', flush=True)


if __name__ == '__main__':
    main()
