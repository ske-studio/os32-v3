"""T2h/h3 real SHM state machine (ILP32) + PM playbook with fake emulator.

Only successful compilation followed by runtime failure counts as mutation RED.
"""
import argparse
from contextlib import contextmanager
import importlib.util
import json
import hashlib
import time
from unittest.mock import patch
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

from host32 import run
ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SRC = ['tools/h3_park_resume.py', 'tools/h3_layout.c',
              'userland/tests/h3/protocol.h', 'userland/tests/h3/state.inc',
              'userland/tests/h3/fixture.inc', 'userland/tests/h3a.c', 'userland/tests/h3b.c']


def module(path):
    spec = importlib.util.spec_from_file_location('h3_playbook', path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    m.ROOT = ROOT  # Mutant copy changes code, never the build/input tree.
    return m


h3 = module(ROOT / 'tools/h3_park_resume.py')


def http_success(path):
    endpoint = path.split('?')[0]
    reply = {'ok': True}
    if endpoint == '/api/break/del':
        reply = dict(ok=1, removed=1)
    elif endpoint == '/api/break/add':
        reply.update(slot=0, eip=path.split('=')[1])
    elif endpoint == '/api/step':
        reply.update(trap_pause=1, cs='0x0008', eip='0x00000069')
    return json.dumps(reply)


class FakeEmulator:
    def __init__(self):
        self.mem = bytearray(0x40000)
        self.writes = []
        self.depth = 0

    @contextmanager
    def freeze(self):
        self.depth += 1
        try:
            yield
        finally:
            self.depth -= 1

    def read(self, address, size):
        return bytes(self.mem[address:address + size])

    def write(self, address, data):
        assert self.depth, 'writes require a frozen snapshot'
        self.writes.append((address, data))
        self.mem[address:address + len(data)] = data

    def word(self, address, value):
        self.mem[address:address + 4] = struct.pack('<I', value)


class Tests(unittest.TestCase):
    def _callTestMethod(self, method):
        try:
            method()
        except RuntimeError as exc:
            # A mutated playbook rejecting a valid fake snapshot is runtime RED.
            # Compiler/process/import errors remain unittest errors, never RED.
            self.fail(str(exc))

    def setUp(self):
        # Every fake-emulator case owns a private lock namespace. Preserve the
        # real flock/competing-CLI test without touching a live instance's lock
        # or another worktree's host test. Mutant subprocesses isolate likewise.
        lock_dir = tempfile.TemporaryDirectory(prefix='h3-fake-lock-')
        self.addCleanup(lock_dir.cleanup)
        lock_env = patch.dict('os.environ', {'TMPDIR': lock_dir.name})
        lock_env.start()
        self.addCleanup(lock_env.stop)
        # Fake clients must not wait on real wall time, including mutants that
        # suppress start or substitute passive deadlines for guest time.
        wall_clock = iter(i / 100 for i in range(1000000))
        wall_patch = patch.object(h3.time, 'monotonic', side_effect=lambda: next(wall_clock))
        wall_patch.start()
        self.addCleanup(wall_patch.stop)
        self.emu = e = FakeEmulator()
        self.o = o = dict(slot_size=192, slot_state=0, slot_as=180, slot_wait=28,
                         slot_in_wait=20, slot_abort=24, slot_tick=44, running=1, as_owner=272, as_generation=0, ledger_size=24,
                         ledger_kind=0, ledger_id=1, ledger_pages=12, ledger_as=2,
                         ledger_count=64, block_size=16384, block_count=2,
                         shm_delta=4096, app_min=2, app_max=5, parked=2, free=0)
        self.s = s = dict(g_slot=0x1000, shm_state=0x2000, shm_block_owner=0x2100,
                         shm_block_span=0x2200, ledger_owners=0x3000, __bss_end=0x10001)
        s.update(g_cur=0x4100, tick_count=0x4104)
        s.update({name: 0x4000 + i * 4 for i, name in enumerate(h3.COUNTERS)})
        e.mem[0x2000] = 1
        e.word(0x2100, 2); e.word(0x2200, 1)
        self.slot = 0x1000 + 2 * 192
        e.word(self.slot, 2); e.word(self.slot + 28, 1)
        e.word(self.slot + 180, 0x5000)
        e.word(0x5000, 91); e.word(0x5000 + 272, 17)
        e.mem[0x3000 + 17 * 24] = 2
        e.mem[0x3000 + 17 * 24 + 1] = 2
        e.word(0x3000 + 17 * 24 + 12, 10)
        self.address = 0x12000
        values = [h3.MAGIC, 17, 91, 3, 0, 0, 1, 1, 8, 0, 42, 0]
        e.mem[self.address:self.address + 48] = struct.pack('<12I', *values)
        self.p = h3.Playbook(e, {'symbols': s, 'offsets': o, 'map_sha256': 'map', 'elf_sha256': 'elf',
                              'trace_sites': dict(launch_pending=101, resume_pending=102, consumed=103, wm_kill=104, syscall_abort=105)}, sleep=lambda _: None)
        self.ident = dict(index=0, address=self.address, app=2, owner=17, generation=91, slot=self.slot)

        e.mem[0x2001] = 1; e.word(0x2104, 3); e.word(0x2204, 1)
        other = s['g_slot'] + 3 * o['slot_size']
        e.word(other, 2); e.word(other + 28, 1); e.word(other + 180, 0x6000)
        e.word(0x6000, 93); e.word(0x6000 + 272, 18)
        ledger = s['ledger_owners'] + 18 * o['ledger_size']
        e.mem[ledger] = 2; e.mem[ledger + 1] = 3; e.word(ledger + 12, 10)
        e.mem[0x16000:0x16030] = struct.pack('<12I', h3.MAGIC, 18, 93, 3, 0, 0, 1, 2, 0, 0, 43, 1)

    def change(self, field, value):
        self.emu.word(self.address + h3.FIELDS.index(field) * 4, value)

    def discover_ok(self, fixture):
        try:
            return self.p.discover(fixture)
        except RuntimeError as exc:
            self.fail(str(exc))

    def test_address_and_owner(self):
        self.assertEqual(self.discover_ok(1), self.ident)
        self.assertEqual(self.ident['address'], 0x12000)
        self.assertEqual((self.ident['app'], self.ident['owner'], self.ident['generation']), (2, 17, 91))

    def test_two_independent_blocks(self):
        e, o, s = self.emu, self.o, self.s
        e.mem[0x2001] = 1; e.word(0x2104, 3); e.word(0x2204, 1)
        slot = s['g_slot'] + 3 * o['slot_size']
        e.word(slot, 2); e.word(slot + 28, 1); e.word(slot + 180, 0x6000)
        e.word(0x6000, 93); e.word(0x6000 + 272, 18)
        ledger = s['ledger_owners'] + 18 * o['ledger_size']
        e.mem[ledger] = 2; e.mem[ledger + 1] = 3; e.word(ledger + 12, 10)
        address = self.address + 16384
        e.mem[address:address + 48] = struct.pack('<12I', h3.MAGIC, 18, 93, 3, 0, 0, 1, 2, 0, 0, 43, 1)
        other = self.discover_ok(2)
        self.assertEqual(other['address'], address)
        self.assertEqual(other['owner'], 18)
        self.p.arm(other, 4)
        self.assertEqual([a for a, _ in e.writes], [address + 16, address + 20])
        self.assertEqual(self.p.block(self.address)['arm'], 0)

    def test_init_publication(self):
        self.assertEqual(self.p.initialize(1), self.ident)
        self.assertEqual(self.emu.writes, [])
        for field in ('owner', 'generation'):
            saved = self.p.block(self.address)[field]
            self.change(field, saved + 1)
            with self.assertRaises(RuntimeError): self.p.initialize(1)
            self.change(field, saved)

    def test_init_waits_for_fixture_publication(self):
        self.change('owner', 0); self.change('generation', 0); self.change('phase', 1)
        def publish(_):
            self.assertEqual(self.emu.depth, 0, 'publication wait must thaw')
            self.change('owner', self.ident['owner'])
            self.change('generation', self.ident['generation'])
            self.change('phase', 3)
        self.p.sleep = publish
        self.assertEqual(self.p.initialize(1), self.ident)
        self.assertEqual(self.emu.writes, [])

    def test_modes_and_write_whitelist(self):
        for mode in h3.MODES.values():
            self.change('arm', 0)
            case = self.p.arm(self.ident, mode)
            self.assertEqual(case['mode'], mode)
        self.assertEqual([a - self.address for a, _ in self.emu.writes], [16, 20] * 6)
        with self.assertRaises(RuntimeError): self.p.arm(self.ident, 7)

    def test_stale_identity_no_write(self):
        for addr, value in [(0x5000, 92), (0x5000 + 272, 18), (0x2100, 3),
                            (self.address + 4, 18), (self.address + 8, 90)]:
            old = self.p.word(addr); self.emu.word(addr, value)
            with self.assertRaises(RuntimeError): self.p.arm(self.ident, 1)
            self.assertEqual(self.emu.writes, [])
            self.emu.word(addr, old)

    def test_park_preconditions(self):
        for addr, value in [(self.slot, 1), (self.slot + 28, 0),
                            (self.address + 12, 4), (self.address + 20, 1), (self.address + 36, 1)]:
            old = self.p.word(addr); self.emu.word(addr, value)
            with self.assertRaises(RuntimeError): self.p.arm(self.ident, 1)
            self.assertEqual(self.emu.writes, [])
            self.emu.word(addr, old)

    def test_arm_running_op_wait(self):
        self.emu.word(self.slot, self.o['running'])
        self.emu.word(self.slot + self.o['slot_wait'], 0)
        self.emu.word(self.slot + self.o['slot_in_wait'], 1)
        self.emu.word(self.s['g_cur'], self.ident['app'])
        case = self.p.arm(self.ident, 5)
        self.assertEqual(case['identity'], self.ident)
        self.assertTrue(case['armed_in_running_wait'])
        self.change('phase', 5); self.change('arm', 0)
        self.change('consumed', 1); self.change('resumes', 9)
        self.assertTrue(self.p.resumed(case))
        self.emu.word(self.s['ring3_switch_count'], 2)
        case['before']['ring3_switch_count'] = 3
        with self.assertRaises(RuntimeError):
            self.p.resumed(case)
        self.assertEqual([a for a, _ in self.emu.writes],
                         [self.address + 16, self.address + 20])

    def test_arm_running_wait_rejects_wrong_current_or_mark(self):
        self.emu.word(self.slot, self.o['running'])
        self.emu.word(self.slot + self.o['slot_wait'], 1)  # stale park mark
        for current, in_wait in ((3, 1), (2, 0), (2, 2)):
            with self.subTest(current=current, in_wait=in_wait):
                self.emu.word(self.s['g_cur'], current)
                self.emu.word(self.slot + self.o['slot_in_wait'], in_wait)
                with self.assertRaises(RuntimeError):
                    self.p.arm(self.ident, 5)
                self.assertEqual(self.emu.writes, [])
        self.emu.word(self.slot, 3)  # WAIT_KEY is not OP_WAIT
        self.emu.word(self.slot + self.o['slot_wait'], 0)
        self.emu.word(self.slot + self.o['slot_in_wait'], 1)
        self.emu.word(self.s['g_cur'], 2)
        with self.assertRaises(RuntimeError):
            self.p.arm(self.ident, 5)
        self.assertEqual(self.emu.writes, [])

    def test_corrupt_diagnostics(self):
        for addr, value in [(0x2200, 2), (self.slot + 180, 0), (0x5000 + 272, 0),
                            (0x3000 + 17 * 24 + 12, 0), (self.slot, 0)]:
            old = self.p.word(addr); self.emu.word(addr, value)
            with self.assertRaises(RuntimeError): self.p.discover(1)
            self.emu.word(addr, old)
        for offset in (0, 1):
            addr = 0x3000 + 17 * 24 + offset
            old = self.emu.mem[addr]; self.emu.mem[addr] = 0
            with self.assertRaises(RuntimeError): self.p.discover(1)
            self.emu.mem[addr] = old

    def test_resume_evidence(self):
        case = self.p.arm(self.ident, 1)
        self.assertFalse(self.p.resumed(case))
        self.change('phase', 5); self.change('arm', 0); self.change('consumed', 1)
        self.change('resumes', 9)
        self.emu.word(self.s['ring3_switch_count'], 1)
        self.assertTrue(self.p.resumed(case))
        for name, value in [('resumes', 8), ('consumed', 2), ('arm', 1), ('mode', 2)]:
            old = self.p.block(self.address)[name]; self.change(name, value)
            with self.assertRaises(RuntimeError): self.p.resumed(case)
            self.change(name, old)
        self.emu.word(self.s['ring3_switch_count'], 0)
        with self.assertRaises(RuntimeError): self.p.resumed(case)

    def recovered(self, case):
        e, s = self.emu, self.s
        e.word(self.slot, 0); e.word(0x2100, 0); e.mem[0x2000] = 0
        e.word(0x3000 + 17 * 24 + 12, 0)
        e.mem[0x3000 + 17 * 24] = 0
        e.word(s['appslot_reclaim_count'], 1); e.word(s['appslot_last_reclaim_id'], 2)
        kills, aborts, _ = h3.ROUTES[case['mode']]
        e.word(s['fault_kill_count'], kills); e.word(s['ring3_abort_count'], aborts)

    def test_reclaim_and_depths(self):
        case = self.p.arm(self.ident, 1)
        self.recovered(case)
        self.p.reclaimed(case)
        for name in ('kctx_irq_depth', 'kctx_exc_depth', 'ring3_wm_depth', 'ring3_in_syscall',
                     'ledger_irq_ops', 'ledger_exc_ops', 'g_pending_id'):
            self.emu.word(self.s[name], 1)
            with self.assertRaises(RuntimeError): self.p.reclaimed(case)
            self.emu.word(self.s[name], 0)
        for addr, value in [(self.slot, 2), (0x2100, 2), (0x3000 + 17 * 24 + 12, 1),
                            (self.s['appslot_reclaim_count'], 2), (self.s['fault_kill_count'], 0),
                            (self.s['appslot_last_reclaim_id'], 3), (self.s['ring3_abort_count'], 1)]:
            old = self.p.word(addr); self.emu.word(addr, value)
            with self.assertRaises(RuntimeError): self.p.reclaimed(case)
            self.emu.word(addr, old)

    def test_trace_both_landings(self):
        case = self.p.arm(self.ident, 5)
        trace = self.trace(case)
        self.p.verify_trace(case, trace)
        for name, value in [('landing', 'launch'), ('launch_pending', 1), ('resume_pending', 0),
                            ('pending_consumed', 2), ('foreground_app', 3), ('foreground_window', 43),
                            ('case_id', 'old'), ('map_sha256', 'old'), ('vector', 13), ('fired', 1),
                            ('resume_observed', False)]:
            bad = dict(trace); bad[name] = value
            with self.assertRaises(RuntimeError): self.p.verify_trace(case, bad)

    def test_next_launch_generation(self):
        case = self.p.arm(self.ident, 1)
        with self.assertRaises(RuntimeError): self.p.next_launch(case)
        self.emu.word(0x5000, 92)
        self.change('generation', 92)
        self.p.next_launch(case)

    def test_timeout_and_nm(self):
        with self.assertRaises(RuntimeError): self.p.wait(lambda: False, 'test', attempts=2)
        self.assertEqual(h3.symbols('00123456 b g_slot\n00000018 A h3_size'),
                         {'g_slot': 0x123456, 'h3_size': 24})
        with self.assertRaises(RuntimeError): h3.symbols('00000001 b x\n00000002 b x')
        self.assertEqual(h3.symbols('00000001 b x\n00000002 b x\n00000003 B g_slot',
                                    ('g_slot',)), {'g_slot': 3})

    def test_http_client_contract(self):
        calls = []
        class Client:
            def get(self, path):
                calls.append(('get', path))
                if path == '/api/instance':
                    return json.dumps(dict(ok=True, trap_pause=False, user_pause=False))
                return json.dumps(dict(ok=True, addr='0x1234', len=4, space='phys', hex='01000000'))
            def post(self, path, body=None):
                calls.append(('post', path, body))
                return http_success(path)
        emu = h3.Emulator.__new__(h3.Emulator)
        emu.client = Client(); emu.freeze_depth = 0
        with emu.freeze():
            with emu.freeze():
                self.assertEqual(emu.read(0x1234, 4), b'\x01\x00\x00\x00')
                emu.write(0x1234, b'\x01\x00\x00\x00')
        self.assertEqual(calls.count(('post', '/api/pause', None)), 1)
        self.assertEqual(calls.count(('post', '/api/resume', None)), 1)
        self.assertIn(('post', '/api/mem?addr=0x1234&space=phys', '01000000'), calls)
        self.assertIn(('get', '/api/mem?addr=0x1234&len=4&space=phys'), calls)
        with patch.object(h3.time, 'sleep'):
            emu.click(90, 68, 400)
        emu.stop()
        self.assertEqual(calls[-1], ('post', '/api/key', 'seq=CTRL%2BSTOP&hold=300'))
        with patch.object(emu.client, 'get', return_value='{"ok":true,"trap_pause":true,"user_pause":false}'):
            count = len(calls)
            with self.assertRaises(RuntimeError):
                with emu.freeze(): pass
            self.assertEqual(len(calls), count)


    def trace(self, case):
        pending = h3.ROUTES[case['mode']][2]
        samples = ([dict(site='resume_pending', pending=2), dict(site='consumed', pending=0)]
                   if pending else [dict(site='wm_kill', pending=0)])
        for row in samples:
            row.update(eip=self.p.layout['trace_sites'][row['site']], eax=99, argument=2, current=2, irq=0, exc=0, observed_at=case['armed_at']+1)
        capture = dict(case_id=case['case_id'], elf_sha256='elf', sites=self.p.layout['trace_sites'],
                       samples=samples, started_at=case['armed_at'], ended_at=case['armed_at']+2)
        self.record(case, capture)
        if case['mode'] == 6:
            case['reclaimed'] = dict(case['before'])
        return dict(map_sha256='map', identity=self.ident, mode=case['mode'], case_id=case['case_id'],
                    landing='resume' if pending else 'wm-kill', launch_pending=0,
                    resume_pending=pending, pending_consumed=pending,
                    fired=case['mode'], resume_observed=True, foreground_window=42, foreground_app=2,
                    vector=h3.VECTORS[case['mode']], capture=capture)

    def record(self, case, capture):
        case['capture_record'] = dict(sha256=hashlib.sha256(h3.json_bytes(capture)).hexdigest(),
            started_at=capture['started_at'], ended_at=capture['ended_at'])

    def test_survivor_wait_and_irq_retry(self):
        case = self.p.arm(self.ident, 1)
        self.recovered(case)
        survivor = case['survivor']
        self.emu.word(self.s['g_cur'], survivor['app'])
        self.emu.word(survivor['slot'], self.o['running'])
        self.emu.word(survivor['slot'] + self.o['slot_in_wait'], 1)
        for name in ('ring3_in_syscall', 'ring3_wm_depth', 'kctx_irq_depth'):
            self.emu.word(self.s[name], 1)
        sleeps = []
        def irq_exit(_):
            self.assertEqual(self.emu.depth, 0)
            sleeps.append(1)
            self.emu.word(self.s['kctx_irq_depth'], 0)
        self.p.sleep = irq_exit
        try:
            result = self.p.reclaimed(case)
        except RuntimeError as exc:
            self.fail(str(exc))
        self.assertEqual(result['ring3_wm_depth'], 1)
        self.assertEqual(len(sleeps), 1)
        for addr, value in [(self.s['g_cur'], 2), (survivor['slot'], 2),
                            (survivor['slot'] + self.o['slot_in_wait'], 0),
                            (self.s['ring3_wm_depth'], 0), (self.s['ring3_in_syscall'], 2)]:
            old = self.p.word(addr); self.emu.word(addr, value)
            with self.assertRaises(RuntimeError): self.p.reclaimed(case)
            self.emu.word(addr, old)

    def test_unrelated_shm_skipped(self):
        # Move the second fixture behind unrelated live SHM, including span=0 tail.
        self.o['block_count'] = 4
        self.emu.mem[0x30000:0x30030] = self.emu.mem[0x16000:0x16030]
        # Actual index 3 begins at 0x1e000.
        self.emu.mem[0x1e000:0x1e030] = self.emu.mem[0x16000:0x16030]
        self.emu.mem[0x2003] = 1
        self.emu.word(0x210c, 3); self.emu.word(0x220c, 1)
        for owner, span in ((1, 1), (3, 0), (3, 2)):
            self.emu.word(0x2104, owner); self.emu.word(0x2204, span)
            self.assertEqual(self.discover_ok(2)['index'], 3)
        self.emu.word(0x2104, 3); self.emu.word(0x2204, 1)
        with self.assertRaises(RuntimeError): self.p.discover(2)

    def test_mode_routes_and_vectors(self):
        for mode, expected in {1:(1,0,1,14), 2:(1,0,1,13), 3:(1,0,1,0),
                               4:(1,0,1,6), 5:(1,1,1,None), 6:(0,0,0,None)}.items():
            self.setUp()
            case = self.p.arm(self.ident, mode)
            self.assertEqual((*h3.ROUTES[mode], h3.VECTORS[mode]), expected)
            self.recovered(case)
            self.p.reclaimed(case)
            for name in ('fault_kill_count', 'ring3_abort_count'):
                old = self.p.word(self.s[name]); self.emu.word(self.s[name], old + 1)
                with self.assertRaises(RuntimeError): self.p.reclaimed(case)
                self.emu.word(self.s[name], old)
            trace = self.trace(case)
            self.p.verify_trace(case, trace)
            for name, value in [('vector', 99), ('pending_consumed', 9), ('resume_pending', 9),
                                ('launch_pending', 1), ('landing', 'launch')]:
                bad = dict(trace); bad[name] = value
                with self.assertRaises(RuntimeError): self.p.verify_trace(case, bad)
            bad = self.trace(case); bad['capture']['samples'] = []
            with self.assertRaises(RuntimeError): self.p.verify_trace(case, bad)

    def test_protocol_header(self):
        import re
        source = (ROOT / 'userland/tests/h3/protocol.h').read_text()
        constants = {name: int(value, 0) for name, value in re.findall(
            r'#define H3_(\w+) (0x[0-9A-Fa-f]+|\d+)u', source)}
        self.assertEqual(h3.MAGIC, constants['MAGIC'])
        self.assertEqual(h3.VERSION, constants['VERSION'])
        for name, value in h3.PHASES.items(): self.assertEqual(value, constants[name])
        for name, value in h3.MODES.items():
            self.assertEqual(value, constants[name.upper().replace('-', '_')])
        body = source.split('typedef struct {')[1].split('} H3Block;')[0]
        fields = []
        for decl in re.findall(r'unsigned int ([^;]+);', body):
            fields.extend(x.strip() for x in decl.split(','))
        self.assertEqual(h3.FIELDS, fields)

    def cli(self, action, case=None, trace=None, extra=(), expected=None):
        with tempfile.TemporaryDirectory(prefix='h3-cli-') as directory:
            d = pathlib.Path(directory)
            elf, kmap, layout, casepath, tracepath = [d / name for name in ('elf','map','layout','case','trace')]
            elf.write_text('ELF'); kmap.write_text('MAP')
            data = dict(self.p.layout, elf_sha256=h3.digest(elf), map_sha256=h3.digest(kmap))
            layout.write_text(json.dumps(data))
            if case is not None:
                case = dict(case, map_sha256=data['map_sha256'])
                casepath.write_text(json.dumps(case))
            if trace is not None:
                trace = dict(trace, map_sha256=data['map_sha256'])
                tracepath.write_text(json.dumps(trace))
            argv = ['h3', action, '--elf', str(elf), '--map', str(kmap), '--layout', str(layout),
                    '--case', str(casepath), '--trace', str(tracepath), *extra]
            with patch.object(sys, 'argv', argv), patch.object(h3, 'Emulator', return_value=self.emu), \
                 patch.object(h3.time, 'sleep') as host_sleep, patch.object(h3.time, 'time', return_value=100), \
                 patch.object(h3, 'Playbook', return_value=self.p):
                if expected:
                    with self.assertRaisesRegex(RuntimeError, expected): h3.main()
                else:
                    h3.main()
                    if action == 'stop':
                        host_sleep.assert_not_called()
                    return json.loads(casepath.read_text())

    def test_cli_guards(self):
        case = self.p.arm(self.ident, 5)
        self.cli('init', case, extra=('--fixture','1'), expected='case file already exists')
        self.cli('arm', case, extra=('--mode','pf'), expected='case already armed')
        case.update(resume_verified=True, reclaimed={}, armed_at=90)
        with patch.object(self.p, 'verify_trace'), patch.object(self.p, 'next_launch', return_value={}):
            self.cli('verify', case, {}, expected='STOP was not sent')
        self.change('phase', 6)
        trace = dict(case_id=case['case_id'], observed_at=100, phase=6,
                     identity=self.ident, foreground_window=42, foreground_app=2)
        self.emu.stop = lambda: None
        self.emu.client = self.cleanup_client()
        self.p.sleep = lambda _: self.emu.word(self.s['tick_count'],
            (self.p.word(self.s['tick_count']) + 25) & 0xffffffff)
        with patch.object(self.p, 'checked', return_value={'phase':6}):
            # The Playbook mock's map hash must match the temporary layout.
            for key, value in [('observed_at',101), ('foreground_app',3),
                               ('foreground_window',43), ('case_id','old'), ('phase',3),
                               ('identity',{})]:
                bad = dict(trace); bad[key] = value
                self.cli('stop', case, bad, expected='STOP foreground not confirmed')
            self.cli('stop', case, dict(trace, observed_at=94), expected='STOP not sent')
            before = self.p.guest_tick()
            sent = self.cli('stop', case, trace)
            self.assertGreaterEqual((self.p.guest_tick() - before) & 0xffffffff, 210)
            self.assertTrue(sent['stop_sent'])
            self.cli('stop', sent, trace, expected='STOP requires a resumed loop case')
        for value in (False, None):
            bad = dict(case, resume_verified=value)
            self.cli('stop', bad, trace, expected='STOP requires')

    def test_cli_hash_guards(self):
        # Do not mock digest: exercise both files independently through argv.
        for changed in ('elf_sha256', 'map_sha256'):
            with tempfile.TemporaryDirectory(prefix='h3-sha-') as directory:
                d = pathlib.Path(directory)
                elf, kmap, layout = [d / name for name in ('elf','map','layout')]
                elf.write_text('ELF'); kmap.write_text('MAP')
                data = dict(elf_sha256=h3.digest(elf), map_sha256=h3.digest(kmap))
                data[changed] = 'old'; layout.write_text(json.dumps(data))
                with patch.object(sys, 'argv', ['h3','init','--elf',str(elf),'--map',str(kmap),
                                              '--layout',str(layout)]):
                    with self.assertRaisesRegex(RuntimeError, 'current map/ELF'): h3.main()

    def test_make_layout(self):
        import os
        cross = pathlib.Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        elf, kmap = ROOT / 'build/out/kernel.elf', ROOT / 'build/out/kernel.map'
        if not cross.exists() or not elf.exists() or not kmap.exists():
            self.skipTest('cross GCC / built ELF/map unavailable')
        layout = h3.make_layout(elf, kmap)
        self.assertEqual(layout['offsets']['slot_in_wait'], 20)
        self.assertEqual(layout['elf_sha256'], h3.digest(elf))
        self.assertNotEqual(layout['trace_sites']['launch_landing'], layout['trace_sites']['resume_landing'])


    def test_capture_client_and_counts(self):
        case = self.p.arm(self.ident, 1)
        frames = [('launch_pending', 0), ('resume_pending', 2), ('consumed', 0)]
        calls = []
        parent = self
        class Client:
            def get(self, path):
                calls.append(path)
                if path == '/api/break': return '{"ok":true,"breakpoints":[]}'
                if path == '/api/instance':
                    return json.dumps(dict(ok=True, trap_pause=bool(frames) and len(calls)>2, user_pause=False))
                if path == '/api/regs':
                    site, pending = frames[0]
                    parent.emu.word(parent.s['g_pending_id'], pending)
                    return json.dumps(dict(ok=True, eip=hex(parent.p.layout['trace_sites'][site]), eax='0x2'))
                raise AssertionError(path)
            def post(self, path):
                calls.append(path)
                if path == '/api/resume': frames.pop(0)
                return http_success(path)
        self.emu.client = Client()
        with patch.object(h3.time, 'monotonic', side_effect=range(100)):
            capture = h3.capture_trace(self.p, case, seconds=8)
        self.assertEqual(len(capture['samples']), 3)
        self.assertEqual(h3.capture_counts(case, capture, self.p.layout),
                         dict(launch_pending=0, resume_pending=1, pending_consumed=1, wm_kill=0, syscall_abort=0))
        self.assertEqual(calls.count('/api/step?n=1'), 3)
        for site in ('launch_pending','resume_pending','consumed','wm_kill'):
            self.assertIn(f"/api/break/del?addr=0x{self.p.layout['trace_sites'][site]:x}", calls)
        for key, value in [('case_id','old'), ('elf_sha256','old'), ('sites',{})]:
            bad = dict(capture); bad[key] = value
            with self.assertRaises(RuntimeError): h3.capture_counts(case, bad, self.p.layout)
        for key, value in [('pending',2), ('irq',1), ('exc',1), ('eip',999)]:
            bad = json.loads(json.dumps(capture)); bad['samples'][-1][key] = value
            with self.assertRaises(RuntimeError): h3.capture_counts(case, bad, self.p.layout)
        bad = dict(capture, samples=capture['samples'][:-1])
        with self.assertRaises(RuntimeError): h3.capture_counts(case, bad, self.p.layout)

    def test_trace_site_parser(self):
        disasm = '''0010 <exec_launch>:
  10: call 90 <exec_setjmp>
  15: test %eax,%eax
  18: call 80 <exec_pending_finish>
  1d: ret
0020 <exec_resume>:
  20: call 90 <exec_setjmp>
  25: test %eax,%eax
  28: call 80 <exec_pending_finish>
  2d: ret
00a0 <exec_kill>:
  a0: push %ebp
  a1: mov %esp,%ebp
00b0 <ring3_abort_check>:
  b0: jmp c0 <ring3_kill_kind>
0080 <exec_pending_finish>:
  80: movl $0x0,0x40
  8a: ret
'''
        sym = dict(exec_pending_finish=0x80, exec_kill=0xa0, g_pending_id=0x40)
        sites = h3.trace_sites(disasm, sym)
        self.assertEqual(sites, dict(pending_finish=0x80, wm_kill=0xa0, syscall_abort=0xb0, launch_landing=0x15,
                                    resume_landing=0x25, launch_pending=0x18, resume_pending=0x28,
                                    consumed=0x8a))
        for old, new in [('  80: movl $0x0,0x40', '  80: nop'),
                         ('  20: call 90 <exec_setjmp>', '  20: nop'),
                         ('  a0: push %ebp', '  a0: nop'),
                         ('  a1: mov %esp,%ebp', '  a1: mov %eax,%ebp'),
                         ('  b0: jmp c0 <ring3_kill_kind>', '  b0: ret')]:
            with self.assertRaises(RuntimeError): h3.trace_sites(disasm.replace(old,new), sym)


    def test_cdecl_stack_argument_capture(self):
        case = self.p.arm(self.ident, 6)
        frames = [2, 3]
        self.emu.word(0x7004, 2)
        parent = self
        calls = []
        class Client:
            def get(self, path):
                calls.append(path)
                if path == '/api/break': return '{"ok":true,"breakpoints":[]}'
                if path == '/api/instance':
                    return json.dumps(dict(ok=True, trap_pause=bool(frames) and len(calls)>2, user_pause=False))
                if path == '/api/regs':
                    parent.emu.word(0x7004, frames[0])
                    return json.dumps(dict(ok=True, eip='0x68', eax='0x63', esp='0x7000'))
                raise AssertionError(path)
            def post(self, path):
                if path == '/api/resume': frames.pop(0)
                return http_success(path)
        self.emu.client = Client()
        with patch.object(h3.time, 'monotonic', side_effect=range(100)):
            capture = h3.capture_trace(self.p, case, seconds=6)
        self.assertEqual([row['argument'] for row in capture['samples']], [2, 3])
        self.assertEqual([row['eax'] for row in capture['samples']], [99, 99])
        self.assertEqual(h3.capture_counts(case, capture, self.p.layout)['wm_kill'], 1)

    def test_capture_hash_and_timestamps(self):
        case = self.p.arm(self.ident, 1)
        trace = self.trace(case)
        with tempfile.TemporaryDirectory(prefix='h3-capture-') as directory:
            path = pathlib.Path(directory) / 'raw.json'
            h3.save_capture(case, trace['capture'], path)
            self.assertEqual(case['capture_record']['sha256'], h3.digest(path))
            self.p.verify_trace(case, trace)
        bad = json.loads(json.dumps(trace)); bad['capture']['samples'][0]['eax'] = 123
        with self.assertRaisesRegex(RuntimeError, 'hash mismatch'): self.p.verify_trace(case, bad)
        for key, value in [('started_at',case['armed_at']-1), ('ended_at',case['armed_at']-1)]:
            bad = json.loads(json.dumps(trace)); bad['capture'][key] = value
            self.record(case, bad['capture'])
            with self.assertRaises(RuntimeError): self.p.verify_trace(case, bad)
        bad = json.loads(json.dumps(trace)); bad['capture']['samples'][0]['observed_at'] = 0
        self.record(case, bad['capture'])
        with self.assertRaisesRegex(RuntimeError, 'sample outside'): self.p.verify_trace(case, bad)
        case.pop('capture_record')
        with self.assertRaisesRegex(RuntimeError, 'missing recorded'): self.p.verify_trace(case, trace)

    def test_kapi_two_exclusive_routes(self):
        case = self.p.arm(self.ident, 6)
        trace = self.trace(case)
        self.recovered(case)
        case['reclaimed'] = self.p.reclaimed(case)
        self.p.verify_trace(case, trace)
        sample = trace['capture']['samples'][0]
        sample.update(site='syscall_abort', eip=105)
        trace['landing'] = 'syscall-abort'
        self.record(case, trace['capture'])
        with self.assertRaisesRegex(RuntimeError, 'counter/capture'): self.p.verify_trace(case, trace)
        self.emu.word(self.s['fault_kill_count'], 1)
        self.emu.word(self.s['ring3_abort_count'], 1)
        case['reclaimed'] = self.p.reclaimed(case)
        self.p.verify_trace(case, trace)
        for counter in ('fault_kill_count', 'ring3_abort_count'):
            case['reclaimed'][counter] = 0
            with self.assertRaisesRegex(RuntimeError, 'counter/capture'): self.p.verify_trace(case, trace)
            case['reclaimed'][counter] = 1
        for bad_sample in [dict(sample), dict(sample, site='wm_kill', eip=104),
                           dict(sample, site='resume_pending', eip=102, pending=2),
                           dict(sample, site='launch_pending', eip=101, pending=2)]:
            bad = json.loads(json.dumps(trace)); bad['capture']['samples'].append(bad_sample)
            self.record(case, bad['capture'])
            with self.assertRaises(RuntimeError): self.p.verify_trace(case, bad)
        trace['capture']['samples'][0]['irq'] = 1
        self.record(case, trace['capture'])
        with self.assertRaises(RuntimeError): self.p.verify_trace(case, trace)

    def test_irq_abort_site_is_not_syscall_route(self):
        case = self.p.arm(self.ident, 5)
        trace = self.trace(case)
        trace['capture']['samples'].insert(0, dict(site='syscall_abort', eip=105,
            current=2, irq=1, exc=0, pending=0, observed_at=case['armed_at']+1))
        self.record(case, trace['capture'])
        self.p.verify_trace(case, trace)

    def test_quiescent_window_retry_and_limit(self):
        case = self.p.arm(self.ident, 1)
        self.recovered(case)
        self.emu.word(self.s['ring3_in_syscall'], 1)
        sleeps = []
        def exit_window(_):
            self.assertEqual(self.emu.depth, 0)
            sleeps.append(1)
            self.emu.word(self.s['ring3_in_syscall'], 0)
        self.p.sleep = exit_window
        self.p.reclaimed(case)
        self.assertEqual(len(sleeps), 1)
        self.emu.word(self.s['ring3_in_syscall'], 1)
        self.p.sleep = lambda _: sleeps.append(1)
        with self.assertRaisesRegex(RuntimeError, 'no quiescent'): self.p.reclaimed(case)
        self.assertEqual(len(sleeps), 21)

    def test_observe_separate_output_and_as_identity(self):
        case = self.p.arm(self.ident, 5)
        with tempfile.TemporaryDirectory(prefix='h3-observe-') as directory:
            output = pathlib.Path(directory) / 'observation.json'
            result = self.cli('observe', case, extra=('--out', str(output)))
            self.assertNotIn('observation', result)
            observation = json.loads(output.read_text())
            self.assertEqual(observation['address_space'], dict(address=0x5000, owner=17, generation=91))
            self.assertEqual(observation['slot']['as'], 0x5000)
        self.cli('observe', case, expected='separate --out')

    def test_live_lock_rejects_competing_cli_before_client(self):
        case = self.p.arm(self.ident, 5)
        with h3.live_lock(), patch.object(h3, 'Emulator') as client:
            for action in ('stop', 'observe', 'trace-watch'):
                with self.assertRaisesRegex(RuntimeError, 'another h3 live'):
                    self.cli(action, case)
            client.assert_not_called()

    def test_capture_rejects_foreign_pause_without_resume(self):
        case = self.p.arm(self.ident, 1)
        calls = []
        class Client:
            def get(self, path):
                calls.append(path)
                if path == '/api/break': return '{"ok":true,"breakpoints":[]}'
                return json.dumps(dict(ok=True, trap_pause=False, user_pause=calls.count('/api/instance')>1))
            def post(self, path):
                calls.append(path)
                return http_success(path)
        self.emu.client = Client()
        with self.assertRaisesRegex(RuntimeError, 'user pause during capture'):
            h3.capture_trace(self.p, case)
        self.assertNotIn('/api/resume', calls)
        self.assertEqual(sum('/api/break/del' in path for path in calls), 5)
        self.assertFalse(self.emu.capture_active)

    def test_delete_integer_status_contract(self):
        path = '/api/break/del?addr=0x143364'
        self.assertEqual(h3.debug_reply(dict(ok=1, removed=1), path)['removed'], 1)
        for reply in (dict(ok=0, removed=0), dict(ok=1, removed=0),
                      dict(ok=1), dict(ok=True, removed=1),
                      dict(ok='1', removed=1), dict(ok=1.0, removed=1),
                      dict(ok=2, removed=1), dict(ok=1, removed=True)):
            with self.subTest(reply=reply), self.assertRaises(RuntimeError):
                h3.debug_reply(reply, path)
        for path in ('/api/instance', '/api/pause', '/api/resume', '/api/mem',
                     '/api/break', '/api/break/add?addr=0x68', '/api/step?n=1',
                     '/api/regs', '/api/key', '/api/mouse'):
            self.assertEqual(h3.debug_reply(dict(ok=True), path), dict(ok=True))
            for ok in (False, 0, 1, 'true', None):
                with self.subTest(path=path, ok=ok), self.assertRaises(RuntimeError):
                    h3.debug_reply(dict(ok=ok), path)

    def test_capture_cleanup_attempts_all_and_resumes_owned_trap(self):
        case = self.p.arm(self.ident, 6)
        calls = []
        parent = self
        class Client:
            trap = False
            def get(self, path):
                if path == '/api/break': return http_success(path)[:-1] + ',"breakpoints":[]}'
                if path == '/api/instance':
                    return json.dumps(dict(ok=True, trap_pause=self.trap, user_pause=False))
                if path == '/api/regs':
                    return json.dumps(dict(ok=True, eip=hex(parent.p.layout['trace_sites']['resume_pending']), eax='0x2'))
                raise AssertionError(path)
            def post(self, path):
                calls.append(path)
                if '/api/break/del' in path and sum('/api/break/del' in c for c in calls) == 1:
                    return '{"ok":0,"removed":0}'
                if path == '/api/resume': self.trap = False
                return http_success(path)
        client = self.emu.client = Client()
        def start():
            client.trap = True
            raise RuntimeError('capture body failed')
        with self.assertRaisesRegex(RuntimeError, 'capture body failed; capture cleanup failed'):
            h3.capture_trace(self.p, case, start=start)
        self.assertEqual(sum('/api/break/del' in c for c in calls), 5)
        self.assertIn('/api/resume', calls)
        self.assertFalse(client.trap)
        self.assertFalse(self.emu.capture_active)

    def test_capture_step_failure_resumes_changed_pc(self):
        case = self.p.arm(self.ident, 6)
        calls = []
        parent = self
        class Client:
            trap = False
            pc = parent.p.layout['trace_sites']['resume_pending']
            def get(self, path):
                if path == '/api/break': return '{"ok":true,"breakpoints":[]}'
                if path == '/api/instance': return json.dumps(dict(ok=True, trap_pause=self.trap, user_pause=False))
                if path == '/api/regs': return json.dumps(dict(ok=True, eip=hex(self.pc), eax='0x2'))
                raise AssertionError(path)
            def post(self, path):
                calls.append(path)
                if path == '/api/step?n=1':
                    self.pc = 999
                    raise RuntimeError('step failed')
                if path == '/api/resume': self.trap = False
                return http_success(path)
        client = self.emu.client = Client()
        with self.assertRaisesRegex(RuntimeError, 'step failed'):
            h3.capture_trace(self.p, case, start=lambda: setattr(client, 'trap', True))
        self.assertEqual(sum('/api/break/del' in c for c in calls), 5)
        self.assertIn('/api/resume', calls)
        self.assertFalse(client.trap)
        self.assertFalse(self.emu.capture_active)

    def test_capture_preserves_unowned_trap(self):
        case = self.p.arm(self.ident, 6)
        calls = []
        class Client:
            trap = False
            def get(self, path):
                if path == '/api/break': return '{"ok":true,"breakpoints":[]}'
                if path == '/api/instance': return json.dumps(dict(ok=True, trap_pause=self.trap, user_pause=False))
                if path == '/api/regs': return '{"ok":true,"eip":"0x999"}'
                raise AssertionError(path)
            def post(self, path):
                calls.append(path)
                return http_success(path)
        client = self.emu.client = Client()
        with self.assertRaisesRegex(RuntimeError, 'unowned debugger trap'):
            h3.capture_trace(self.p, case, start=lambda: setattr(client, 'trap', True))
        self.assertEqual(sum('/api/break/del' in c for c in calls), 5)
        self.assertNotIn('/api/resume', calls)
        self.assertTrue(client.trap)
        self.assertFalse(self.emu.capture_active)

    def cleanup_client(self, *, site='wm_kill', late=False, failed=(), user=False):
        parent = self
        class Client:
            trap = False
            user_pause = False
            bps = set()
            calls = []
            deletes = 0
            def get(self, path):
                self.calls.append(path)
                if path == '/api/break':
                    return json.dumps(dict(ok=True, breakpoints=[dict(eip=hex(a)) for a in sorted(self.bps)]))
                if path == '/api/instance':
                    return json.dumps(dict(ok=True, trap_pause=self.trap, user_pause=self.user_pause))
                if path == '/api/regs':
                    return json.dumps(dict(ok=True, eip=hex(parent.p.layout['trace_sites'][site]),
                                           eax='0x2', esp='0x7000'))
                raise AssertionError(path)
            def post(self, path):
                self.calls.append(path)
                if path.startswith('/api/break/add'):
                    self.bps.add(int(path.split('=')[1], 16))
                if path.startswith('/api/break/del'):
                    self.deletes += 1
                    # First deletion executes the sampled instruction; later ones are cleanup.
                    if late and self.deletes == 2:
                        self.trap = True
                        self.user_pause = user
                    addr = int(path.split('=')[1], 16)
                    if addr in failed:
                        return '{"ok":0,"removed":0}'
                    self.bps.remove(addr)
                if path == '/api/resume': self.trap = False
                return http_success(path)
        self.emu.word(0x7004, 2)
        return Client()

    def test_capture_late_duplicate_is_incomplete(self):
        for site in ('wm_kill', 'syscall_abort'):
            for finish in ('deadline', 'advance'):
                with self.subTest(site=site, finish=finish):
                    client = self.emu.client = self.cleanup_client(site=site, late=True)
                    with patch.object(h3.time, 'monotonic', side_effect=range(100)):
                        with self.assertRaisesRegex(RuntimeError, 'incomplete capture: unrecorded watched trap'):
                            h3.capture_trace(self.p, dict(case_id='late'),
                                seconds=2 if finish == 'deadline' else 10,
                                start=lambda: setattr(client, 'trap', True),
                                advance=(lambda: True) if finish == 'advance' else None)
                    self.assertEqual(client.calls.count('/api/step?n=1'), 1)
                    self.assertEqual(client.calls.count('/api/resume'), 2)
                    self.assertEqual(client.bps, set())
                    self.assertFalse(self.emu.capture_active)

    def test_capture_remaining_breakpoints_prevent_resume(self):
        remaining = (101, 104)
        client = self.emu.client = self.cleanup_client(failed=remaining)
        def start():
            client.trap = True
            raise RuntimeError('capture body failed')
        with self.assertRaises(RuntimeError) as caught:
            h3.capture_trace(self.p, dict(case_id='remaining'), start=start)
        message = str(caught.exception)
        for part in ('capture body failed', 'remaining watched breakpoints: 0x65, 0x68',
                     'PM must delete them manually before resume',
                     'trap_pause=True', 'user_pause=False', 'eip=0x68'):
            self.assertIn(part, message)
        self.assertEqual(client.deletes, 5)
        self.assertEqual(client.bps, set(remaining))
        self.assertNotIn('/api/resume', client.calls)
        self.assertFalse(self.emu.capture_active)

    def test_capture_reports_combined_pause(self):
        client = self.emu.client = self.cleanup_client()
        def start():
            client.trap = client.user_pause = True
            raise RuntimeError('capture body failed')
        with self.assertRaises(RuntimeError) as caught:
            h3.capture_trace(self.p, dict(case_id='pause'), start=start)
        for part in ('capture body failed', 'trap_pause=True', 'user_pause=True', 'eip=0x68'):
            self.assertIn(part, str(caught.exception))
        self.assertNotIn('/api/resume', client.calls)
        self.assertTrue(client.trap and client.user_pause)
        self.assertEqual(client.bps, set())
        self.assertFalse(self.emu.capture_active)

    def test_capture_freeze_races(self):
        emu = h3.Emulator.__new__(h3.Emulator)
        emu.freeze_depth = 0; emu.capture_active = True
        class Client:
            def __init__(self): self.trap = False
            def get(self, path):
                return json.dumps(dict(ok=True, trap_pause=self.trap, user_pause=False))
            def post(self, path):
                self.trap = True
                return '{"ok":false}'
        emu.client = Client()
        with self.assertRaises(h3.CaptureTrap):
            with emu.freeze(): pass
        with self.assertRaises(h3.CaptureTrap):
            with emu.freeze(): pass
        self.assertEqual(emu.freeze_depth, 0)

    def clock_capture(self, *, initial=0, step=25, firing=True, wall_step=3,
                      front_at=0, refresh_at=None, switches=1,
                      firing_tick=500, snapshot_delay=0, resume=True):
        case = self.p.arm(self.ident, 5)
        case['map_sha256'] = 'map'
        client = self.emu.client = self.cleanup_client()
        clock = dict(wall=0, elapsed=0)
        self.emu.word(self.s['tick_count'], initial)
        stops = []
        def sleep(delay):
            clock['wall'] += wall_step
            clock['elapsed'] += step
            if step == 0:
                self.assertLessEqual(clock['wall'], 150, 'capture exceeded stall budget')
            self.assertLessEqual(clock['elapsed'], 3000 if not firing or front_at > 3000 else 5000,
                                 'capture exceeded guest budget')
            if clock['elapsed'] >= front_at and not front.exists():
                front.write_text(json.dumps(evidence))
            if refresh_at is not None and clock['elapsed'] >= refresh_at:
                front.write_text(json.dumps(dict(evidence, observed_at=case['armed_at'] + clock['wall'])))
            self.emu.word(self.s['tick_count'],
                          (initial + clock['elapsed']) & 0xffffffff)
            if firing and clock['elapsed'] >= firing_tick:
                self.change('phase', h3.PHASES['FIRING'])
        self.p.sleep = sleep
        original_observe = self.p.observe
        def observe(value):
            if snapshot_delay and not clock.get('delayed'):
                clock['delayed'] = True
                clock['wall'] += snapshot_delay
            return original_observe(value)
        self.p.observe = observe
        def click():
            self.assertEqual(len(client.bps), 5)
            if not resume:
                return
            self.change('phase', h3.PHASES['ARMED'])
            self.change('arm', 0); self.change('consumed', 1)
            self.change('resumes', 9)
            self.emu.word(self.s['ring3_switch_count'], switches)
        def stop():
            self.assertEqual(self.emu.depth, 0)
            stops.append((clock['wall'], clock['elapsed']))
            client.trap = True
        self.emu.stop = stop
        with tempfile.TemporaryDirectory(prefix='h3-clock-') as directory:
            d = pathlib.Path(directory)
            front = d / 'front.json'
            # Model a fresh PM foreground observation when asked, without
            # changing the actual identity/freshness validation.
            evidence = dict(case_id=case['case_id'], identity=self.ident,
                observed_at=case['armed_at'], phase=6, map_sha256='map',
                foreground_window=42, foreground_app=2)
            if front_at == 0:
                front.write_text(json.dumps(evidence))
            original = self.p.foreground
            def foreground(*args):
                if refresh_at is None:
                    front.write_text(json.dumps(evidence))
                return original(*args)
            with patch.object(h3.time, 'monotonic', side_effect=lambda: clock['wall']), \
                 patch.object(h3.time, 'time', side_effect=lambda: case['armed_at'] +
                     (clock['wall'] if refresh_at is not None else 1)), \
                 patch.object(self.p, 'foreground', side_effect=foreground):
                try:
                    self.p.run_capture(case, d/'raw.json', d/'out.json', front, click=click)
                finally:
                    self.assertEqual(client.bps, set())
                    self.assertFalse(client.trap)
                    self.assertFalse(self.emu.capture_active)
                    if step == 0:
                        self.assertGreaterEqual(clock['wall'], 120)
            obs = json.loads((d/'out.json').read_text())['observations']
            self.assertEqual(set(obs), {'before_stop', 'after_2s', 'after_10s', 'final'})
        self.assertEqual(len(stops), 1)
        return stops[0]

    def timed_http_capture(self, mode):
        # Real Emulator HTTP calls cost 0.1 host seconds. 22 ticks/s while
        # thawed produces ~8 ticks/s overall with the old repeated freezes
        # (~1/10 of the 100Hz PIT, 3000 ticks in ~365s). PM started the 90s
        # writer ~18s before capture: its last refresh is near arm + 71s.
        case = self.p.arm(self.ident, mode)
        case['map_sha256'] = 'map'
        memory = self.emu
        debugger = self.cleanup_client()
        clock = dict(wall=0., ticks=0., next_front=1.5, writes=0)
        stops = []
        parent = self
        with tempfile.TemporaryDirectory(prefix='h3-http-clock-') as directory:
            d = pathlib.Path(directory)
            front = d / 'front.json'
            def progress(delay):
                clock['wall'] += delay
                if not client.paused and not debugger.trap:
                    clock['ticks'] += delay * 22
                parent.assertLess(clock['wall'], 1500, 'unbounded capture')
                memory.word(parent.s['tick_count'], int(clock['ticks']))
                if clock['ticks'] >= 500:
                    parent.change('phase', 6)
                if clock['wall'] >= clock['next_front'] and clock['wall'] + 18 < 90:
                    front.write_text(json.dumps(dict(case_id=case['case_id'],
                        identity=parent.ident, map_sha256='map', phase=6,
                        foreground_window=42, foreground_app=2,
                        observed_at=case['armed_at'] + clock['wall'])))
                    clock['writes'] += 1
                    clock['next_front'] = clock['wall'] + (1.5 if clock['writes'] % 2 else 3)
            class Client:
                paused = False
                pauses = 0
                def get(self, path):
                    progress(0.1)
                    if path.startswith('/api/mem?'):
                        from urllib.parse import parse_qs, urlsplit
                        q = parse_qs(urlsplit(path).query)
                        raw = memory.read(int(q['addr'][0], 16), int(q['len'][0]))
                        return json.dumps(dict(ok=True, hex=raw.hex()))
                    if path == '/api/instance':
                        return json.dumps(dict(ok=True, user_pause=self.paused,
                                               trap_pause=debugger.trap))
                    return debugger.get(path)
                def post(self, path):
                    progress(0.1)
                    if path == '/api/pause':
                        self.paused = True
                        self.pauses += 1
                        return http_success(path)
                    if path == '/api/resume' and self.paused:
                        self.paused = False
                        return http_success(path)
                    return debugger.post(path)
            client = Client()
            emu = h3.Emulator.__new__(h3.Emulator)
            emu.client = client
            emu.freeze_depth = 0
            self.p.emu = emu
            self.p.sleep = progress
            def click():
                self.change('phase', 5)
                self.change('arm', 0); self.change('consumed', 1)
                self.change('resumes', 9)
                memory.word(self.s['ring3_switch_count'], 1)
            def stop():
                self.assertFalse(client.paused)
                stops.append((clock['wall'], clock['ticks']))
                debugger.trap = True
            emu.stop = stop
            with patch.object(h3.time, 'monotonic', side_effect=lambda: clock['wall']), \
                 patch.object(h3.time, 'time', side_effect=lambda: case['armed_at'] + clock['wall']):
                try:
                    self.p.run_capture(case, d/'raw.json', d/'out.json', front, click=click)
                finally:
                    self.assertEqual(debugger.bps, set())
                    self.assertFalse(debugger.trap or client.paused)
            self.assertEqual(len(stops), 1)
            self.assertGreaterEqual(stops[0][1], 710)
            self.assertLess(stops[0][0], 72, 'PM writer expired before STOP')
            self.assertLessEqual(client.pauses, 6, 'waiting polls must not freeze')
            self.assertLessEqual(case['foreground_observation']['observed_at'],
                                 case['armed_at'] + stops[0][0])

    def test_http_cost_slow_user_loop_periodic_foreground(self):
        self.timed_http_capture(5)

    def test_http_cost_slow_kapi_loop_periodic_foreground(self):
        self.timed_http_capture(6)

    def test_standalone_stop_guest_grace_and_freshness_retry(self):
        case = self.p.arm(self.ident, 5)
        case.update(map_sha256='map', resume_verified=True)
        self.change('phase', 6)
        client = self.emu.client = self.cleanup_client()
        initial = 0xffffff80
        self.emu.word(self.s['tick_count'], initial)
        clock = dict(wall=0, ticks=0)
        stops = []
        self.emu.stop = lambda: stops.append(clock['ticks'])
        with tempfile.TemporaryDirectory(prefix='h3-stop-') as directory:
            front = pathlib.Path(directory) / 'front.json'
            evidence = dict(case_id=case['case_id'], identity=self.ident,
                observed_at=case['armed_at'], phase=6, map_sha256='map',
                foreground_window=42, foreground_app=2)
            front.write_text(json.dumps(evidence))
            def sleep(_):
                self.assertEqual(self.emu.depth, 0)
                clock['ticks'] += 25
                clock['wall'] += 3  # 210 guest ticks take more than 5 host seconds.
                self.assertLessEqual(clock['ticks'], 300)
                self.emu.word(self.s['tick_count'], (initial + clock['ticks']) & 0xffffffff)
                if clock['ticks'] == 300:
                    front.write_text(json.dumps(dict(evidence,
                        observed_at=case['armed_at'] + clock['wall'])))
            self.p.sleep = sleep
            with patch.object(h3.time, 'time', side_effect=lambda: case['armed_at'] + clock['wall']), \
                 patch.object(h3.time, 'monotonic', side_effect=lambda: clock['wall']):
                persisted = []
                def persist():
                    self.assertTrue(case['stop_sent'])
                    self.assertEqual(len(client.bps), 5)  # Persist before cleanup can fail.
                    persisted.append(1)
                self.p.stop_loop(case, front, persist=persist)
                self.assertEqual(persisted, [1])
        self.assertEqual(stops, [300])
        self.assertTrue(case['stop_sent'])
        self.assertEqual(client.bps, set())
        self.assertFalse(self.emu.capture_active)

    def test_capture_normal_guest(self):
        wall, tick = self.clock_capture(wall_step=0.25)
        self.assertLess(wall, 30)
        self.assertGreaterEqual(tick, 710)

    def test_capture_slow_guest_and_runaway_grace(self):
        wall, tick = self.clock_capture()
        self.assertGreater(wall, 30)
        self.assertGreaterEqual(tick, 710)  # FIRING + 210 guest ticks

    def test_capture_tick_wrap(self):
        wall, tick = self.clock_capture(initial=0xfffffd80)
        self.assertGreater(wall, 30)
        self.assertGreaterEqual(tick, 710)

    def test_capture_stalled_guest_fails_and_cleans_up(self):
        with self.assertRaisesRegex(RuntimeError, 'guest tick stalled'):
            self.clock_capture(step=0)

    def test_capture_guest_budget_without_firing(self):
        with self.assertRaisesRegex(RuntimeError, 'STOP not sent during capture: FIRING not observed'):
            self.clock_capture(firing=False)

    def test_capture_guest_budget_across_wrap(self):
        with self.assertRaisesRegex(RuntimeError, 'STOP not sent during capture: FIRING not observed'):
            self.clock_capture(firing=False, initial=0xfffffd80)

    def test_capture_late_stop_completes_after_guest_limit(self):
        wall, tick = self.clock_capture(front_at=2900, wall_step=0.25)
        self.assertEqual(tick, 2900)
        self.assertGreater(wall, 12)

    def test_capture_late_foreground_without_stop_is_bounded(self):
        with self.assertRaisesRegex(RuntimeError, 'foreground evidence missing'):
            self.clock_capture(front_at=3100, wall_step=0.25)

    def test_capture_stale_foreground_waits_for_refresh(self):
        wall, tick = self.clock_capture(refresh_at=900)
        self.assertEqual(tick, 900)
        self.assertGreater(wall, 5)

    def test_capture_stale_foreground_without_refresh_is_bounded(self):
        with self.assertRaisesRegex(RuntimeError, 'foreground evidence stale'):
            self.clock_capture(refresh_at=3100)

    def test_capture_late_firing_reports_remaining_grace(self):
        with self.assertRaisesRegex(RuntimeError, '210 ticks not reached: FIRING elapsed=100'):
            self.clock_capture(firing_tick=2900, wall_step=0.25)

    def test_capture_guest_boundary_gets_last_stop_attempt(self):
        wall, tick = self.clock_capture(firing_tick=2775, wall_step=0.25)
        self.assertEqual(tick, 3000)

    def test_capture_no_resume_reports_resume_missing(self):
        with self.assertRaisesRegex(RuntimeError, 'resume not captured'):
            self.clock_capture(firing=False, resume=False)

    def test_capture_snapshot_cost_requires_fresh_evidence_again(self):
        wall, tick = self.clock_capture(refresh_at=700, snapshot_delay=6)
        self.assertGreater(tick, 725, 'stale final snapshot must wait for refresh')

    def test_foreground_missing_fields_reports_invalid_evidence(self):
        case = self.p.arm(self.ident, 5)
        with tempfile.TemporaryDirectory(prefix='h3-invalid-front-') as directory:
            path = pathlib.Path(directory) / 'front.json'
            path.write_text('{}')
            with self.assertRaisesRegex(RuntimeError, 'foreground evidence missing/invalid'):
                self.p.foreground(case, path)

    def negative_loop(self, *, standalone=False, race=None, after_firing=False,
                      before_resume=None, stale=False):
        case = self.p.arm(self.ident, 5)
        case.update(map_sha256='map', resume_verified=before_resume is None)
        self.change('phase', 3 if before_resume else 6)
        stops = []
        self.emu.stop = lambda: stops.append(1)
        tick = [0]
        self.p.guest_tick = lambda: tick[0]
        freezes = []
        original_freeze = self.emu.freeze
        @contextmanager
        def freeze():
            if self.emu.depth == 0:
                freezes.append(1)
                if race == 'phase':
                    self.change('phase', 7)
                elif race == 'generation':
                    self.emu.word(0x5000, 92)
                if stale:
                    clock[0] += 6
            with original_freeze():
                yield
        self.emu.freeze = freeze
        clock = [case['armed_at'] + 1]
        with tempfile.TemporaryDirectory(prefix='h3-negative-') as directory:
            d = pathlib.Path(directory)
            front = d/'front.json'
            front.write_text(json.dumps(dict(case_id=case['case_id'],
                identity=self.ident, observed_at=case['armed_at'], phase=6,
                map_sha256='map', foreground_window=42, foreground_app=2)))
            def pump(p, value, **kwargs):
                # Real callbacks, live polling followed by the final frozen
                # snapshot. Stop after three polls so missing checks surface.
                for i in range(3):
                    tick[0] = i * 210
                    if i == 1:
                        if after_firing or before_resume == 'ERROR':
                            self.change('phase', 7)
                        elif before_resume == 'generation':
                            self.emu.word(0x5000, 92)
                        elif before_resume == 'dead':
                            self.emu.word(self.slot, 0)
                    if kwargs['advance']():
                        break
                return dict(started_at=clock[0], ended_at=clock[0], samples=[])
            with patch.object(h3, 'capture_trace', side_effect=pump), \
                 patch.object(h3.time, 'time', side_effect=lambda: clock[0]):
                try:
                    if standalone:
                        self.p.stop_loop(case, front)
                    else:
                        self.p.run_capture(case, d/'raw.json', d/'out.json', front)
                finally:
                    self.assertEqual(stops, [], 'invalid fixture/evidence must never send STOP')
                    if before_resume in ('generation', 'dead'):
                        self.assertEqual(freezes, [], 'resume wait must not freeze')

    def test_stop_snapshot_rejects_phase_race(self):
        for standalone in (False, True):
            with self.subTest(standalone=standalone):
                self.setUp()
                with self.assertRaisesRegex(RuntimeError, 'loop not firing'):
                    self.negative_loop(standalone=standalone, race='phase')

    def test_stop_snapshot_rejects_generation_race(self):
        for standalone in (False, True):
            with self.subTest(standalone=standalone):
                self.setUp()
                with self.assertRaisesRegex(RuntimeError, 'owner/generation/address changed'):
                    self.negative_loop(standalone=standalone, race='generation')

    def test_standalone_stop_snapshot_cost_requires_fresh_evidence(self):
        with self.assertRaisesRegex(RuntimeError, 'foreground evidence stale'):
            self.negative_loop(standalone=True, stale=True)

    def test_capture_phase_changed_after_firing(self):
        with self.assertRaisesRegex(RuntimeError, 'loop not firing: phase changed after FIRING'):
            self.negative_loop(after_firing=True)

    def test_standalone_stop_requires_firing_immediately(self):
        case = self.p.arm(self.ident, 5)
        for phase in (3, 5, 7):
            with self.subTest(phase=phase):
                self.change('phase', phase)
                client = self.emu.client = self.cleanup_client()
                self.emu.stop = lambda: self.fail('unexpected STOP')
                self.p.sleep = lambda _: self.fail('non-FIRING stop waited')
                with self.assertRaisesRegex(RuntimeError, 'loop not firing'):
                    self.p.stop_loop(case, '/missing-front.json')
                self.assertEqual(client.bps, set())
                self.assertFalse(self.emu.capture_active)
                self.change('phase', 3); self.change('arm', 0)

    def test_capture_fixture_error_before_resume(self):
        with self.assertRaisesRegex(RuntimeError, 'fixture ERROR'):
            self.negative_loop(before_resume='ERROR')

    def test_capture_fixture_generation_changed_before_resume(self):
        with self.assertRaisesRegex(RuntimeError, 'changed before resume'):
            self.negative_loop(before_resume='generation')

    def test_capture_fixture_died_before_resume(self):
        with self.assertRaisesRegex(RuntimeError, 'fixture died before resume'):
            self.negative_loop(before_resume='dead')

    def test_capture_parked_requires_switch_increment(self):
        with self.assertRaisesRegex(RuntimeError, 'missing switch increment'):
            self.clock_capture(switches=0, wall_step=0.25)

    def test_serialized_capture_click_stop_observations(self):
        case = self.p.arm(self.ident, 6)
        case['map_sha256'] = 'map'
        frames = []
        events = []
        bps = set()
        parent = self
        class Client:
            def get(self, path):
                if path == '/api/break': return '{"ok":true,"breakpoints":[]}'
                if path == '/api/instance':
                    return json.dumps(dict(ok=True, trap_pause=bool(frames), user_pause=False))
                if path == '/api/regs':
                    return json.dumps(dict(ok=True, eip='0x68', eax='0x63', esp='0x7000'))
                raise AssertionError(path)
            def post(self, path):
                if path.startswith('/api/break/add'): bps.add(path.split('=')[1])
                if path.startswith('/api/break/del'): bps.discard(path.split('=')[1])
                if path == '/api/resume': frames.pop(0)
                return http_success(path)
        self.emu.client = Client()
        self.emu.word(0x7004, 2)
        def click():
            self.assertEqual(len(bps), 5)
            events.append('click')
            self.change('phase', 6); self.change('arm', 0); self.change('consumed', 1)
            self.change('resumes', 9); self.emu.word(self.s['ring3_switch_count'], 1)
        def stop():
            self.assertEqual(self.emu.depth, 0)
            events.append('stop'); frames.append(1)
        self.emu.stop = stop
        original_observe = self.p.observe
        def observe(value):
            self.assertFalse(frames, 'observe must wait until trap is drained')
            events.append('observe')
            return original_observe(value)
        self.p.sleep = lambda _: self.emu.word(self.s['tick_count'],
            (self.p.word(self.s['tick_count']) + 25) & 0xffffffff)
        ticks = iter(x / 4 for x in range(1000))
        with tempfile.TemporaryDirectory(prefix='h3-serial-') as directory:
            d = pathlib.Path(directory)
            foreground = d / 'foreground.json'
            foreground.write_text(json.dumps(dict(case_id=case['case_id'], identity=self.ident,
                observed_at=case['armed_at'], phase=6, map_sha256='map', foreground_window=42, foreground_app=2)))
            with patch.object(h3.time, 'monotonic', side_effect=lambda: next(ticks)), \
                 patch.object(h3.time, 'time', return_value=case['armed_at']+1), \
                 patch.object(self.p, 'observe', side_effect=observe):
                self.p.run_capture(case, d/'raw.json', d/'out.json', foreground, click=click)
            output = json.loads((d/'out.json').read_text())
            self.assertEqual(set(output['observations']), {'before_stop','after_2s','after_10s','final'})
            self.assertEqual(case['capture_record']['sha256'], h3.digest(d/'raw.json'))
        self.assertEqual(events[:3], ['click', 'observe', 'stop'])
        self.assertTrue(case['resume_verified'] and case['stop_sent'])
        self.assertEqual(bps, set())



def c_run(directory, mutation=None):
    directory = pathlib.Path(directory)
    source = (ROOT / 'userland/tests/h3/state.inc').read_text()
    if mutation:
        old, new = mutation
        assert source.count(old) == 1, (old, source.count(old))
        source = source.replace(old, new)
    target = directory / 'userland/tests/h3'
    target.mkdir(parents=True, exist_ok=True)
    (target / 'state.inc').write_text(source)
    (target / 'protocol.h').write_bytes((ROOT / 'userland/tests/h3/protocol.h').read_bytes())
    exe = directory / 'state'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
                    '-fno-stack-protector', '-nostdlib', '-Wl,-e,_start', '-I' + str(directory),
                    str(ROOT / 'tools/tests/h3_state_host.c'), '-o', str(exe)], check=True)
    result = run([str(exe)], capture_output=True).returncode
    if result: return result
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
                    '-fno-stack-protector', '-nostdlib', '-Wl,-e,_start', '-DH3_SELF_TEST',
                    '-I'+str(directory), str(ROOT/'tools/tests/h3_state_host.c'),
                    '-o', str(exe)], check=True)
    result = run([str(exe)], capture_output=True).returncode
    if result: return result
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
                    '-fno-stack-protector', '-nostdlib', '-Wl,-e,_start', '-DH3_SLOT_TEST',
                    '-I'+str(directory), '-I'+str(ROOT/'sdk/include/os32'),
                    str(ROOT/'tools/tests/h3_state_host.c'), '-o', str(exe)], check=True)
    return run([str(exe)], capture_output=True).returncode


