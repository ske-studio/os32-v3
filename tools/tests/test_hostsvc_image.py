#!/usr/bin/env python3
"""H2: real temporary images, D1 wire layout, FD lifetime and runtime mutants."""
CHECK = dict(name='hostsvc-image', tier=1, needs=[], budget=2,
             covers=['tools/host_image.py', 'tools/hostsvc_contracts.pyi',
                     'fs/serialfs/sfs_proto.h', 'sdk/include/os32/os32_kapi_shared.h',
                     'tools/tests/hostsvc_fixture.py'])
import argparse
import errno
import importlib.util
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch

from hostsvc_fixture import Fixture

ROOT = Path(__file__).resolve().parents[2]


def load(path):
    spec = importlib.util.spec_from_file_location('host_image', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def constants(path):
    definitions = dict(re.findall(r'^#define\s+(\w+)\s+([^\n/]+)', path.read_text(), re.M))
    def value(name):
        expression = definitions[name].strip()
        expression = re.sub(r'\b[A-Z][A-Z_0-9]*\b',
                            lambda match: str(value(match[0])), expression)
        return eval(expression, {'__builtins__': {}}, {})
    return value


def test(path):
    m = load(path)
    wire = constants(ROOT / 'fs/serialfs/sfs_proto.h')
    errors = constants(ROOT / 'sdk/include/os32/os32_kapi_shared.h')
    for name in vars(m):
        if name.startswith('SFS_'):
            assert getattr(m, name) == wire(name), name
        if name.startswith('ERR_'):
            assert getattr(m, name) == errors('OS32_' + name), name
    for name, offset in [('STATUS', 0), ('HANDLE', 4), ('SIZE_LO', 8),
                         ('SIZE_HI', 12), ('MTIME_LO', 16), ('MTIME_HI', 20)]:
        assert wire('SFS_BOPEN_' + name + '_OFF') == offset
    for name, offset in [('HANDLE', 0), ('LBA', 4), ('COUNT', 8)]:
        assert wire('SFS_BREAD_' + name + '_OFF') == offset
    assert wire('SFS_BCLOSE_HANDLE_OFF') == 0
    assert wire('SFS_BREAD_DATA_OFF') == struct.calcsize('<i')

    def status(response, expected):
        assert response == struct.pack('<i', expected), (response, expected)

    with tempfile.TemporaryDirectory() as directory:
        fixture = Fixture('PY_IMAGE')
        image = Path(directory) / 'install.iso'
        # Distinct sectors expose wrong offsets and shared seek positions.
        sectors = [bytes([i]) * 512 for i in range(4)]
        image.write_bytes(b''.join(sectors))
        timestamp = 1700000000123456789
        os.utime(image, ns=(timestamp, timestamp))
        store = m.ImageStore({'install': str(image), '日本語': str(image),
                              'a' * 63: str(image), 'missing': str(image) + '.missing'})
        def opened(sid=7, alias=b'install'):
            response = store.dispatch(sid, m.SFS_T_BOPEN, alias)
            assert len(response) == wire('SFS_BOPEN_RSP_LEN')
            code, handle, size, mtime = struct.unpack('<iIQQ', response)
            assert code == 0 and handle != 0
            assert (size, mtime) == (image.stat().st_size, image.stat().st_mtime_ns)
            return handle, (size, mtime)

        def read(handle, lba, count=1, sid=7):
            return store.dispatch(sid, m.SFS_T_BREAD, struct.pack('<IIH', handle, lba, count))

        status(store.dispatch(7, m.SFS_T_BOPEN, b'install'), m.ERR_STALE)
        store.open_session(7)
        handle, identity = opened()
        assert identity == (2048, timestamp)
        assert read(handle, 0, 2) == struct.pack('<i', 0) + b''.join(sectors[:2])
        assert read(handle, 2, 2) == struct.pack('<i', 0) + b''.join(sectors[2:])
        assert read(handle, 1) == struct.pack('<i', 0) + sectors[1]
        # Maximum successful reply and exact end-of-image boundary.
        assert len(read(handle, 2, 2)) == wire('SFS_BREAD_RSP_MAX')
        with patch.object(m.os, 'pread', wraps=os.pread) as pread:
            for lba, count in [(4, 1), (3, 2), (0xffffffff, 1), (0xffffffff, 2)]:
                status(read(handle, lba, count), m.ERR_INVAL)
            for count in [0, 3, 65535]:
                status(read(handle, 0, count), m.ERR_INVAL)
            assert pread.call_count == 0
        for alias in [b'', b'a' * 64, b'bad\0alias', b'\xff']:
            status(store.dispatch(7, m.SFS_T_BOPEN, alias), m.ERR_INVAL)
        for alias in [b'unknown', b'missing']:
            status(store.dispatch(7, m.SFS_T_BOPEN, alias), m.ERR_NOTFOUND)
        opened(alias='日本語'.encode())
        opened(alias=b'a' * 63)
        for op, size in [(m.SFS_T_BREAD, wire('SFS_BREAD_REQ_LEN')),
                         (m.SFS_T_BCLOSE, wire('SFS_BCLOSE_REQ_LEN'))]:
            for payload in [b'', b'\0' * (size - 1), b'\0' * (size + 1)]:
                status(store.dispatch(7, op, payload), m.ERR_INVAL)
        status(store.dispatch(7, 0x7f, b''), m.ERR_INVAL)
        store.open_session(8)
        status(read(handle, 0, sid=8), m.ERR_STALE)
        status(store.dispatch(8, m.SFS_T_BCLOSE, struct.pack('<I', handle)), m.ERR_STALE)
        assert read(handle, 0) == struct.pack('<i', 0) + sectors[0]
        with patch.object(m.os, 'pread', return_value=b'short'):
            status(read(handle, 0), m.ERR_IO)
        with patch.object(m.os, 'pread', side_effect=OSError(errno.EIO, 'injected')):
            status(read(handle, 0), m.ERR_IO)

        # A pathname replacement changes the next BOPEN identity. An already
        # open FD still addresses its original image, even after rename.
        replacement = Path(directory) / 'replacement.iso'
        replacement.write_bytes(b'R' * 2048)
        os.utime(replacement, ns=(timestamp + 1, timestamp + 1))
        fixture.fail('image_replace', replacement)
        fixture.failure_take('image_replace').replace(image)
        changed, new_identity = opened()
        assert new_identity != identity
        assert read(handle, 0) == struct.pack('<i', 0) + sectors[0]
        assert read(changed, 0) == struct.pack('<i', 0) + b'R' * 512
        # Same image across sessions preserves identity, independent of handle.
        other, same_identity = opened(sid=8)
        assert other != changed and same_identity == new_identity
        os.utime(image, ns=(timestamp + 2, timestamp + 2))
        status(read(changed, 0), m.ERR_STALE)
        current, _ = opened()
        real_pread = os.pread
        def change_during_read(fd, length, offset):
            data = real_pread(fd, length, offset)
            os.utime(image, ns=(timestamp + 3, timestamp + 3))
            return data
        with patch.object(m.os, 'pread', side_effect=change_during_read):
            status(read(current, 0), m.ERR_STALE)
        current, _ = opened()
        with image.open('r+b') as stream:
            stream.truncate(1024)
        # Size alone changes identity even when mtime is deliberately restored.
        os.utime(image, ns=(timestamp + 3, timestamp + 3))
        status(read(current, 0), m.ERR_STALE)
        smaller, _ = opened()
        status(store.dispatch(7, m.SFS_T_BCLOSE, struct.pack('<I', smaller)), 0)
        status(read(smaller, 0), m.ERR_STALE)
        status(store.dispatch(7, m.SFS_T_BCLOSE, struct.pack('<I', smaller)), m.ERR_STALE)
        reopened, _ = opened()
        assert reopened != smaller
        # Both replacing a session and BYE release every owned FD.
        for sid, close in [(7, store.open_session), (8, store.close_session)]:
            with patch.object(m.os, 'close', wraps=os.close) as closer:
                close(sid)
                assert closer.call_count > 0
                for call in closer.call_args_list:
                    try:
                        os.fstat(call.args[0])
                    except OSError as exc:
                        assert exc.errno == errno.EBADF
                    else:
                        raise AssertionError('session leaked an image FD')
        status(read(reopened, 0), m.ERR_STALE)
        status(read(other, 0, sid=8), m.ERR_STALE)
        store.close_session(8)
        # Reject a non-sector size and sizes beyond u32 total_sects, without
        # allocating disk blocks for the upper-bound check (sparse files).
        for size, expected in [(513, m.ERR_INVAL),
                               (m.U32_MAX * 512, 0), ((m.U32_MAX + 1) * 512, m.ERR_INVAL)]:
            with image.open('r+b') as stream:
                stream.truncate(size)
            response = store.dispatch(7, m.SFS_T_BOPEN, b'install')
            assert struct.unpack_from('<i', response)[0] == expected
            if expected:
                assert len(response) == wire('SFS_STATUS_LEN')
            else:
                large_handle, size_lo, size_hi = struct.unpack_from('<III', response, 4)
                assert size_lo + (size_hi << 32) == size
                assert read(large_handle, m.U32_MAX - 1) == struct.pack('<i', 0) + b'\0' * 512
        store.close_session(7)
        fixture.close()
    print('PASS host image: wire, bounds, identity, sessions, FD cleanup')


MUTATIONS = [
    ('fixed-identity', 'return st.st_size, st.st_mtime_ns', 'return st.st_size, 0'),
    ('unbounded-pread', 'if offset + length > identity[0]:', 'if False:'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', default=str(ROOT / 'tools/host_image.py'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        with tempfile.TemporaryDirectory() as directory:
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, name
                mutant = Path(directory) / 'host_image.py'
                mutant.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(mutant)],
                                        capture_output=True, text=True)
                if result.returncode == 0 or 'AssertionError' not in result.stderr:
                    raise RuntimeError(f'{name}: not runtime RED (rc={result.returncode}): {result.stderr}')
                print('RED', name)
    return 0


def run(ctx):
    argv = [sys.executable, '-B', __file__]
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
