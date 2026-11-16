#!/usr/bin/env python3
"""HostFS integration for the HMD1 virtual files and M3 helpers."""
CHECK = dict(name='hostfs-manifest', tier=1, needs=[], budget=20,
    covers=['tools/serialfs_host.py', 'tools/host_tree_manifest.py',
            'tools/tests/test_hostfs_manifest.py'],
    basis=['HSYNC_MANIFEST_DESIGN §§3,7,8; M3'])

import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
PRODUCT = ROOT / 'tools/serialfs_host.py'


def load(path):
    spec = importlib.util.spec_from_file_location('hostfs_under_test', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def check(mod):
    for secure in (False, True):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'file').write_bytes(b'123456789')
            (root / 'dir').mkdir()
            os.mkfifo(root / 'other')
            os.utime(root / 'file', ns=(1999999999, 1999999999))
            os.utime(root / 'dir', ns=(4294967296000000000, 4294967296000000000))
            fs = mod.HostFS(tmp, secure=secure)
            new = b'/.os32-manifest/new'
            assert struct.unpack('<BII', fs.stat(new)[1]) == (1, 32, 0)
            n, desc = fs.read(new, 0, 32)
            assert n == len(desc) == 32
            magic, version, state, token, size, error = struct.unpack('<4sHH16sIi', desc)
            assert (magic, version, state, error) == (b'HMD1', 1, 1, 0)
            path = b'/.os32-manifest/data/' + token.hex().encode()
            assert struct.unpack('<BII', fs.stat(path)[1]) == (1, size, 0)
            n, data = fs.read(path, 0, 4096, max_payload=4096)
            assert n == len(data) == size
            pos = 24
            found = set()
            while pos < len(data):
                kind, flags, length, length_file, mtime, crc = struct.unpack_from('<BBHIII', data, pos)
                pos += 16
                name = data[pos:pos + length]
                pos += length
                assert (kind, length_file, mtime) == struct.unpack('<BII', fs.stat(b'/' + name)[1])
                found.add(name)
            assert found == {b'file', b'dir', b'other'}
            assert fs.read(b'/file', 2, 3) == (3, b'345')
            # Linux lacks O_BINARY: inject a flag and strip it before the OS call.
            real_open = os.open
            binary = 0x40000000
            def binary_open(path, flags, **kwargs):
                if path == b'file' or os.fsencode(path).endswith(b'/file'):
                    assert flags & binary, 'open_read_fd must request O_BINARY'
                return real_open(path, flags & ~binary, **kwargs)
            with patch.object(mod.os, 'O_BINARY', binary, create=True), \
                 patch.object(mod.os, 'open', binary_open):
                assert fs.read(b'/file', 0, 9) == (9, b'123456789')

            for name in (b'/', b'/dir'):
                try:
                    fs.open_read_fd(fs._comps(name))
                except mod.SfsError as exc:
                    assert exc.code == mod.ERR_ISDIR
                else:
                    raise AssertionError('directory opened')
            try:
                fs.stat(b'/.os32-manifest/data/' + b'0' * 32)
            except mod.SfsError as exc:
                assert exc.code == mod.ERR_NOTFOUND
            else:
                raise AssertionError('invalid token accepted')
            fs.close()
            assert fs.manifest.job is None
    assert mod.HostFS.wire_mtime(SimpleNamespace(st_mtime=2.0, st_mtime_ns=1999999999)) == 2
    assert mod.HostFS.wire_mtime(SimpleNamespace(st_mtime=-1.5)) == 0
    assert mod.HostFS.wire_mtime(SimpleNamespace(st_mtime=4294967296.0)) == 0


def main():
    if '--case' in sys.argv:
        try:
            check(load(sys.argv[sys.argv.index('--case') + 1]))
        except OSError as exc:
            raise AssertionError('unexpected filesystem error') from exc
        return
    check(load(PRODUCT))
    print('integration GREEN')
    if '--mutate' in sys.argv:
        source = PRODUCT.read_text()
        mutants = [
            ('os.O_RDONLY | os.O_NOFOLLOW | getattr(os, "O_BINARY", 0), dir_fd=pfd)',
             'os.O_RDONLY | os.O_NOFOLLOW, dir_fd=pfd)'),
            ('os.O_RDONLY | getattr(os, "O_BINARY", 0))', 'os.O_RDONLY)'),
            ('m = int(st.st_mtime)', 'm = st.st_mtime_ns // 10**9'),
            ('virtual = self.manifest.stat(wire)', 'virtual = None'),
            ('virtual = self.manifest.read(wire, offset, count)', 'virtual = None'),
            ('return len(virtual[1]), virtual[1]', 'return 0, virtual[1]'),
            ('self.manifest.close()', 'pass  # no close'),
            ('raise SfsError(exc.code) from exc', 'raise SfsError(ERR_IO) from exc'),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, 'mutant.py')
            for i, (old, new) in enumerate(mutants, 1):
                # Exception translation occurs in both entry points; mutate stat only.
                assert source.count(old) == (2 if old.startswith('raise SfsError(exc.code)') else 1)
                path.write_text(source.replace(old, new, 1))
                cmd = [sys.executable, '-B', __file__, '--case']
                subprocess.run(cmd + [str(PRODUCT)], check=True, capture_output=True)
                result = subprocess.run(cmd + [str(path)], capture_output=True, text=True)
                assert result.returncode and 'AssertionError' in result.stderr, result.stderr
                subprocess.run(cmd + [str(PRODUCT)], check=True, capture_output=True)
                print('mutation', i, 'runtime RED')
        print(len(mutants), 'runtime RED; restored controls GREEN')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