C_MUTANTS = [
    ('(h3_api->caller_identity((a), (o), (g)) == 0)', '(h3_api->caller_identity((a), (o), (g)) != 0)'),
    ('b->owner = owner;', 'b->owner = owner + 1;'),
    ('b->generation = generation;', 'b->generation = generation + 1;'),
    ('!b->owner || !b->generation', '!b->owner && !b->generation'),
    ('b->phase != H3_RESUMED || ', ''),
    ('b->arm != 1 || ', ''),
    (' || b->consumed != 0', ''),
    ('mode < H3_PF || mode > H3_KAPI_LOOP', '0'),
    ('b->arm = 0;', 'b->arm = 1;'),
    ('b->consumed++;', 'b->consumed += 2;'),
    ('b->phase = H3_ARMED;', 'b->phase = H3_WAIT;'),
]
PY_MUTANTS = [
    ("self.checked(ident)  # fixture values must equal host identity()", "pass", 1),
    ("((s['__bss_end'] + 4095) & ~4095)", "(s['__bss_end'] & ~4095)", 1),
    ("base + index * o['block_size']", "base", 1),
    ("self.identity(ident['index']) == ident", 'True', 1),
    ("(b['owner'], b['generation']) == (ident['owner'], ident['generation'])", 'True', 1),
    ("state == self.o['parked']", 'True', 1),
    ("self.word(ident['slot'] + self.o['slot_wait']) == 1", 'True', 1),
    ("b['resumes'] > case['resumes']", 'True', 1),
    ("switches > before_switches", 'True', 1),
    ("b['consumed'] == 1", 'True', 1),
    ("c[name] == 0", 'True', 1),
    ("trace['pending_consumed'] == pending", 'True', 1),
    ("trace['landing'] == landing", 'True', 1),
    ("fresh['generation'] != case['identity']['generation']", 'True', 1),
    ("trace['foreground_app'] == case['identity']['app']", 'True', 2),
]


