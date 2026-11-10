#!/usr/bin/env python3
"""Exercise the product PHY code against a fake GEM MIF and PHY."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-lan-p17', tier=2, budget=60,
             covers=['drivers/net/gem/gem_phy.c', 'tools/ppc/test_lan_p17.py',
                     'tools/ppc/lan_p17_fake.c'],
             inputs=['drivers/net/gem/gem.h', 'include/types.h'])
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('ta0', 'if (v & GEM_MIF_FRAME_TA0)', 'if (!(v & GEM_MIF_FRAME_TA0))', 2, 'H:A6 phy-invalid'),
    ('poll', 'GEM_MIF_CONFIG_BB_ENA | GEM_MIF_CONFIG_POLL_ENA', 'GEM_MIF_CONFIG_BB_ENA', 0, 'H:config'),
    ('half', 'ANAR_CSMA | ANAR_TX_FD);', 'ANAR_CSMA | ANAR_TX_FD | 0x80);', 0, 'H:advertise'),
    ('latch', 'r = rd(g, g->phy_addr, BMSR, &v); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }\n        status = v;', 'v = status;\n        status = v;', 7, 'H:link'),
    ('poll-limit', '|| ++g->phy_polls >= PHY_POLLS) { g->phy_step = PHY_START; return GEM_ETIME; }\n        return GEM_AGAIN;\n    case PHY_DONE', ') { g->phy_step = PHY_START; return GEM_ETIME; }\n        return GEM_AGAIN;\n    case PHY_DONE', 8, 'H:an-timeout'),
    ('reset-limit', '|| ++g->phy_polls >= PHY_POLLS) { g->phy_step = PHY_START; return GEM_ETIME; }\n            return GEM_AGAIN;', ') { g->phy_step = PHY_START; return GEM_ETIME; }\n            return GEM_AGAIN;', 11, 'H:reset-poll-limit'),
    ('ffff', 'a != 0xffff &&', '1 &&', 0, 'H:A6 phy-invalid'),
    ('unknown-write', 'if (!g || !supported(g->phy_id1, g->phy_id2))', 'if (!g)', 16, 'H:unknown-write'),
    ('current', 'status = v;', 'status |= v;', 17, 'H:current-status'),
    ('retry', 'if (!(v & status & ANAR_TX_FD)) { g->phy_step = PHY_START; return GEM_EIO; }', 'if (!(v & status & ANAR_TX_FD)) { return GEM_EIO; }', 6, 'H:retry'),
    ('startneg', 'BMCR_AUTOEN | BMCR_STARTNEG);', 'BMCR_AUTOEN);', 0, 'H:advertise'),
    ('gtcr', 'r = wr(g, GTCR, 0);', 'r = GEM_OK;', 0, 'H:advertise'),
    ('order', 'r = wr(g, ANAR, ANAR_CSMA | ANAR_TX_FD);', 'r = wr(g, BMCR, BMCR_AUTOEN | BMCR_STARTNEG);', 0, 'H:advertise'),
    ('done-read', 'r = rd(g, g->phy_addr, BMSR, &v); if (r != GEM_OK) return r;', 'return GEM_OK;', 18, 'H:done-link'),
    ('done-restart', 'if (!(v & BMSR_LINK)) { g->phy_step = PHY_START; return GEM_EIO; }', 'if (!(v & BMSR_LINK)) { return GEM_EIO; }', 19, 'H:done-restart'),
    ('local', 'if (!(v & status & ANAR_TX_FD))', 'if (!(v & ANAR_TX_FD))', 22, 'H:partner'),
    ('partner', 'if (!(v & status & ANAR_TX_FD))', 'if (0)', 6, 'H:partner'),
]

def check(source, temp, case):
    exe = temp / 'test'
    cmd = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-DCASE='+str(case), '-Wall', '-Wextra', '-Werror', '-Iinclude', '-Idrivers/net/gem', str(source), 'tools/ppc/lan_p17_fake.c', '-o', str(exe)]
    built = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if built.returncode:
        raise RuntimeError('compile: ' + built.stderr)
    return subprocess.run(['qemu-i386', str(exe)], cwd=ROOT, text=True, capture_output=True, timeout=30)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'drivers/net/gem/gem_phy.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as d:
        temp = Path(d)
        source = temp / 'gem_phy.c'
        source.write_text(original)
        for case in range(23):
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
    print('PASS ppc-lan-p17')

if __name__ == '__main__':
    main()
