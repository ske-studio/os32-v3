#!/usr/bin/env python3
"""B/F acceptance: HMD1 bytes, snapshot failures, lifecycle and runtime mutants."""
CHECK = dict(name='host-tree-manifest', tier=1, needs=[], budget=20,
    covers=['tools/host_tree_manifest.py', 'tools/tests/test_host_tree_manifest.py'],
    basis=['HSYNC_MANIFEST_DESIGN §§2,3,6,8; M1-M3'])

import importlib.util
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = ROOT / 'tools/host_tree_manifest.py'
NEW = b'/.os32-manifest/new'


class Clock:
    def __init__(self):
        self.now = 0

    def __call__(self):
        return self.now


class FS:
    """The six M3 helpers, with real fds; both opening branches are exercised."""
    def __init__(self, root, secure):
        self.root, self.secure = os.fsencode(root), secure
        self.opened = []

    def _comps(self, wire):
        assert wire.startswith(b'/')
        return [c for c in wire.split(b'/') if c]

    def path(self, comps):
        return os.path.join(self.root, *comps)

    def _stat_raw(self, comps):
        return os.stat(self.path(comps), follow_symlinks=not self.secure)

    @staticmethod
    def _kind(st):
        return ((2, 0) if stat.S_ISDIR(st.st_mode) else
                (1, st.st_size) if stat.S_ISREG(st.st_mode) else (3, 0))

    @staticmethod
    def wire_mtime(st):
        m = int(st.st_mtime)
        return m if 0 <= m <= 0xffffffff else 0

    def _entries(self, comps):
        return [(n, *self._kind(self._stat_raw(comps + [n])))
                for n in os.listdir(self.path(comps))]

    def open_read_fd(self, comps):
        if self.secure:
            parent = os.open(self.path(comps[:-1]), os.O_RDONLY | os.O_DIRECTORY)
            try:
                fd = os.open(comps[-1], os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent)
            finally:
                os.close(parent)
        else:
            fd = os.open(self.path(comps), os.O_RDONLY)
        self.opened.append(fd)
        return fd


