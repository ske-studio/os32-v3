#!/usr/bin/env python3
"""Exercise the product GEM ring on a freestanding ILP32 host."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-lan-p15', tier=2, budget=60,
             covers=['drivers/net/gem/gem_ring.c', 'tools/ppc/test_lan_p15.py', 'tools/ppc/lan_p15_fake.c'],
             inputs=['drivers/net/gem/gem.h', 'include/types.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('fifo-units', 'g->io.read32(g->io.ctx, GEM_RX_FIFO_SIZE) * 64u', 'g->io.read32(g->io.ctx, GEM_RX_FIFO_SIZE)', 'H:publish'),
    ('own', '| (u32)GEM_RD_OWN', '| 0', 'H:descriptor'),
    ('full', 'g->ring_cursor < GEM_RX_PUBLISHED', 'g->ring_cursor < GEM_RING_COUNT', 'H:sync'),
    ('sync', 'g->io.sync(base + GEM_POOL_RX_RING, GEM_RING_COUNT * GEM_DESC_SIZE, DMA_PREWRITE)', '0', 'H:sync'),
    ('order', 'GEM_TX_RING_PTR_HI, 0', 'GEM_TX_RING_PTR_LO, 0', 'H:publish'),
    ('fboff', '(2u << GEM_RX_CONFIG_FBOFF_SHFT)', '(0u << GEM_RX_CONFIG_FBOFF_SHFT)', 'H:publish'),
    ('master', 'if (g->io.master_enable(g->io.ctx)) return GEM_EIO;\n        g->ring_publish_step = 2;\n    }\n    /* gem_init:811-834: DMA enable follows master readback; MAC stays off. */\n    if (g->ring_publish_step == 2) {', 'g->ring_publish_step = 2;\n    }\n    if (g->ring_publish_step == 2) {\n        g->io.write32(g->io.ctx, GEM_TX_CONFIG, 1);\n        if (g->io.master_enable(g->io.ctx)) return GEM_EIO;', 'H:publish'),
]
def check(source, temp):
    exe = temp / 'test'
    cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude', '-Idrivers/net/gem', str(source), 'tools/ppc/lan_p15_fake.c', '-o', str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True)
    return subprocess.run(['qemu-i386', str(exe)], cwd=ROOT, text=True, capture_output=True, timeout=30)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'drivers/net/gem/gem_ring.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as directory:
        temp = Path(directory)
        source = temp / 'gem_ring.c'
        source.write_text(original)
        result = check(source, temp)
        if result.returncode or 'REACHED host' not in result.stdout.splitlines():
            raise RuntimeError('baseline: ' + result.stdout + result.stderr)
        if args.mutate:
            for name, old, new, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError('nonunique anchor: ' + name)
                source.write_text(original.replace(old, new, 1))
                result = check(source, temp)
                if result.returncode != 1 or diagnostic not in result.stdout.splitlines() or 'REACHED host' not in result.stdout.splitlines():
                    raise RuntimeError('mutation %s: %s' % (name, result.stdout + result.stderr))
                print('RED', name, diagnostic)
    print('PASS ppc-lan-p15')

if __name__ == '__main__':
    main()
