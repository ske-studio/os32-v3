#!/usr/bin/env python3
"""Stage G: fake guest request stream through three real HostFS adapters.

Counts isolate tree work: label/gate traffic is not modeled. They check
§5's variable terms; open uses its two-STAT assumption, not measured VFS.
This tests the wire flow; production guest policy is tested by inventory-sync.
No sockets, serial device, system emulator, or product source edits are used.
"""
CHECK = dict(name='hsync-inventory-flow', tier=1, needs=[], budget=30,
    covers=['tools/tests/test_hsync_inventory_flow.py', 'tools/serialfs_host.py',
            'tools/host_tree_manifest.py', 'tools/lanfs_host.py',
            'tools/rshell_serial.py', 'tools/rshell_hwb.py', 'tools/rshell_peek.py',
            'tools/host_sfs_link.py', 'tools/host_agent.py', 'tools/host_image.py',
            'tools/lan_bridge.py', 'tools/host_linux.py', 'tools/host_notify.py',
            'tools/host_services.py', 'tools/host_rshell.py', 'tools/host_operator_socket.py',
            'tools/host_agent_config.py'],
    basis=['HSYNC_MANIFEST_DESIGN §§5,7,8; confirmed note 3'],
    env_limit='Python fake guest; VFS STAT multiplicity and production hsync are not executed.')

from collections import Counter
from contextlib import ExitStack
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import serialfs_host as sfs
import host_tree_manifest as tree
import lanfs_host as lan
import rshell_serial as serial
import host_agent as agent

PEER, HOST = b'guest!', b'host!!'
NEW = b'/.os32-manifest/new'
SIZES = (507, 508, 509, 1023, 1024, 1025)


class Failure(Exception):
    def __init__(self, code):
        self.code = code


def frame(raw, offset=0, cap=1028):
    result = sfs.try_frame(raw, offset, cap)
    assert result[0] == 'frame' and result[1] + offset <= len(raw)
    return result[2]


