#!/usr/bin/env python3
"""Source/include ownership, legacy order and feature closure from real make."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_profile import query
ROOT = Path(__file__).resolve().parents[2]
FEATURES = 'V86 X87 PC98_DEVICES GEFORCE TRIDENT CIRRUS PCM FM LAN KCG CD FEP SQLITE HWDEV KDEBUG'.split()


def check(root):
    expected = json.loads((ROOT / 'tools/tests/fixtures/build_profile_x86.json').read_text())
    actual = query(root)
    for name, value in expected.items():
        assert actual[name] == value, name
    c = query(root, ['KSTRING_C_SRC=lib/kstring_c.c'])['C_KERNEL']
    assert c.count('lib/kstring_c.c') == 1
    assert c.index('lib/kstring_c.c') == c.index('lib/divdi3.c') + 1
    ppc = query(root, ['ARCH=ppc', 'PLATFORM=powermac'], config_only=True)
    assert ppc['C_KERNEL'].count('lib/kstring_c.c') == 1
    assert not ppc['ASM_KERNEL']
    assert ppc['GAS_KERNEL'] == ['arch/ppc/entry.S', 'arch/ppc/ofw_note.S']
    for name in ppc['C_KERNEL'] + ppc['KERNEL_SRC_DIRS']:
        assert not any(x in name for x in ('arch/x86', 'platform/pc98', 'kernel/v86', 'drivers/video/geforce')), name
    assert 'drivers/base' in ppc['KERNEL_SRC_DIRS']
    off = query(root, ['FEATURE_' + name + '=0' for name in FEATURES], config_only=True)
    assert not any('kernel/v86/' in name for name in off['C_KERNEL'])
    assert not any('geforce/' in name for name in off['C_KERNEL'])
    for assignments in (['FEATURE_UNKNOWN=1'], ['ARCH=ppc','PLATFORM=powermac','FEATURE_V86=1'], ['FEATURE_KDEBUG=0']):
        try:
            query(root, assignments, config_only=True)
        except subprocess.CalledProcessError:
            pass
        else:
            raise AssertionError(assignments)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    # Copy only make inputs; compiler and generated products are unnecessary.
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        shutil.copytree(ROOT / 'build', root / 'build', ignore=shutil.ignore_patterns('out','sdk','__pycache__'))
        (root / 'Makefile').write_text('include build/config.mk\n-include build/kernel-objects.mk\ninclude build/kernel.mk\n')
        # No libgcc invocation is allowed by these query-only tests.
        check(root)
        if args.mutate:
            order = root / 'build/sources/order.mk'
            source = order.read_text()
            anchor = 'kdebug/gf5_test.c kdebug/gf_display_test.c'
            assert source.count(anchor) == 1
            order.write_text(source.replace(anchor, 'kdebug/gf_display_test.c kdebug/gf5_test.c'))
            try:
                check(root)
            except AssertionError:
                print('RED: reordered legacy sources rejected at runtime')
            else:
                raise SystemExit('mutation survived')
    print('PASS build selection')
