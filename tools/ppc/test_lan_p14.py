#!/usr/bin/env python3
"""Test GEM reset and monitoring against a stateful ILP32 bus."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-lan-p14', tier=2, budget=60,
             covers=['drivers/net/gem/gem_reset.c', 'tools/ppc/test_lan_p14.py',
                     'tools/ppc/lan_p14_fake.c'],
             inputs=['drivers/net/gem/gem.h', 'include/types.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('rx-stop', 'r = wait_clear(g, GEM_MAC_RX_CONFIG, GEM_MAC_RX_ENABLE, now);', 'r = GEM_OK;', 0, 'H:A5 reset-active'),
    ('delay', 'if (now - g->started < ticks(g->io.tb_hz, 5000U)) {\n            if (++g->polls >= GEM_WAIT_POLLS) { g->state = GEM_STATE_QUARANTINED; return GEM_ETIME; }\n            return GEM_AGAIN;\n        }\n        g->io.write32(g->io.ctx, GEM_RESET_REG, GEM_RESET_RX);', 'g->io.write32(g->io.ctx, GEM_RESET_REG, GEM_RESET_RX);', 0, 'H:delay'),
    ('timeout', 'now - g->started >= ticks(g->io.tb_hz, 1000000U)', '(now - g->started >= ticks(g->io.tb_hz, 1000000U) && 0)', 1, 'H:timeout'),
    ('quarantine', 'if (g->state == GEM_STATE_QUARANTINED) return GEM_ETIME;\n    switch', 'if (g->state == GEM_STATE_QUARANTINED) { g->io.write32(g->io.ctx, GEM_INTMASK, 0); return GEM_ETIME; }\n    switch', 1, 'H:quarantine'),
    ('immediate', 'g->rx_watch_started = now; g->rx_watching = 1;', 'g->rx_watch_started = now; g->rx_watching = 1; g->state = GEM_STATE_RESETTING;', 3, 'H:watch-immediate'),
    ('extend', 'if ((g->rx_status & GEM_MAC_RX_OVERFLOW) && !g->rx_watching)', 'if (g->rx_status & GEM_MAC_RX_OVERFLOW)', 2, 'H:watch-deadline'),
]

def check(source, temp, case):
    exe = temp / 'test'
    cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror', '-DCASE='+str(case), '-I.', '-Iinclude', '-Idrivers/net/gem', str(source), 'tools/ppc/lan_p14_fake.c', '-o', str(exe)]
    build = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if build.returncode:
        raise RuntimeError('compile: ' + build.stderr)
    return subprocess.run(['qemu-i386', str(exe)], cwd=ROOT, text=True, capture_output=True, timeout=30)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'drivers/net/gem/gem_reset.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as directory:
        temp = Path(directory)
        source = temp / 'gem_reset.c'
        source.write_text(original)
        for case in range(6):
            result = check(source, temp, case)
            if result.returncode or 'REACHED host' not in result.stdout.splitlines():
                raise RuntimeError('baseline %d: %s' % (case, result.stdout + result.stderr))
        if args.mutate:
            for name, old, new, case, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError('nonunique anchor: ' + name)
                source.write_text(original.replace(old, new, 1))
                result = check(source, temp, case)
                if result.returncode != 1 or diagnostic not in result.stdout.splitlines() or 'REACHED host' not in result.stdout.splitlines():
                    raise RuntimeError('mutation %s: %s' % (name, result.stdout + result.stderr))
                print('RED', name, diagnostic)
    print('PASS ppc-lan-p14')

if __name__ == '__main__':
    main()