class Route:
    def __init__(self, kind, root):
        self.kind, self.seq, self.sid = kind, 0, 0
        self.counts, self.busy, self.unavail = Counter(), 0, 0
        self.now, self.delay = 0.0, 0.0
        self.cap = 512 if kind == 'serial' else 1028
        if kind == 'serial':
            self.server = serial.make_server(SimpleNamespace(serve_host=str(root), allow_write=[]))
            self.fs = self.server.fs
        elif kind == 'lan':
            self.fs = sfs.HostFS(root)
            self.server = sfs.Server(self.fs, max_payload=self.cap)
            self.adapter = lan.LanServer(HOST, self.server, peer=PEER)
        else:
            self.agent = agent.HostAgent(HOST, file_root=str(root), peer=PEER,
                                         allow_net=False, quiet=True)
            self.agent.peer = PEER
            self.agent.now = lambda: self.now
            self.fs = self.agent._sfs_backend().fs
        self.sid = self.exchange(sfs.T_HELLO, struct.pack('<HHI', 1, self.cap, 42)).sid

    def exchange(self, op, payload):
        self.seq += 1
        wire = sfs.encode(op, self.sid, self.seq if self.sid else 0, payload, self.cap)
        if self.kind == 'serial':
            raw = self.server.handle(frame(wire, cap=self.cap))
        elif self.kind == 'lan':
            raw = self.adapter.handle(HOST + PEER + struct.pack('>H', lan.ETHERTYPE) + wire)
            raw = raw[14:]
        else:
            # Actual CALL declaration and pre/post backend wall-clock checks.
            ent = dict(req=('SFS CALL %d' % len(wire)).encode(), got=wire)
            out = agent.ListSink()
            self.agent._serve(self.seq, ent, out)
            assert ent['sfs_deadline'] == self.now + 3.0
            self.now += self.delay
            self.agent._service_sfs(self.seq, ent, out)
            status, _ = ent['resp']
            self.unavail += status == 503
            if status != 200:
                raise Failure(status)
            raw = ent['body']
        return frame(raw, cap=self.cap)

    def call(self, op, path, prefix=b''):
        self.counts[op] += 1
        reply = self.exchange(op, prefix + bytes([len(path)]) + path)
        assert reply.sid == self.sid and reply.seq == self.seq
        rc = struct.unpack_from('<i', reply.payload)[0]
        if rc < 0:
            raise Failure(rc)
        return reply.payload[4:]

    def stat(self, path):
        return struct.unpack('<BII', self.call(sfs.T_STAT, path))

    def opened(self, path):
        self.stat(path)
        return self.stat(path)[1]

    def read(self, path, size):
        data = bytearray()
        # Inventory's 64512-byte VFS request boundary is also a wire boundary.
        while len(data) < size:
            end = min(size, len(data) + 64512)
            while len(data) < end:
                want = min(end - len(data), self.cap - 4)
                block = self.call(sfs.T_READ, path, struct.pack('<IH', len(data), want))
                assert len(block) == want, 'short READ'
                data.extend(block)
        return bytes(data)

    def inventory(self):
        try:
            self.opened(NEW)
            desc = self.read(NEW, 32)
        except Failure as exc:
            if exc.code in (-2, -6, -10, -9, -16):
                return None  # old host fallback
            raise
        for _ in range(1000):
            magic, version, state, token, size, error = struct.unpack('<4sHH16sIi', desc)
            assert magic == b'HMD1' and version == 1
            if state != tree.BUSY:
                break
            self.busy += 1
            path = b'/.os32-manifest/status/' + token.hex().encode()
            self.opened(path)
            desc = self.read(path, 32)
        else:
            raise AssertionError('BUSY did not terminate')
        if state == tree.ERROR:
            if error == tree.ERR_FULL:
                return None  # descriptor FULL fallback
            raise Failure(error)
        assert state == tree.READY
        path = b'/.os32-manifest/data/' + token.hex().encode()
        assert self.stat(path)[1] == size
        self.opened(path)
        raw = self.read(path, size)
        magic, version, head, total, count, crc, skipped = struct.unpack_from('<4sHHIIII', raw)
        assert (magic, version, head, total, skipped) == (b'HMT1', 1, 24, len(raw), 0)
        assert zlib.crc32(raw[24:]) == crc
        entries, at = {}, 24
        for _ in range(count):
            kind, flags, length, size, mtime, crc = struct.unpack_from('<BBHIII', raw, at)
            path = raw[at + 16:at + 16 + length]
            assert kind == 1 and flags == 0 and path not in entries and len(path) == length
            entries[path] = (kind, size, mtime, crc)
            at += 16 + length
        assert at == len(raw)
        self.raw = raw
        return entries

    def legacy(self):
        entries, cookie = {}, 0
        while True:
            page = self.call(sfs.T_LIST, b'/', struct.pack('<I', cookie))
            cookie, at = struct.unpack_from('<I', page)[0], 4
            while at < len(page):
                kind, size, length = struct.unpack_from('<BIB', page, at)
                name = page[at + 6:at + 6 + length]
                entries[name] = (kind, size, 0, 0)
                at += 6 + length
            if not cookie:
                return entries

    def sync(self, dest, use_inventory=True, single=None):
        self.stat(b'/' if single is None else b'/' + single)
        entries = self.inventory() if use_inventory and single is None else None
        active = entries is not None
        if entries is None:
            entries = {single: None} if single else self.legacy()
        copied = 0
        for name, meta in entries.items():
            path = b'/' + name
            if not active:
                meta = (*self.stat(path), 0)
            _, size, mtime, crc = meta
            old, old_time = dest[name]
            if len(old) == size and old_time == mtime:
                continue  # same_mtime, before CRC
            if active:
                equal = zlib.crc32(old) == crc
            else:
                self.opened(path)
                equal = old == self.read(path, size)
            if not equal:
                self.opened(path)
                body = self.read(path, size)
                assert not active or zlib.crc32(body) == crc
                self.stat(path)  # post-copy source confirmation
                copied += 1
                dest[name] = (body, mtime)
            else:
                dest[name] = (old, mtime)
        return copied