PY_MUTANTS += [
    ("not args.case.exists()", "True", 1),
    ("'mode' not in case", "True", 1),
    ("digest(args.elf) == layout['elf_sha256']", "True", 1),
    ("digest(args.map) == layout['map_sha256']", "True", 1),
    ("time.time() - trace['observed_at'] > 5", "False", 1),
    ("not case.get('stop_sent')", "True", 3),
    ("case['mode'] < 5 or case.get('stop_sent') is True", "True", 2),
    ("trace['foreground_window'] == case['window']", "True", 2),
    ("c['fault_kill_count'] - case['before']['fault_kill_count'] == kills", "True", 1),
    ("c['ring3_abort_count'] - case['before']['ring3_abort_count'] == aborts", "True", 1),
    ("trace['vector'] == VECTORS[case['mode']]", "True", 1),
    ("self.word(s['g_cur']) == survivor['app']", "True", 1),
    ("self.word(survivor['slot'] + o['slot_state']) == o['running']", "True", 1),
    ("self.word(survivor['slot'] + o['slot_in_wait']) == 1", "True", 1),
    ("((0, 0), (1, 1)) if in_wait else", "((0, 0),) if in_wait else", 1),
    ("if self.quiescent(case, self.counters()):", "if False:", 1),
    ("continue  # unrelated SHM / multi-block tail is not our fixture", "raise RuntimeError('unrelated SHM')", 1),
    ("trace['resume_pending'] == pending", "True", 1),
    ("trace['launch_pending'] == 0", "True", 1),
    ("sample['pending'] == 0 and sample['irq'] == 0 and sample['exc'] == 0", "True", 1),
    ("counts == dict(launch_pending=0, resume_pending=pending,", "True or counts == dict(launch_pending=0, resume_pending=pending,", 1),
]


