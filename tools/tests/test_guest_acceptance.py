#!/usr/bin/env python3
"""Offline acceptance-index, PT0 and TVDM tests; --mutate checks four guards."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_guest_acceptance as ga
import tvdump_recv as tv
sys.path.insert(0, str(ROOT / 'tools/accept'))
import pt0_snapshot as pt
sys.path.insert(0, str(ROOT / 'tools/tests'))
import mutpar


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.cases = yaml.safe_load(ga.INDEX.read_text())

    def test_valid(self):
        self.assertEqual(ga.validate(self.cases), self.cases)
        self.assertIn('| id |', ga.render(self.cases))

    def test_e_rejects_legacy_bb(self):
        for name in ('e9-E', 'e10b-regression-E'):
            case = next(c for c in self.cases if c['id'] == name)
            self.assertEqual(case['expect']['completion'], 0)
            self.assertEqual(case['expect']['kill_delta'], 1)
            self.assertEqual(case['expect']['pf_error'], 7)
        marker = (ROOT / 'userland/tests/ring3_marker.h').read_text()
        row = next(l for l in marker.splitlines() if 'ring3_guard bb (E)' in l)
        self.assertIn('| 7', row)
        self.assertIn('kill +1', row)
        self.assertNotIn('SURV', row)

    def test_missing(self):
        del self.cases[0]['expect']
        with self.assertRaises(ValueError):
            ga.validate(self.cases)

    def test_duplicate(self):
        self.cases.append(copy.deepcopy(self.cases[0]))
        with self.assertRaises(ValueError):
            ga.validate(self.cases)

    def test_missing_contract(self):
        self.cases[0]['contract'] = 'absent-contract.md#section'
        with self.assertRaises(ValueError):
            ga.validate(self.cases)

    def test_mode(self):
        self.cases[0]['mode'] = 'unknown'
        with self.assertRaises(ValueError):
            ga.validate(self.cases)

    def test_batch_reference(self):
        case = next(c for c in self.cases if c['mode'] == 'batch')
        case['entry'] = 'db_test'
        with self.assertRaises(ValueError):
            ga.validate(self.cases)

    def test_tvdump(self):
        cells = bytes([ord('A'), 0xe1]) * 2000
        raw = b'> tvdump\nTVDM' + bytes([80, 25]) + cells + b'\nDone\x04'
        self.assertEqual(tv.parse_tvdump(raw), (80, 25, cells))
        with patch.object(tv.emu, 'post', return_value=raw) as post:
            self.assertEqual(tv.receive_tvdump(), raw)
            post.assert_called_once_with('/api/cmd', b'tvdump', timeout=20)

    def test_tvdump_short(self):
        for raw in (b'', b'TVDM', b'TVDM\x50', b'TVDM\x50\x19' + b'\0' * 3999):
            with self.subTest(length=len(raw)), self.assertRaises(ValueError):
                tv.parse_tvdump(raw)

    def test_tvdump_dimensions(self):
        for cols, rows in ((79, 25), (80, 24), (0, 0)):
            with self.subTest(cols=cols, rows=rows), self.assertRaises(ValueError):
                tv.parse_tvdump(b'TVDM' + bytes([cols, rows]) + b'\0' * 4000)

    def test_pt_compare_cli(self):
        base = {'pde0': 0x2007, 'ptes': [0x1003] * 256}
        other = copy.deepcopy(base)
        other['ptes'][42] ^= 0x60
        self.assertFalse(pt.compare(base, other))
        self.assertTrue(pt.compare(base, other, True))
        with tempfile.TemporaryDirectory() as td:
            paths = [Path(td) / name for name in ('base.json', 'other.json')]
            paths[0].write_text(json.dumps(base))
            paths[1].write_text(json.dumps(other))
            cmd = [sys.executable, '-B', str(ROOT / 'tools/accept/pt0_snapshot.py'), '--compare', *map(str, paths), '--ignore-ad']
            self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
            other['ptes'][42] ^= 4
            paths[1].write_text(json.dumps(other))
            self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 1)
        self.assertFalse(pt.compare(base, other, True))
        other = copy.deepcopy(base)
        other['pde0'] ^= 4
        self.assertFalse(pt.compare(base, other, True))
        with self.assertRaises(ValueError):
            pt.compare(base, {'pde0': 0, 'ptes': []}, True)

    def test_pt_snapshot_symbols(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'kernel.map'
            addresses = {name: 0x500000 + i * 4 for i, name in enumerate(pt.SYMBOLS)}
            path.write_text(''.join('  0x%08x %s\n' % (addr, name) for name, addr in addresses.items()))
            calls = []
            def read(addr, length):
                calls.append((addr, length))
                if length == 1024:
                    return (0x1003).to_bytes(4, 'little') * 256
                return (0x2007 if addr not in addresses.values() else 0).to_bytes(4, 'little')
            result = pt.snapshot(path, read)
            self.assertEqual(result['pde0'], 0x2007)
            self.assertIn((0x2000, 1024), calls)
            for addr in addresses.values():
                self.assertIn((addr, 4), calls)
            path.write_text('')
            with self.assertRaises(ValueError):
                pt.snapshot(path, read)


MUTATIONS = [
    ('tools/tests/guest_acceptance.yaml', '    tag: BB??\n    completion: 0', '    tag: BB??\n    completion: SURV'),

    ('tools/gen_guest_acceptance.py', 'or case["id"] in ids', 'or False'),
    ('tools/accept/pt0_snapshot.py', 'mask = ~AD_BITS if ignore_ad else ~0', 'mask = ~(AD_BITS | 4) if ignore_ad else ~0'),
    ('tools/tvdump_recv.py', 'if (cols, rows) != (TVDM_COLS, TVDM_ROWS):', 'if False:'),
]


def mutations():
    with tempfile.TemporaryDirectory() as td:
        control = mutpar.run_script_in_tree(ROOT, td, {},
                                           'tools/tests/test_guest_acceptance.py', ['--self'],
                                           real=('tools/gen_guest_acceptance.py',),
                                           capture_output=True, timeout=60)
    if control.returncode != 0:
        print('CONTROL failed')
        return 1
    print('CONTROL GREEN')
    failed = 0
    for target, old, new in MUTATIONS:
        source = (ROOT / target).read_text()
        if source.count(old) != 1:
            print('MUTATE missing marker:', target)
            failed += 1
            continue
        with tempfile.TemporaryDirectory() as td:
            result = mutpar.run_script_in_tree(ROOT, td, {target: source.replace(old, new, 1)},
                                              'tools/tests/test_guest_acceptance.py', ['--self'],
                                              real=('tools/gen_guest_acceptance.py',),
                                              capture_output=True, timeout=60)
        # Assertion failures prove semantic detection, rather than syntax/import errors.
        detected = result.returncode != 0 and b'FAIL:' in result.stderr and b'FAILED (failures=' in result.stderr
        print('MUTATE', target, 'RED' if detected else 'MISSED')
        failed += not detected
    return failed


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(AcceptanceTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    failures = 0 if result.wasSuccessful() else 1
    if '--mutate' in sys.argv:
        failures += mutations()
    sys.exit(bool(failures))
