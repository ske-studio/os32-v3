#!/usr/bin/env python3
"""LAN_OPERATOR_DESIGN:128-133,218-219,224,251: P4a SFS on link.

Direct calls model fresh link rids around the same sid/seq; no socket used.
"""
CHECK = dict(name='host-sfs-link', tier=1, needs=[], budget=5,
    covers=['tools/host_sfs_link.py', 'tools/tests/test_host_sfs_link.py',
    'tools/lanfs_host.py', 'tools/serialfs_host.py', 'tools/host_tree_manifest.py', 'tools/lan_bridge.py', 'tools/host_image.py'],
    basis=['LAN_OPERATOR_DESIGN §5', 'LAN_OPERATOR_DESIGN §7'])

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import host_sfs_link as host
import serialfs_host as sfs

PEER = b'peer!!'


def exercise(module):
    with tempfile.TemporaryDirectory() as tmp:
        backend = module.SfsLink(tmp, allow_write=['allowed'], peer=PEER)
        def call(kind, sid, seq, payload=b''):
            return backend.call(sfs.encode(kind, sid, seq, payload, 1028), PEER)
        def decode(raw):
            result = sfs.try_frame(raw, 0, 1028)
            assert result[0] == 'frame' and result[1] == len(raw)
            return result[2]
        hello = sfs.encode(sfs.T_HELLO, 0, 0, struct.pack('<HHI', 1, 1028, 42))
        first = backend.call(hello, PEER)
        sid = decode(first).sid
        path = b'/allowed'
        payload = struct.pack('<IBB', 0, sfs.WF_TRUNC, len(path)) + path + b'written'
        wire = sfs.encode(sfs.T_WRITE, sid, 1, payload, 1028)
        with patch.object(backend.fs, 'write', wraps=backend.fs.write) as write:
            reply = backend.call(wire, PEER)
            assert struct.unpack('<i', decode(reply).payload)[0] == 7
            assert backend.call(hello, PEER) == first
            assert backend.call(wire, PEER) == reply
            assert write.call_count == 1, 'duplicate write'
        assert Path(tmp, 'allowed').read_bytes() == b'written'
        read = decode(call(sfs.T_READ, sid, 2, struct.pack('<IHB', 0, 1024, len(path)) + path))
        assert read.payload == struct.pack('<i', 7) + b'written'
        Path(tmp, 'allowed').write_bytes(b'x' * 1024)
        maximum = call(sfs.T_READ, sid, 3, struct.pack('<IHB', 0, 1024, len(path)) + path)
        assert len(maximum) == module.MAX_FRAME == 1044
        # A maximum-sized request also passes the codec (unknown op -> INVAL).
        assert call(0x3f, sid, 4, b'x' * 1028)
        denied = struct.pack('<IBB', 0, 1, 7) + b'/denied' + b'bad'
        assert struct.unpack('<i', decode(call(sfs.T_WRITE, sid, 5, denied)).payload)[0] == sfs.ERR_ROFS
        ro = module.SfsLink(tmp)
        ro_sid = decode(ro.call(hello, PEER)).sid
        assert struct.unpack('<i', decode(ro.call(sfs.encode(sfs.T_WRITE, ro_sid, 1, payload), PEER)).payload)[0] == sfs.ERR_ROFS
        fresh = module.SfsLink(tmp, peer=PEER)
        stale = decode(fresh.call(wire, PEER))
        assert stale.type == sfs.T_ERR and struct.unpack('<i', stale.payload)[0] == sfs.ERR_STALE
        before = backend.server.executed
        corrupt = bytearray(wire)
        corrupt[-1] ^= 1
        for bad in (b'', b'bad' + wire[3:], wire[:-1], wire + b'x', bytes(corrupt),
                    sfs.encode(0x3f, sid, 6, b'x' * 1029, 1029)):
            try:
                backend.call(bad, PEER)
            except ValueError:
                pass
            else:
                raise AssertionError('invalid frame accepted')
        try:
            backend.call(wire, b'other!')
        except ValueError:
            pass
        else:
            raise AssertionError('wrong peer accepted')
        assert backend.server.executed == before
        assert call(sfs.T_BYE, sid, 6) == b''
        assert call(sfs.T_BYE, sid, 6) == b''
        assert backend.server.sid == 0
        image_sid = decode(backend.call(hello, PEER)).sid
        missing = decode(call(0x30, image_sid, 1, b'missing'))
        assert missing.type == 0x30 | sfs.T_RESP
        assert struct.unpack('<i', missing.payload)[0] == sfs.ERR_NOTFOUND, 'BOPEN differs from raw LAN'

        print('baseline PASS')


def main():
    exercise(host)
    if '--mutate' in sys.argv:
        source = Path(host.__file__).read_text()
        anchor = '        response = self.adapter.handle(packet)'
        assert source.count(anchor) == 1
        # The designed mutant: new link calls clear the existing replay state.
        mutations = [
            (anchor, '        self.adapter.high_seq = 0\n        self.server.cache_key = None\n' + anchor,
             'duplicate write'),
            ('image=ImageStore({})', 'image=None', 'BOPEN differs from raw LAN')]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, 'mutant.py')
            for old, new, marker in mutations:
                assert source.count(old) == 1
                path.write_text(source.replace(old, new))
                spec = importlib.util.spec_from_file_location('mutant', path)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                try:
                    exercise(module)
                except AssertionError as exc:
                    assert str(exc) == marker, str(exc)
                    print(marker, 'RED')
                else:
                    raise AssertionError('mutant survived')



def run(ctx):
    import subprocess
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
