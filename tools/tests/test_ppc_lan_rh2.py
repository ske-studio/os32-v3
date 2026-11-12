#!/usr/bin/env python3
"""RH2 product capture/clock/PCI ordering and standard unknown PHY, ILP32."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32

CHECK = dict(name='ppc-lan-rh2', tier=2, budget=60, needs=[],
    covers=['tools/tests/test_ppc_lan_rh2.py',
            'platform/powermac/gmac_tree.c', 'platform/powermac/pci_config.c',
            'platform/powermac/gmac_clock.c', 'drivers/net/gem/gem_phy.c',
            'platform/powermac/lan_boot.h',
            'platform/powermac/ofw.h', 'arch/ppc/*.h', 'drivers/net/gem/gem.h',
            'tools/ppc/lan_p3_fake.c',
            'tools/ppc/lan_p17_fake.c', 'lib/kstring*', 'include/*.h',
            'tools/tests/host32.py'])
FLAGS = ['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-nostdlib',
         '-static', '-fno-stack-protector', '-D__cdecl=',
         *['-I'+str(ROOT / p) for p in ('.', 'include', 'lib', 'arch/ppc',
                                      'platform/powermac', 'drivers/net/gem')]]


def main():
    tree = (ROOT / 'platform/powermac/gmac_tree.c').read_text()
    phy = (ROOT / 'drivers/net/gem/gem_phy.c').read_text()
    with tempfile.TemporaryDirectory(prefix='rh2-') as td:
        d = Path(td)
        def tree_case(text, cases, red=False):
            src = d / 'gmac_tree.c'; src.write_text(text)
            exe = d / 'tree'
            subprocess.run(FLAGS + ['-DRH2_TREE', '-DPM_PCI_FAKE_MMIO',
                '-DPM_GMAC_CLOCK_FAKE', str(src),
                *[str(ROOT / p) for p in ('platform/powermac/pci_config.c',
                  'platform/powermac/gmac_clock.c', 'lib/kstring_c.c',
                  'tools/ppc/lan_p3_fake.c')], '-o', str(exe)], check=True)
            for case in cases:
                r = host32.run([str(exe), str(case)], runner='qemu', capture_output=True, timeout=10)
                expected = b'reached=A1\nH:A1 value\n' if red else b'reached=A1\n'
                assert r.returncode == int(red) and r.stdout == expected, (case, r.returncode, r.stdout)
        def phy_case(text, case, diagnostic=None):
            src = d / 'gem_phy.c'; src.write_text(text)
            exe = d / 'phy'
            subprocess.run(FLAGS + ['-DCASE='+str(case), str(src),
                str(ROOT / 'tools/ppc/lan_p17_fake.c'), '-o', str(exe)], check=True)
            r = host32.run([str(exe)], runner='qemu', capture_output=True, timeout=10)
            expected = b'REACHED host\n' + ((diagnostic+'\n').encode() if diagnostic else b'')
            assert r.returncode == int(diagnostic is not None) and r.stdout == expected, (case, r.returncode, r.stdout)
        tree_case(tree, [27, 28, 29, 30, 31, 32])
        anchor = 'static const char clock_model[] = "";'
        assert tree.count(anchor) == 1
        approved = tree.replace(anchor, 'static const char clock_model[] = "fixture,QuickSilver";')
        tree_case(approved, range(26, 33))
        for case in range(23, 33):
            phy_case(phy, case)
        print('GREEN RH2: empty/unknown/QEMU/model/reg gates; native clock -> PCI -> Master clear; snapshot; unknown PHY 10/100/1000/latch/no writes', flush=True)
        if '--mutate' in sys.argv:
            old = 'v & ~GTCR_1000_ADV'
            assert phy.count(old) == 1
            phy_case(phy.replace(old, 'v'), 30, 'H:gigabit-advertise')
            print('RED gigabit advertisement: REACHED host H:gigabit-advertise exit=1', flush=True)
            old = 'if (!((n == (int)len || (n == (int)len + 1 && !prop[len])) &&\n          !kstrncmp((char *)prop, clock_model, len)))'
            assert approved.count(old) == 1
            tree_case(approved.replace(old, 'if (n < 0)'), [27], True)
            print('RED clock condition: reached=A1 H:A1 value exit=1', flush=True)
            old = 'return GEM_OK; /* RH_PHY_ACCEPT: unknown IDs remain readable. */'
            assert phy.count(old) == 1
            phy_case(phy.replace(old, 'return supported(a, b) ? GEM_OK : GEM_EUNSUPPORTED;'), 23, 'H:unknown-init')
            print('RED unknown reject: REACHED host H:unknown-init exit=1', flush=True)
            old = 'r = rd(g, g->phy_addr, BMSR, &v.bmsr); if (r) goto done; /* RH_PHY_SECOND */'
            assert phy.count(old) == 1
            phy_case(phy.replace(old, '/* second read removed */'), 24, 'H:standard-link')
            print('RED BMSR second: REACHED host H:standard-link exit=1', flush=True)


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_ppc_lan_rh2.py',
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
