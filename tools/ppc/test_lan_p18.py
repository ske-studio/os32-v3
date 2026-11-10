#!/usr/bin/env python3
"""Exercise GEM transmit and completion using the product code and a fake bus."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-lan-p18', tier=2, budget=60,
             covers=['drivers/net/gem/gem_tx.c', 'tools/ppc/test_lan_p18.py', 'tools/ppc/lan_p18_fake.c'],
             inputs=['drivers/net/gem/gem.h', 'drivers/net/gem/gem_ring.c', 'drivers/net/gem/gem_reset.c', 'include/types.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('sync', 'if (g->io.sync(buf, length, DMA_PREWRITE)) return GEM_EIO;', 'if (0) return GEM_EIO;', 'H:A4 dma-unpublished'),
    ('early-sync', 'for (u32 i = 0; i < bytes; i++) buf[i] = src[i];\n    for (u32 i = bytes; i < length; i++) buf[i] = 0;\n    /* gem_start:1759-1780: publish buffer then LE address and flags. */\n    if (g->io.sync(buf, length, DMA_PREWRITE)) return GEM_EIO;', 'if (g->io.sync(buf, length, DMA_PREWRITE)) return GEM_EIO;\n    for (u32 i = 0; i < bytes; i++) buf[i] = src[i];\n    for (u32 i = bytes; i < length; i++) buf[i] = 0;', 'H:sync-copy'),
    ('short-sync', 'g->io.sync(buf, length, DMA_PREWRITE)', 'g->io.sync(buf, 1, DMA_PREWRITE)', 'H:sync-range'),
    ('postwrite', 'g->io.sync(base + GEM_POOL_TX_BUF + g->tx_tail * GEM_BUF_SIZE,\n                       GEM_BUF_SIZE, DMA_POSTWRITE)', '(base != 0 && 0)', 'H:postwrite'),
    ('pad', 'buf[i] = 0;', 'buf[i] = 1;', 'H:sync-pad'),
    ('kick', 'GEM_TX_KICK, next);', 'GEM_TX_KICK, (next + 1) % GEM_RING_COUNT);', 'H:kick'),
    ('full', 'g->tx_pending >= 31', 'g->tx_pending >= 32', 'H:full'),
    ('overrun', 'if (count > g->tx_pending) return GEM_EIO;', 'if (0) return GEM_EIO;', 'H:overrun'),
    ('timeout', 'g->tx_pending && now - g->tx_started >= (unsigned long long)g->io.tb_hz * 5', '0', 'H:timeout'),
    ('limit', 'budget < 8 ? budget : 8', 'budget', 'H:limit'),
]
def check(source, temp):
    exe = temp / 'test'
    cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude', '-Idrivers/net/gem', str(source), 'drivers/net/gem/gem_ring.c', 'drivers/net/gem/gem_reset.c', 'tools/ppc/lan_p18_fake.c', '-o', str(exe)]
    build = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if build.returncode:
        raise RuntimeError('compile: ' + build.stderr)
    return subprocess.run(['qemu-i386', str(exe)], cwd=ROOT, text=True, capture_output=True, timeout=30)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'drivers/net/gem/gem_tx.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as directory:
        temp = Path(directory)
        source = temp / 'gem_tx.c'
        source.write_text(original)
        result = check(source, temp)
        if result.returncode or 'REACHED host' not in result.stdout.splitlines() or 'PASS host' not in result.stdout.splitlines():
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
    print('PASS ppc-lan-p18')

if __name__ == '__main__':
    main()