def fixture(root):
    result = {}
    for i in range(60):
        name = ('f%02d' % i).encode()
        data = bytes([i]) * SIZES[i % len(SIZES)]
        path = root / name.decode()
        path.write_bytes(data)
        os.utime(path, (1700000000, 1700000000))
        result[name] = (data, 1700000000)
    return result


def counts(route):
    return tuple(route.counts[op] for op in (sfs.T_STAT, sfs.T_LIST, sfs.T_READ))


def suite():
    inventories = []
    with tempfile.TemporaryDirectory(prefix='hsync-flow-') as td:
        root = Path(td)
        source = fixture(root)
        with ExitStack() as cleanup:
            def route(kind):
                r = Route(kind, root)
                cleanup.callback(r.fs.close)
                return r
            for kind in ('serial', 'lan', 'link'):
                for scene in ('unchanged', 'partial', 'all-mtime'):
                    dest = dict(source)
                    changed = 2 if scene == 'partial' else 0
                    for name in (list(dest)[:2] if changed else list(dest) if scene == 'all-mtime' else []):
                        data, stamp = dest[name]
                        dest[name] = (b'x' * len(data) if changed else data, stamp - 1)
                    r = route(kind)
                    assert r.sync(dest) == changed and dest == source, scene
                    assert struct.unpack_from('<I', r.raw, 12)[0] == len(source)
                    # §5: V=4+2P, plus explicit data STAT and root STAT.
                    m = (len(r.raw) + r.cap - 5) // (r.cap - 4)
                    k = sum((SIZES[i] + r.cap - 5) // (r.cap - 4) for i in range(changed))
                    assert counts(r) == (6 + 2*r.busy + 3*changed, 0, 1+r.busy+m+k), (scene, counts(r))
                    print(kind, scene, 'STAT/LIST/READ=', counts(r), 'BUSY=', r.busy, '503=', r.unavail)
                    inventories.append(r.raw)
                # Independent legacy control and fallback must have identical results
                # and identical operation counts after the one failed new probe.
                for mode in ('old', 'full', 'single'):
                    dest = {n: (b'x'*len(d), t-1) for n, (d, t) in source.items()}
                    expected, actual = dict(dest), dict(dest)
                    control, r = route(kind), route(kind)
                    one = b'f00' if mode == 'single' else None
                    control.sync(expected, use_inventory=False, single=one)
                    with ExitStack() as stack:
                        if mode == 'old':
                            stack.enter_context(patch.object(r.fs.manifest, '_route', return_value=None))
                        if mode == 'full':
                            stack.enter_context(patch.object(tree, 'MAX_ENTRIES', 0))
                        r.sync(actual, single=one)
                    assert actual == expected, mode
                    extra = (0, 0, 0) if one else (1, 0, 0) if mode == 'old' else (2, 0, 1)
                    assert counts(r) == tuple(a+b for a, b in zip(counts(control), extra)), mode
                    assert counts(control)[1] > 0 or one
                    print(kind, mode, 'STAT/LIST/READ=', counts(r), 'legacy=', counts(control), 'BUSY=', r.busy, '503=', r.unavail)
                # Deterministic budget yields exercise actual resumable generation.
                r = route(kind)
                clock = iter(range(0, 10**15, 100000000))
                r.fs.manifest.clock = lambda: next(clock)
                assert r.inventory() and r.busy > 0 and r.unavail == 0
                print(kind, 'budget BUSY=', r.busy, '503=', r.unavail)
                # Change a file after enumeration/open, during the first CRC read.
                r = route(kind)
                original = tree.os.read
                changed = False
                def changing(fd, size):
                    nonlocal changed
                    block = original(fd, size)
                    if not changed:
                        changed = True
                        p = root / 'f00'
                        p.write_bytes(b'z' * len(source[b'f00'][0]))
                    return block
                with patch.object(tree.os, 'read', side_effect=changing):
                    try:
                        r.inventory()
                    except Failure as exc:
                        assert exc.code == sfs.ERR_STALE
                    else:
                        raise AssertionError('STALE accepted')
                fixture(root)
                print(kind, 'STALE rejected; BUSY=', r.busy, '503=', r.unavail)
            assert all(raw == inventories[0] for raw in inventories), 'adapter inventory differs'
            r = route('link')
            # Slow backend, not BUSY: the post-call check must produce 503.
            backend = r.agent._sfs_backend()
            original = backend.call
            def slow(*args):
                body = original(*args)
                r.now += 3.01
                return body
            with patch.object(backend, 'call', side_effect=slow):
                try:
                    r.stat(b'/f00')
                except Failure as exc:
                    assert exc.code == 503
                else:
                    raise AssertionError('deadline accepted')
            assert r.busy == 0 and r.unavail == 1
            print('link slow CALL BUSY=0 503=1 (3.0s deadline)')


def mutations():
    # Private copies only. Each mutant must fail at the named runtime assertion.
    cases = [
        ('count-unchanged', __file__, 'continue  # same_mtime, before CRC',
         'self.stat(path); continue  # same_mtime, before CRC', 'unchanged'),
        ('count-partial', __file__, 'copied += 1', 'self.stat(path); copied += 1', 'partial'),
        ('count-mtime', __file__, 'dest[name] = (old, mtime)',
         'self.read(path, size)\n                dest[name] = (old, mtime)', 'all-mtime'),
        ('fallback-old', __file__, 'return None  # old host fallback', 'raise', 'Failure'),
        ('fallback-full', __file__, 'return None  # descriptor FULL fallback', 'raise Failure(error)', 'Failure'),
        ('single', __file__, 'use_inventory and single is None', 'use_inventory', 'single'),
        ('stale', tree.__file__, 'if _identity(before) != _identity(after) or length != before.st_size:',
         'if False:', 'STALE accepted'),
        ('deadline', agent.__file__, 'if self.now() >= ent["sfs_deadline"]:',
         'if False:', 'deadline accepted'),
    ]
    for name, filename, old, new, marker in cases:
        source = Path(filename).read_text()
        if filename == __file__:
            prefix, suffix = source.split('\ndef mutations():', 1)
            assert prefix.count(old) == 1, (name, 'anchor')
            mutated = prefix.replace(old, new) + '\ndef mutations():' + suffix
        else:
            assert source.count(old) == 1, (name, 'anchor')
            mutated = source.replace(old, new)
        with tempfile.TemporaryDirectory(prefix='hsync-flow-mutant-') as td:
            path = Path(td, Path(filename).name)
            path.write_text(mutated)
            result = subprocess.run([sys.executable, '-B', __file__, '--variant', str(path)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode > 0 and marker in result.stderr, (name, result.stdout, result.stderr)
            suite()  # Restored normal control after every mutation.
            print('RED', name, 'runtime; restored GREEN', flush=True)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    if '--variant' in sys.argv:
        path = Path(sys.argv[-1])
        if path.name == Path(__file__).name:
            # Execute the variant's suite, not the control's globals.
            namespace = dict(globals(), __name__='variant')
            exec(compile(path.read_text(), str(path), 'exec'), namespace)
            namespace['suite']()
        else:
            name = path.stem
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module
            spec.loader.exec_module(module)
            if name == 'host_tree_manifest':
                tree = module
                sfs.TreeManifest = module.TreeManifest
                sfs.ManifestError = module.ManifestError
            else:
                agent = module
            suite()
    else:
        suite()
        print('CONTROL GREEN', flush=True)
        if '--mutate' in sys.argv:
            mutations()
