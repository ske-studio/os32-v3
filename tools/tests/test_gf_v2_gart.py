#!/usr/bin/env python3
"""G5: exercise the real GART TU with fake instmem and channel lifecycle."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'drivers/video/geforce/gf_v2_gart.c'


def observe(work, source):
    path = work / 'gart.c'
    path.write_text(source)
    exe = work / 'gart'
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-I' + str(ROOT), '-I' + str(SOURCE.parent), *include_flags(ROOT),
        str(path), str(ROOT / 'tools/tests/gf_v2_gart_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    mutations = [
        ('rollback', '(void)pte_write(g, chosen + k, 0);', '(void)k;'),
        ('best fit', 'gap >= n && gap < best', 'gap >= n && best > GF_V2_GART_PAGES'),
        ('replay', 'for (u32 i = 0; i < g->gart.count; i++) {\n        const struct gf_v2_gart_bind *b = &g->gart.bind[i];\n        for (u32 j = 0; j < b->n; j++) {', None),
        ('epoch', 'g->gart.epoch == g->ch->epoch', 'g->gart.epoch != 0'),
    ]
    with tempfile.TemporaryDirectory(prefix='gfx2-gart-') as td:
        work = Path(td)
        assert observe(work, source) == 0, 'baseline failed'
        print('GREEN G5 page table, holes, rollback, ranges, lifecycle and failures', flush=True)
        if args.mutate:
            for name, anchor, replacement in mutations:
                if name == 'replay':
                    replacement = anchor.replace('i < g->gart.count', 'i < g->gart.count && dl == 0')
                assert source.count(anchor) == 1, (name, 'nonunique anchor')
                rc = observe(work, source.replace(anchor, replacement))
                assert rc > 0, (name, rc, 'expected runtime RED')
                print(f'RED {name}: runtime rc={rc}', flush=True)
            assert observe(work, source) == 0
            print('GREEN restored source', flush=True)


if __name__ == '__main__':
    main()