PY_MUTANTS += [
    ("sample['argument'] == case['identity']['app']", "sample['eax'] == case['identity']['app']", 1),
    ("sample['esp'] + 4", "sample['esp']", 1),
    ("re.fullmatch(r'push\\s+%ebp', kill[0][1])", "True", 1),
    ("hashlib.sha256(json_bytes(capture)).hexdigest() == record['sha256']", "True", 1),
    ("case['armed_at'] <= capture['started_at'] <= capture['ended_at']", "True", 1),
    ("reclaimed['fault_kill_count'] - case['before']['fault_kill_count'] == syscall", "True", 1),
    ("sample['irq'] == 0 and sample['exc'] == 0):", "True):", 1),
    ("fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)", "pass", 1),
    ("if start:\n            start()", "if False:\n            start()", 1),
    ("for delay in (2, 10):", "for delay in (10,):", 1),
]


PY_MUTANTS += [
    ("type(reply.get('ok')) is int and reply['ok'] == 1", "reply.get('ok') is True", 1),
    ("type(reply.get('removed')) is int and reply['removed'] == 1", "True", 1),
    ("errors.append(str(exc))\n        try:", "errors.append(str(exc)); break\n        try:", 1),
    ("if can_resume:", "if False:", 1),
]


PY_MUTANTS += [
    ("if failure is None and watched_trap:", "if False:", 1),
    ("not remaining and instance['trap_pause']", "instance['trap_pause']", 1),
    ("if remaining:", "if False:", 1),
    ("if errors or not can_resume:", "if False:", 1),
]


