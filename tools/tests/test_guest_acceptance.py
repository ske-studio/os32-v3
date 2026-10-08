#!/usr/bin/env python3
"""Offline acceptance-index, PT0 and TVDM tests; --mutate checks index, debugger guards and extent traversal."""
import copy
import contextlib
import io
from urllib.parse import parse_qs, urlsplit
import json
import os
import struct
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
import as_extents_at_teardown as ext
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


class ExtentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.layout = ext.make_layout(ROOT / 'build/out/kernel.elf',
                                     ROOT / 'build/out/kernel.map',
                                     ROOT / 'build/out/vmkernel.lz4')

    def setUp(self):
        self.o = self.layout['offsets']
        self.assertEqual((self.o['slot_size'], self.o['as_size']), (204, 1228))
        self.assertEqual(self.o['extent_count'] * self.o['extent_size'], 512)
        self.app_id = 3
        self.slot_ptr = self.layout['slots'] + self.app_id * self.o['slot_size']
        self.as_ptr = 0x600000
        self.slot = bytearray(self.o['slot_size'])
        self.space = bytearray(self.o['as_size'])
        def put(raw, key, value):
            struct.pack_into('<I', raw, self.o[key], value)
        put(self.slot, 'slot_state', 2)
        put(self.slot, 'slot_cpl3', 1)
        put(self.slot, 'slot_as', self.as_ptr)
        put(self.space, 'as_pd', 0x610000)
        put(self.space, 'as_poisoned', 1)
        self.table = bytearray(self.o['extent_count'] * self.o['extent_size'])
        for i, name in enumerate(('libc', 'exec', 'anon', 'anon', 'arena', 'large')):
            off = i * self.o['extent_size']
            for key, value in (('extent_base', 0x88000000 + i * 0x2000),
                               ('extent_end', 0x88001000 + i * 0x2000),
                               ('extent_kind', self.o['kind_' + name]),
                               ('extent_flags', 0)):
                struct.pack_into('<I', self.table, off + self.o[key], value)
        begin = self.o['as_appmem'] + self.o['table_e']
        self.space[begin:begin + len(self.table)] = self.table
        self.calls = []
        self.blocks = {self.layout['bp']: bytes.fromhex(self.layout['code_hex']),
                       self.slot_ptr: self.slot, self.as_ptr: self.space}
        self.regs = {'eip': self.layout['bp'], 'eax': self.slot_ptr}

    def read(self, address, length):
        self.calls.append((address, length))
        for base, data in self.blocks.items():
            if base <= address and address + length <= base + len(data):
                return bytes(data[address - base:address - base + length])
        raise ValueError('read outside fake AppSlot/AS objects')

    def capture(self, **changes):
        args = dict(layout=self.layout, deployment=self.layout,
                    deployed_image_hash=self.layout['image_sha256'], regs=self.regs,
                    read_mem=self.read, cmd='fixture --child', app_id=self.app_id)
        args.update(changes)
        return ext.snapshot(**args)

    def test_entry_eax_traversal(self):
        try:
            result = self.capture()
        except (ValueError, KeyError) as exc:
            self.fail('entry traversal rejected the valid full objects: ' + str(exc))
        self.assertEqual(result['cmd'], 'fixture --child')
        self.assertEqual(result['id'], 3)
        self.assertEqual(result['slot'], self.slot_ptr)
        self.assertEqual(result['addrspace'], self.as_ptr)
        self.assertEqual(result['poisoned'], 1)
        self.assertEqual(result['elf_sha256'], self.layout['elf_sha256'])
        self.assertEqual(result['total'], 6)
        self.assertEqual(result['free'], 26)
        self.assertEqual(result['arenas'], 3)
        self.assertEqual(result['kinds'], dict(LIBC_INITIAL=1, EXEC_INITIAL=1,
                                             ANON=2, EXEC_ARENA=1, EXEC_LARGE=1))
        self.assertEqual(self.calls, [(self.layout['bp'], len(self.blocks[self.layout['bp']])),
                                     (self.slot_ptr, 204), (self.as_ptr, 1228)])

    def test_parse_kind_counts(self):
        result = ext.parse_extents(bytes(self.table), self.o)
        self.assertEqual(result['total'], 6)
        self.assertEqual(result['kinds']['ANON'], 2)
        self.assertEqual(result['kinds']['EXEC_ARENA'], 1)
        self.assertEqual(result['kinds']['EXEC_LARGE'], 1)
        empty = ext.parse_extents(bytes(len(self.table)), self.o)
        self.assertEqual((empty['total'], empty['free'], empty['arenas']), (0, 32, 0))
        with self.assertRaises(ValueError):
            ext.parse_extents(bytes(self.table[:-1]), self.o)
        bad = bytearray(self.table)
        struct.pack_into('<I', bad, self.o['extent_kind'], 99)
        with self.assertRaises(ValueError):
            ext.parse_extents(bytes(bad), self.o)

    def test_deployment_and_entry_guards(self):
        for key in ('elf_sha256', 'map_sha256', 'image_sha256', 'bp', 'eax_role', 'offsets'):
            deployed = copy.deepcopy(self.layout)
            deployed[key] = None
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.capture(deployment=deployed)
        with self.assertRaises(ValueError):
            self.capture(deployed_image_hash='0' * 64)
        for regs in ({'eip': self.layout['bp'] + 0x24, 'eax': self.as_ptr},
                     {'eip': self.layout['bp'], 'eax': self.as_ptr}):
            with self.assertRaises(ValueError):
                self.capture(regs=regs)
        self.assertEqual(self.calls, [])
        self.blocks[self.layout['bp']] = bytes(len(self.blocks[self.layout['bp']]))
        with self.assertRaisesRegex(ValueError, 'running ELF code mismatch'):
            self.capture()
        self.assertEqual(len(self.calls), 1)

    def test_short_and_inactive_reads(self):
        with self.assertRaisesRegex(ValueError, 'short memory read'):
            self.capture(read_mem=lambda address, length: b'')
        struct.pack_into('<I', self.slot, self.o['slot_state'], self.o['state_free'])
        with self.assertRaisesRegex(ValueError, 'not a live USER'):
            self.capture()
        struct.pack_into('<I', self.slot, self.o['slot_state'], 2)
        struct.pack_into('<I', self.slot, self.o['slot_as'], 0)
        with self.assertRaisesRegex(ValueError, 'no AS'):
            self.capture()

    def test_layout_register_proof(self):
        cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-objdump'
        raw = subprocess.check_output([str(cross), '-d', '--disassemble=exec_teardown_app',
                                       str(ROOT / 'build/out/kernel.elf')], text=True)
        self.assertEqual(ext.entry_code(raw, self.layout['bp'], self.o), self.layout['code_hex'])
        with self.assertRaisesRegex(ValueError, 'entry EAX convention'):
            ext.entry_code(raw.replace('%eax,%eax', '%edx,%edx', 1), self.layout['bp'], self.o)
        wrong = dict(self.o, slot_as=self.o['slot_as'] + 4)
        with self.assertRaisesRegex(ValueError, 'AppSlot.as'):
            ext.entry_code(raw, self.layout['bp'], wrong)
        with self.assertRaisesRegex(ValueError, 'nm entry'):
            ext.entry_code(raw, self.layout['bp'] + 0x24, self.o)

    def test_capture_cli(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            deployment, image, out = [work / x for x in ('deploy.json', 'readback.lz4', 'sample.json')]
            deployment.write_text(json.dumps(self.layout))
            image.write_bytes((ROOT / 'build/out/vmkernel.lz4').read_bytes())
            args = ['ext', 'capture', str(out), '--deployment', str(deployment),
                    '--deployed-image', str(image), '--cmd', 'fixture --child', '--id', '3', '--live']
            def get(path, timeout):
                self.assertEqual(timeout, 20)
                if path == '/api/instance':
                    return json.dumps(dict(ok=True, trap_pause=True, user_pause=False)).encode()
                if path == '/api/regs':
                    return json.dumps({key: hex(value) for key, value in self.regs.items()}).encode()
                query = parse_qs(urlsplit(path).query)
                self.assertEqual(query['space'], ['phys'])
                raw = self.read(int(query['addr'][0], 16), int(query['len'][0]))
                return json.dumps(dict(ok=True, hex=raw.hex())).encode()
            with patch.object(sys, 'argv', args), patch.object(ext, 'make_layout', return_value=self.layout), \
                    patch.object(ext.emu, 'get', side_effect=get), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(ext.main(), 0)
            self.assertEqual(json.loads(out.read_text())['kinds']['ANON'], 2)
            # Hash mismatch must refuse before even requesting debugger state.
            image.write_bytes(b'wrong deployed image')
            with patch.object(sys, 'argv', args), patch.object(ext, 'make_layout', return_value=self.layout), \
                    patch.object(ext.emu, 'get') as get_mock, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(ext.main(), 1)
                get_mock.assert_not_called()
            image.write_bytes((ROOT / 'build/out/vmkernel.lz4').read_bytes())
            with patch.object(sys, 'argv', args), patch.object(ext, 'make_layout', return_value=self.layout), \
                    patch.object(ext.emu, 'get', return_value=b'{"ok":true,"trap_pause":false,"user_pause":false}'), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(ext.main(), 1)

    def test_prepare_cli(self):
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / 'deploy.json'
            result = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/accept/as_extents_at_teardown.py'),
                                     'prepare', str(out)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(out.read_text()), self.layout)
            # Existing evidence is never overwritten.
            self.assertEqual(subprocess.run([sys.executable, '-B', str(ROOT / 'tools/accept/as_extents_at_teardown.py'),
                                            'prepare', str(out)], capture_output=True).returncode, 1)