def load(path):
    spec = importlib.util.spec_from_file_location('inventory_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def descriptor(raw):
    magic, version, state, token, size, error = struct.unpack('<4sHH16sIi', raw)
    assert magic == b'HMD1' and version == 1
    return state, token.hex().encode(), size, error


def start(m, prefix=b''):
    return descriptor(m.read(prefix + NEW, 0, 32)[1])


def ready(m, desc, prefix=b''):
    for _ in range(100):
        if desc[0] != 0:
            return desc
        desc = descriptor(m.read(prefix + b'/.os32-manifest/status/' + desc[1], 0, 32)[1])
    raise AssertionError('BUSY never finished')


def data(m, desc, prefix=b''):
    assert desc[0] == 1 and desc[3] == 0, 'generation not READY'
    path = prefix + b'/.os32-manifest/data/' + desc[1]
    assert m.stat(path) == (0, struct.pack('<BII', 1, desc[2], 0))
    raw = m.read(path, 0, desc[2])[1]
    assert len(raw) == desc[2]
    for width in (507, 508, 509, 1023, 1024, 1025, 64512):
        chunks = [m.read(path, pos, width)[1] for pos in range(0, len(raw), width)]
        assert b''.join(chunks) == raw, 'offset/retransmission'
        assert m.read(path, 0, width)[1] == raw[:width]
    assert m.read(path, len(raw), 1) == (0, b'')
    return raw


def records(raw):
    magic, version, head, size, count, crc, skipped = struct.unpack('<4sHHIIII', raw[:24])
    assert (magic, version, head, size) == (b'HMT1', 1, 24, len(raw))
    assert zlib.crc32(raw[24:]) == crc, 'records CRC'
    out, pos = [], 24
    for _ in range(count):
        kind, flags, length, size, mtime, crc = struct.unpack('<BBHIII', raw[pos:pos + 16])
        assert flags == 0
        path = raw[pos + 16:pos + 16 + length]
        out.append((path, kind, size, mtime, crc))
        pos += 16 + length
    assert pos == len(raw)
    return out, skipped


def fails(code, fn):
    try:
        fn()
    except Exception as exc:
        assert getattr(exc, 'code', None) == code, repr(exc)
    else:
        raise AssertionError('expected error ' + str(code))


def closed(fs):
    for fd in fs.opened:
        try:
            os.fstat(fd)
        except OSError:
            continue
        raise AssertionError('fd leaked')


def tree_test(mod):
    for secure in (False, True):
        with tempfile.TemporaryDirectory() as tmp:
            fs = FS(tmp, secure)
            os.mkdir(fs.path([b'd']))
            os.mkdir(fs.path([b'd', b'empty']))
            os.mkfifo(fs.path([b'other']))
            bodies = {b'a': b'123456789', b'd/z': b'x' * (65536 * 2 + 17),
                      b'empty': b'', b'\xff': b'bytes'}
            for name, body in bodies.items():
                with open(fs.path([name]), 'wb') as f:
                    f.write(body)
            os.mkdir(fs.path([b'bad\\dir']))
            Path(tmp, 'bad\\dir', 'hidden').write_bytes(b'excluded')
            m = mod.TreeManifest(fs, nonce=bytes.fromhex('ab01020304050607'))
            assert m.stat(NEW) == (0, struct.pack('<BII', 1, 32, 0)) and m.job is None
            desc = ready(m, start(m))
            assert desc[1] == b'ab010203040506070100000000000000', 'token hex'
            raw = data(m, desc)
            print('host metrics:', {k: m.job[k] for k in ('elapsed_ns', 'crc_bytes', 'files', 'busy_reads')})
            rows, skipped = records(raw)
            assert [r[0] for r in rows] == [b'a', b'd', b'd/empty', b'd/z', b'empty', b'other', b'\xff'], 'tree order/empty dir'
            assert skipped == 1, 'skipped names'
            for path, kind, size, mtime, crc in rows:
                st = fs._stat_raw(path.split(b'/'))
                assert (kind, size, mtime) == (*fs._kind(st), fs.wire_mtime(st)), 'metadata'
                assert crc == (zlib.crc32(bodies[path]) if kind == 1 else 0), 'file CRC'
            assert rows[0][4] == 0xcbf43926
            assert m.job['crc_bytes'] == sum(map(len, bodies.values())) and m.job['files'] == 4
            fs.sid = 7  # Transport reconnect must have no effect on the job.
            assert data(m, desc) == raw
            # Completed bytes remain immutable, even when the source changes.
            Path(tmp, 'a').write_bytes(b'changed')
            assert data(m, desc) == raw
            assert records(data(m, ready(m, start(m))))[0][0][4] == zlib.crc32(b'changed')
            sub = ready(m, start(m, b'/d'), b'/d')
            assert [r[0] for r in records(data(m, sub, b'/d'))[0]] == [b'empty', b'z']
            assert m.stat(b'/a') is None
            os.mkdir(fs.path([b'.os32-manifest']))
            assert m.stat(NEW) is None and m.read(NEW, 0, 32) is None
            m.close()
            closed(fs)


def lifecycle_test(mod):
    with tempfile.TemporaryDirectory() as tmp:
        fs, clock = FS(tmp, True), Clock()
        Path(tmp, 'big').write_bytes(b'x' * (65536 * 3))
        m = mod.TreeManifest(fs, clock=clock, nonce=b'abcdefgh')
        real_read = os.read
        lengths = []
        def slow_read(fd, count):
            lengths.append(count)
            clock.now += 600000000
            return real_read(fd, count)
        with patch.object(mod.os, 'read', slow_read):
            old = None
            retained_jobs = []
            baseline_fds = len(os.listdir('/proc/self/fd'))
            for _ in range(5):
                desc = start(m)
                assert desc[0] == 0 and desc[3] == 0, 'budget BUSY'
                assert m.job['busy_reads'] == 1, 'BUSY counter'
                if old:
                    fails(-2, lambda: m.stat(b'/.os32-manifest/status/' + old))
                old = desc[1]
                retained_jobs.append(m.job)
                assert len(os.listdir('/proc/self/fd')) == baseline_fds + 1, 'replacement fd leak'
            fails(-1, lambda: m.stat(b'/.os32-manifest/data/' + desc[1]))
            fails(-1, lambda: m.read(b'/.os32-manifest/data/' + desc[1], 0, 32))
            desc = ready(m, desc)
        assert max(lengths) <= 65536, 'read unit'
        assert desc[0] == 1 and m.job['busy_reads'] >= 3
        print('metrics:', {k: m.job[k] for k in ('elapsed_ns', 'crc_bytes', 'files', 'busy_reads')})
        clock.now += mod.TTL_NS - 1
        assert m.stat(b'/.os32-manifest/status/' + desc[1])[0] == 0
        clock.now += mod.TTL_NS
        fails(-2, lambda: m.stat(b'/.os32-manifest/status/' + desc[1]))
        assert m.job is None, 'expiry cleanup'
        desc = start(m)
        m.close()
        fails(-2, lambda: m.read(b'/.os32-manifest/status/' + desc[1], 0, 32))
        closed(fs)
        restarted = mod.TreeManifest(fs, nonce=b'newnonce')
        assert start(restarted)[1] != desc[1]
        restarted.close()


def stale_test(mod):
    for mode in ('opened', 'during', 'directory', 'subtree', 'io'):
        with tempfile.TemporaryDirectory() as tmp:
            fs = FS(tmp, True)
            target = Path(tmp, 'a')
            target.write_bytes(b'123456789')
            m = mod.TreeManifest(fs)
            original_open, original_read, original_entries = fs.open_read_fd, os.read, fs._entries
            original_stat = fs._stat_raw
            root_before = original_stat([])
            after_listing = False
            def observed_stat(comps):
                nonlocal after_listing
                if comps:
                    after_listing = False
                    return original_stat(comps)
                if mode == 'opened' or (mode == 'directory' and not after_listing):
                    return root_before
                return original_stat(comps)
            def changed_open(comps):
                replacement = Path(tmp, 'replacement')
                replacement.write_bytes(b'123456789')
                os.replace(replacement, target)
                return original_open(comps)
            def changed_read(fd, count):
                result = original_read(fd, count)
                if result and mode == 'subtree':
                    Path(tmp, 'newchild').write_bytes(b'')
                if result and mode == 'during':
                    target.write_bytes(b'987654321')
                    st = target.stat()
                    os.utime(target, ns=(st.st_atime_ns, st.st_mtime_ns + 1))
                return result
            def changed_entries(comps):
                nonlocal after_listing
                result = original_entries(comps)
                Path(tmp, 'newchild').write_bytes(b'')
                after_listing = True
                return result
            with patch.object(fs, '_stat_raw', observed_stat), \
                 patch.object(fs, 'open_read_fd', changed_open if mode == 'opened' else original_open), \
                 patch.object(mod.os, 'read', changed_read if mode in ('during', 'subtree') else original_read), \
                 patch.object(fs, '_entries', changed_entries if mode == 'directory' else original_entries):
                if mode == 'io':
                    with patch.object(fs, 'open_read_fd', side_effect=OSError('injected')):
                        desc = ready(m, start(m))
                else:
                    desc = ready(m, start(m))
            assert desc[0] == 2 and desc[2] == 0 and desc[3] == (-1 if mode == 'io' else -11), 'snapshot ' + mode
            closed(fs)
            m.close()


class SyntheticFS(FS):
    """Large/invalid entry fixtures without creating thousands of disk files."""
    def __init__(self, names, timestamp=0, size=0):
        self.names, self.timestamp, self.size = names, timestamp, size

    def _stat_raw(self, comps):
        if b'.os32-manifest' in comps:
            raise FileNotFoundError()
        return SimpleNamespace(st_dev=1, st_ino=1, st_mode=stat.S_IFDIR if not comps else stat.S_IFIFO,
                               st_size=self.size, st_mtime=self.timestamp,
                               st_mtime_ns=-1, st_ctime_ns=0)

    def _entries(self, comps):
        return [(name, 3, 0) for name in self.names]


def bounds_test(mod):
    for count, width, error in ((8192, 5, 0), (8193, 5, -13), (6000, 200, -13)):
        fs = SyntheticFS([str(i).encode().zfill(width) for i in range(count)])
        m = mod.TreeManifest(fs)
        desc = ready(m, start(m))
        assert desc[3] == error, 'manifest bounds'
        if not error:
            assert len(records(data(m, desc))[0]) == count
        else:
            assert desc[0] == 2 and desc[2] == 0
        m.close()
    fs = SyntheticFS([b'a' * 256, b'bad\\name', b'ok'])
    m = mod.TreeManifest(fs)
    assert records(data(m, ready(m, start(m))))[1] == 2, 'skipped long name'
    for timestamp in (-0.25, -1, 0, 0.999999999, 1.999999999, 0xffffffff, 0x100000000):
        fs = SyntheticFS([b'other'], timestamp)
        m = mod.TreeManifest(fs)
        row = records(data(m, ready(m, start(m))))[0][0]
        expected = int(timestamp)
        expected = expected if 0 <= expected <= 0xffffffff else 0
        assert row[3] == expected, 'wire mtime'
    # HostFS's float->int contract, including values rounding across a second,
    # applies identically to files, directories and other objects in both modes.
    for secure in (False, True):
        with tempfile.TemporaryDirectory() as tmp:
            fs = FS(tmp, secure)
            Path(tmp, 'file').write_bytes(b'')
            Path(tmp, 'dir').mkdir()
            os.mkfifo(fs.path([b'fifo']))
            raw_stat = fs._stat_raw
            for timestamp in (-0.25, -1, 1.999999999, 2.0, 0xffffffff, 0x100000000):
                def metadata(comps):
                    st = raw_stat(comps)
                    fields = {name: getattr(st, name) for name in (
                        'st_dev', 'st_ino', 'st_mode', 'st_size', 'st_mtime_ns', 'st_ctime_ns')}
                    return SimpleNamespace(**fields, st_mtime=timestamp)
                with patch.object(fs, '_stat_raw', metadata):
                    m = mod.TreeManifest(fs)
                    rows = records(data(m, ready(m, start(m))))[0]
                    for path, kind, size, mtime, crc in rows:
                        st = fs._stat_raw([path])
                        assert (kind, size, mtime) == (*fs._kind(st), fs.wire_mtime(st)), 'all-kind wire mtime'
                    m.close()
            closed(fs)
    with tempfile.TemporaryDirectory() as tmp:
        fs = FS(tmp, True)
        with open(Path(tmp, 'huge'), 'wb') as f:
            f.truncate(0x80000000)
        m = mod.TreeManifest(fs)
        assert ready(m, start(m))[3] == -1, 'file size bound'
        m.close()


CASES = dict(tree=tree_test, lifecycle=lifecycle_test, stale=stale_test, bounds=bounds_test)
MUTANTS = [
    ('return crc & 0xffffffff', 'return (crc ^ 1) & 0xffffffff', 'tree'),
    ('                crc = 0\n', '                if kind == 2 and not self.fs._entries(child):\n                    continue\n                crc = 0\n', 'tree'),
    ("job['skipped_names'] += 1", "job['skipped_names'] += 0", 'tree'),
    ('if _identity(original) != _identity(before):', 'if False: # opened identity', 'stale'),
    ('if _identity(before) != _identity(after) or length != before.st_size:', 'if False: # read identity', 'stale'),
    ('if _identity(before) != _identity(self.fs._stat_raw(comps)):  # enumeration', 'if False: # enumeration', 'stale'),
    ('if _identity(before) != _identity(self.fs._stat_raw(comps)):  # subtree', 'if False: # subtree', 'stale'),
    ('self.close()  # new replaces', 'pass  # new replaces', 'lifecycle'),
    ("now - job['access'] >= TTL_NS", "now - job['access'] > TTL_NS", 'lifecycle'),
    ('self.clock() < deadline', 'True', 'lifecycle'),
    ("job['busy_reads'] += 1", "job['busy_reads'] += 0", 'lifecycle'),
    ('os.read(fd, CHUNK)', 'os.read(fd, CHUNK * 2)', 'lifecycle'),
    ('            os.close(fd)', '            pass  # leaked fd', 'tree'),
    ("len(name) > 255 or b'\\\\' in name", "b'\\\\' in name", 'bounds'),
    ('count >= MAX_ENTRIES', 'count > MAX_ENTRIES', 'bounds'),
    ('> MAX_BYTES:', '> MAX_BYTES * 2:', 'bounds'),
    ('self.fs.wire_mtime(st), crc)', 'max(0, st.st_mtime_ns // 10**9), crc)', 'bounds'),
    ("job['data'][offset:offset + count]", "job['data'][offset + 1:offset + count]", 'tree'),
]


def main():
    if '--case' in sys.argv:
        case = sys.argv[sys.argv.index('--case') + 1]
        module = sys.argv[sys.argv.index('--module') + 1]
        CASES[case](load(module))
        return
    module = load(PRODUCT)
    for name, case in CASES.items():
        case(module)
        print(name, 'GREEN')
    if '--mutate' not in sys.argv:
        return
    source = PRODUCT.read_text()
    with tempfile.TemporaryDirectory() as tmp:
        mutant = Path(tmp, 'mutant.py')
        for index, (old, new, case) in enumerate(MUTANTS, 1):
            assert source.count(old) == 1, old
            mutant.write_text(source.replace(old, new))
            cmd = [sys.executable, '-B', __file__, '--case', case, '--module']
            subprocess.run(cmd + [str(PRODUCT)], check=True, capture_output=True)
            result = subprocess.run(cmd + [str(mutant)], capture_output=True, text=True)
            assert result.returncode != 0 and 'AssertionError' in result.stderr, result.stderr
            assert 'SyntaxError' not in result.stderr and 'ImportError' not in result.stderr
            print('mutation', index, case, 'runtime RED')
            subprocess.run(cmd + [str(PRODUCT)], check=True, capture_output=True)
    print(len(MUTANTS), 'runtime RED; restored controls GREEN')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