PY_MUTANTS += [
    ("case.get('armed_in_running_wait') is True", "False", 1),
    ("switches == before_switches", "True", 1),
    ("advance=advance, guest_time=True", "advance=advance, guest_time=False", 2),
    ("parked_wait or running_wait", "parked_wait", 1),
    ("state == self.o['running']", "True", 1),
    ("self.word(self.s['g_cur']) == ident['app']", "True", 1),
    ("self.word(ident['slot'] + self.o['slot_in_wait']) == 1", "True", 1),
    ("seconds * GUEST_HZ", "500 * GUEST_HZ", 1),
    ("now - progressed_at < TICK_STALL_SECONDS", "now - progressed_at < 30", 1),
    ("(tick - first_tick) & 0xffffffff", "tick - first_tick", 1),
    ("(self.guest_tick() - firing_at) & 0xffffffff", "self.guest_tick() - firing_at", 1),
    ("< RUNAWAY_WAIT_TICKS or not Path(foreground).exists()", "< 0 or not Path(foreground).exists()", 1),
]


PY_MUTANTS += [
    ("TICK_STALL_SECONDS = 120", "TICK_STALL_SECONDS = 1000", 1),
    ("first_tick = tick", "first_tick = (tick - 1000) & 0xffffffff", 1),
    ("firing_at = None\n        stop_at = None", "case['armed_in_running_wait'] = True\n        firing_at = None\n        stop_at = None", 1),
    ("not case.get('stop_sent') and", "True and", 1),
    ("if evidence is None:", "if False:", 4),
    ("p.stop_loop(case, args.trace, persist=lambda: args.case.write_bytes(json_bytes(case)))", "p.foreground(case, args.trace); p.emu.stop(); case['stop_sent'] = True", 1),
]