MUTATIONS = [
    ('tools/tests/guest_acceptance.yaml', '    tag: BB??\n    completion: 0', '    tag: BB??\n    completion: SURV'),

    ('tools/gen_guest_acceptance.py', 'or case["id"] in ids', 'or False'),
    ('tools/accept/pt0_snapshot.py', 'mask = ~AD_BITS if ignore_ad else ~0', 'mask = ~(AD_BITS | 4) if ignore_ad else ~0'),
    ('tools/accept/as_extents_at_teardown.py', "as_ptr = word(slot, o['slot_as'])",
     "as_ptr = word(slot, o['slot_as'] + WORD_BYTES)"),
    ('tools/accept/as_extents_at_teardown.py', "table_start = o['as_appmem'] + o['table_e']",
     "table_start = o['as_appmem'] + WORD_BYTES + o['table_e']"),
    ('tools/accept/as_extents_at_teardown.py', "as_ptr = word(slot, o['slot_as'])", "as_ptr = slot_ptr"),
    ('tools/accept/as_extents_at_teardown.py', "offsets['kind_anon']: 'ANON', offsets['kind_arena']: 'EXEC_ARENA'",
     "offsets['kind_anon']: 'EXEC_ARENA', offsets['kind_arena']: 'ANON'"),
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
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(cls)
                               for cls in (AcceptanceTests, ExtentTests))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    failures = 0 if result.wasSuccessful() else 1
    if '--mutate' in sys.argv:
        failures += mutations()
    sys.exit(bool(failures))
