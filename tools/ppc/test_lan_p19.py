#!/usr/bin/env python3
"""Exercise GEM receive using the product code and a fake bus."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-lan-p19', tier=2, budget=60,
             covers=['drivers/net/gem/gem_rx.c', 'tools/ppc/test_lan_p19.py', 'tools/ppc/lan_p19_fake.c'],
             inputs=['drivers/net/gem/gem.h', 'drivers/net/gem/gem_ring.c', 'include/types.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('owned', 'if (flags & GEM_RD_OWN)', 'if (!(flags & GEM_RD_OWN))', 'H:A7 rx-owned'),
    ('producer', '(slot + GEM_RX_PUBLISHED) % GEM_RING_COUNT', 'slot', 'H:golden'),
    ('offset', 'buf[i + 2]', 'buf[i + 0]', 'H:offset'),
    ('crc', '(flags & GEM_RD_BAD_CRC) || ', '', 'H:bad'),
    ('budget', 'seen < 8', 'seen < 9', 'H:A11 budget'),
    ('kick-count', 'g->rx_head % 4 == 0', '1', 'H:kick-count'),
    ('kick-value', '(g->rx_head + GEM_RX_PUBLISHED) & 31', 'g->rx_head', 'H:kick-value'),
    ('postread', 'if (g->io.sync(buf, GEM_BUF_SIZE, DMA_POSTREAD)) return GEM_EIO;', 'if (0) return GEM_EIO;', 'H:buffer-read'),
    ('kick-off-by-one', '(g->rx_head + GEM_RX_PUBLISHED) & 31', '(g->rx_head + GEM_RX_PUBLISHED + 1) & 31', 'H:kick-value'),
    ('desc-postread', 'if (g->io.sync(desc, GEM_DESC_SIZE, DMA_POSTREAD)) return GEM_EIO;', 'if (0) return GEM_EIO;', 'H:desc-read'),
    ('late-postread', 'if (g->io.sync(buf, GEM_BUF_SIZE, DMA_POSTREAD)) return GEM_EIO;', '/* delayed sync */', 'H:buffer-read'),
]
def instrument(source):
    source = source.replace('#include "gem.h"', '#include "gem.h"\nvoid observe_desc(const u8 *);\nvoid observe_buffer(const u8 *);')
    source = source.replace('flags = get64(desc + GEM_DESC_FLAGS_OFFSET);', 'observe_desc(desc); flags = get64(desc + GEM_DESC_FLAGS_OFFSET);')
    source = source.replace('((u8 *)frame)[i] = buf[i + 2];', '{ observe_buffer(buf); ((u8 *)frame)[i] = buf[i + 2]; }')
    source = source.replace('((u8 *)frame)[i] = buf[i + 0];', '{ observe_buffer(buf); ((u8 *)frame)[i] = buf[i + 0]; }')
    return source

def check(source, temp):
    exe = temp / 'test'
    cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude', '-Idrivers/net/gem', str(source), 'drivers/net/gem/gem_ring.c', 'tools/ppc/lan_p19_fake.c', '-o', str(exe)]
    build = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if build.returncode:
        raise RuntimeError('compile: ' + build.stderr)
    return subprocess.run(['qemu-i386', str(exe)], cwd=ROOT, text=True, capture_output=True, timeout=30)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'drivers/net/gem/gem_rx.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as directory:
        temp = Path(directory)
        source = temp / 'gem_rx.c'
        source.write_text(instrument(original))
        result = check(source, temp)
        if result.returncode or 'REACHED host' not in result.stdout.splitlines() or 'PASS host' not in result.stdout.splitlines():
            raise RuntimeError('baseline: ' + result.stdout + result.stderr)
        if args.mutate:
            for name, old, new, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError('nonunique anchor: ' + name)
                mutated = original.replace(old, new, 1)
                if name == 'late-postread':
                    mutated = mutated.replace('*bytes = length;', 'if (g->io.sync(buf, GEM_BUF_SIZE, DMA_POSTREAD)) return GEM_EIO;\n            *bytes = length;')
                source.write_text(instrument(mutated))
                result = check(source, temp)
                if result.returncode != 1 or diagnostic not in result.stdout.splitlines() or 'REACHED host' not in result.stdout.splitlines():
                    raise RuntimeError('mutation %s: %s' % (name, result.stdout + result.stderr))
                print('RED', name, diagnostic)
    print('PASS ppc-lan-p19')

if __name__ == '__main__':
    main()
