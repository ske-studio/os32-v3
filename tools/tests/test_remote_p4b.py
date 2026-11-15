#!/usr/bin/env python3
"""LAN_OPERATOR_DESIGN:84,128-133,178,207-210: SFS on link wiring."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import serialfs_host as sfs

CHECK = dict(name='remote-p4b', tier=1, needs=[], budget=5, kind='contract',
    covers=['tools/host_agent_config.py', 'tools/host_image.py', 'tools/host_linux.py', 'tools/host_notify.py', 'tools/host_operator_socket.py', 'tools/host_rshell.py', 'tools/host_services.py', 'tools/lan_bridge.py', 'tools/host_agent.py', 'tools/host_sfs_link.py', 'tools/lanfs_host.py',
            'tools/serialfs_host.py', 'tools/tests/test_remote_p4b.py'],
    basis=['LAN_OPERATOR_DESIGN §2', 'LAN_OPERATOR_DESIGN §5',
           'LAN_OPERATOR_DESIGN §6'])
PEER = b'peer!!'


def test(path):
    spec = importlib.util.spec_from_file_location('agent_p4b', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    with tempfile.TemporaryDirectory() as tmp:
        agent = mod.HostAgent(b'host!!', file_root=tmp, allow_write=('/',),
                              peer=PEER, quiet=True)
        agent._switch_to(dict(sess=1, epoch=1, mac=PEER))
        clock = [0.0]
        agent.now = lambda: clock[0]
        sink = mod.ListSink()
        rid = 0
        def request(wire, decl=None):
            nonlocal rid
            if rid in agent.ledger:
                agent._on_release(rid, sink)
            rid += 1
            agent._on_request(rid, 0, ('SFS CALL %d' % (len(wire) if decl is None else decl)).encode(), sink)
            if wire:
                agent._on_wdata(rid, 1, wire, sink)
            return agent.ledger[rid]
        def decode(raw):
            parsed = sfs.try_frame(raw, 0, 1028)
            assert parsed[0] == 'frame'
            return parsed[2]
        hello = sfs.encode(sfs.T_HELLO, 0, 0, struct.pack('<HHI', 1, 1028, 42))
        first = request(hello)
        assert first['resp'][0] == 200
        sid = decode(first['body']).sid
        backend = agent.sfs_link
        payload = struct.pack('<IBB', 0, sfs.WF_TRUNC, 2) + b'/a' + b'written'
        wire = sfs.encode(sfs.T_WRITE, sid, 1, payload)
        with patch.object(backend.fs, 'write', wraps=backend.fs.write) as write:
            reply = request(wire)
            assert reply['resp'][0] == 200 and struct.unpack('<i', decode(reply['body']).payload)[0] == 7, 'rid lost replay cache'
            agent._on_release(rid, sink)
            assert request(wire)['body'] == reply['body'], 'rid lost replay cache'
            assert agent.sfs_link is backend and write.call_count == 1, 'rid lost replay cache'
        assert Path(tmp, 'a').read_bytes() == b'written'
        read = request(sfs.encode(sfs.T_READ, sid, 2, struct.pack('<IHB', 0, 1024, 2) + b'/a'))
        assert decode(read['body']).payload == struct.pack('<i', 7) + b'written'
        # Exercise the normal DATA delivery, not only the response ledger.
        sink.frames.clear()
        agent._on_window(rid, 0, 32, sink)
        data = [f[34:34 + mod.hdr_unpack(f[14:])[5]] for f in sink.frames if f[14] == mod.OP_DATA]
        assert b''.join(data) == read['body']
        assert request(sfs.encode(sfs.T_BYE, sid, 3))['resp'] == (200, 0)
        # Link sessions retain the generation's backend; a new generation replaces it.
        agent._switch_to(dict(sess=2, epoch=1, mac=PEER))
        assert request(hello)['resp'][0] == 200 and agent.sfs_link is backend
        agent.host_generation += 1
        assert request(hello)['resp'][0] == 200 and agent.sfs_link is not backend
        backend = agent.sfs_link
        sid = backend.server.sid
        with patch.object(backend, 'call', side_effect=ValueError('bad CRC')) as call:
            failed = request(wire)
            agent._on_wdata(rid, 1, wire, sink)
            agent._on_status(rid, sink)
            assert failed['resp'][0] == 400 and call.call_count == 1, 'ValueError retried'
        with patch.object(backend, 'call', wraps=backend.call) as call:
            assert request(hello + b'x', len(hello))['resp'][0] == 400, 'length guard removed'
            assert not call.called, 'length guard removed'
            short = request(hello[:-1], len(hello))
            assert short['resp'] is None
            clock[0] += 0.5
            agent._on_request(rid, 0, ('SFS CALL %d' % len(hello)).encode(), sink)
            clock[0] += 0.5
            agent.tick(sink)
            assert short['resp'][0] == 503 and not call.called, 'deadline removed'
            late = request(b'', len(hello))
            clock[0] += 1.0
            agent._on_wdata(rid, 1, hello, sink)
            assert late['resp'][0] == 503 and not call.called, 'deadline removed'
        def slow(frame, source):
            clock[0] += 1.0
            return b'late'
        with patch.object(backend, 'call', side_effect=slow):
            assert request(hello)['resp'][0] == 503, 'deadline removed'
        # A direct service invocation must also reject a short body.
        ent = dict(got=b'a', decl=2, sfs_deadline=clock[0] + 1)
        with patch.object(backend, 'call', return_value=b''):
            agent._service_sfs(rid, ent, sink)
        assert ent['resp'][0] == 400, 'length guard removed'
        for line in ('SFS CALL 0', 'SFS CALL 1045', 'SFS CALL -1', 'SFS CALL x',
                     'SFS CALL 16 extra', 'SFS OTHER 16'):
            agent._on_release(rid, sink)
            rid += 1
            agent._on_request(rid, 0, line.encode(), sink)
            assert agent.ledger[rid]['resp'][0] == 400
        # Default policy is RO; both settings use the same peer and root as raw SFS.
        ro = mod.HostAgent(b'host!!', file_root=tmp, peer=PEER, quiet=True)
        ro._switch_to(dict(sess=1, epoch=1, mac=PEER))
        agent = ro
        rid = 0
        sid = decode(request(hello)['body']).sid
        denied = request(sfs.encode(sfs.T_WRITE, sid, 1, payload))
        assert struct.unpack('<i', decode(denied['body']).payload)[0] == sfs.ERR_ROFS
        # Raw Ethernet MARK demux goes through the existing adapter unchanged.
        raw = b'host!!' + PEER + b'\x88\xb5' + hello
        sink.frames.clear()
        ro.handle(raw.ljust(60, b'\0'), sink)
        assert len(sink.frames) == 1 and decode(sink.frames[0][14:]).type == sfs.T_HELLO | sfs.T_RESP
        sink.frames.clear()
        ro.handle(raw[:6] + b'other!' + raw[12:], sink)
        assert not sink.frames
        config_path = Path(tmp, 'config.json')
        for writable in (False, True):
            config_path.write_text(json.dumps(dict(iface='eth0', root=tmp,
                **{'allow-write': writable, 'peer': '70:65:65:72:21:21',
                   'backend': 'np21w', 'frame-endpoint': '127.0.0.1:8026',
                   'operator-endpoint': '127.0.0.1:8027', 'state-dir': tmp})))
            # Capture main's real configuration arguments before opening sockets.
            with patch.object(sys, 'argv', ['host_agent', '--config', str(config_path)]), \
                    patch.object(mod, 'HostAgent', side_effect=RuntimeError('captured')) as factory:
                try:
                    mod.main()
                except RuntimeError as exc:
                    assert str(exc) == 'captured'
                settings = factory.call_args.kwargs
                assert settings['file_root'] == tmp and settings['peer'] == PEER
                assert settings['allow_write'] == (('/',) if writable else ())
    print('PASS remote-p4b')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', default=str(ROOT / 'tools/host_agent.py'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    test(args.module)
    if args.mutate:
        source = Path(args.module).read_text()
        mutations = [
            ('self.sfs_link is None or self._sfs_generation != self.host_generation',
             'True', 'rid lost replay cache'),
            ('and len(payload) > room:', 'and False:', 'length guard removed'),
            ('len(ent["got"]) != ent["decl"]:', 'False:', 'length guard removed'),
            ('        except ValueError:\n            self._answer(rid, ent, HTTP_BAD, b"", None, out)',
             '        except ValueError:\n            self._sfs_backend().call(ent["got"], self.peer)\n            self._answer(rid, ent, HTTP_BAD, b"", None, out)', 'bad CRC'),
            ('now >= ent["sfs_deadline"]', 'False', 'deadline removed'),
            ('self.now() >= ent["sfs_deadline"]', 'False', 'deadline removed')]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, 'host_agent.py')
            for old, new, marker in mutations:
                assert source.count(old) == (2 if old.startswith('self.now()') else 1), old
                path.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(path)],
                                        capture_output=True, text=True)
                assert result.returncode and marker in result.stderr, result.stderr
                print('RED', marker)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