PY_MUTANTS += [
    ("    def guest_tick(self):", "    @frozen\n    def guest_tick(self):", 1),
    ("return self.word(case['identity']['address'] + FIELDS.index('phase') * 4)",
     "return self.checked(case['identity'])['phase']", 1),
    ("evidence = self.loop_evidence(case, foreground, firing_at, status)\n            if evidence is None:\n                return\n            stop_at",
     "# Omit the final freshness check.\n            stop_at", 1),
    ("'STOP not sent during capture: ' + status['reason']", "'STOP not sent during capture'", 2),
]



PY_MUTANTS += [
    ("require(self.checked(case['identity'])['phase'] == PHASES['FIRING'], 'loop not firing')",
     "pass  # omit frozen phase check", 2),
    ("self.checked(case['identity'])['phase'] == PHASES['FIRING']",
     "self.block(case['identity']['address'])['phase'] == PHASES['FIRING']", 2),
    ("# Identity reads also cost host time; refresh evidence after them.\n            evidence = self.loop_evidence(case, foreground, firing_at, status)\n            if evidence is None:\n                return",
     "# Omit standalone STOP freshness recheck.", 1),
    ("(PHASES['ARMED'], PHASES['FIRING'], PHASES['ERROR'])",
     "(PHASES['ARMED'], PHASES['FIRING'])", 1),
    ("self.resume_wait_alive(case)", "pass", 1),
    ("require(firing_at is None, 'loop not firing: phase changed after FIRING')", "pass", 1),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--module', type=pathlib.Path)
    args = parser.parse_args()
    global h3
    if args.module:
        h3 = module(args.module)
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    if not result.wasSuccessful(): return 1
    if args.module: return 0
    with tempfile.TemporaryDirectory(prefix='h3-test-') as directory:
        assert c_run(directory) == 0, 'ILP32 baseline'
        print('ILP32: state/default/self/SDK caller_identity slot PASS')
        if args.mutate:
            for mutant in C_MUTANTS:
                rc = c_run(directory, mutant)
                assert rc > 0, ('not runtime RED', mutant, rc)
            source = (ROOT / 'tools/h3_park_resume.py').read_text()
            for old, new, count in PY_MUTANTS:
                assert source.count(old) == count, (old, source.count(old), count)
                copy = pathlib.Path(directory) / 'playbook.py'
                copy.write_text(source.replace(old, new))
                p = subprocess.run([sys.executable, __file__, '--module', str(copy)], capture_output=True)
                assert p.returncode == 1 and b'FAILED (failures=' in p.stderr and b'errors=' not in p.stderr, (old, p.stderr.decode())
            print(f'mutations: {len(C_MUTANTS) + len(PY_MUTANTS)} runtime RED, compile failures 0')
    return 0


if __name__ == '__main__':
    sys.exit(main())
