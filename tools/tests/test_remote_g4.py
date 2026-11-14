#!/usr/bin/env python3
"""Exercise LAN and serial selection and GSERIAL handoff."""
import base64
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
CHECK = dict(name='remote-g4', tier=1, needs=[], budget=10,
             covers=['tools/gserial.py', 'tools/remote_transport.py', 'tools/host_agent_config.py',
                     'tools/tests/test_remote_g4.py', 'tools/host_image.py',
                     'tools/lan_bridge.py', 'tools/lanfs_host.py',
                     'tools/rshell_hwb.py', 'tools/rshell_peek.py',
                     'tools/rshell_serial.py', 'tools/serialfs_host.py'])


def load(path):
    spec = importlib.util.spec_from_file_location('gserial_g4', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def encoded(body):
    return base64.b64encode(body).decode()


class Lan:
    def __init__(self, result):
        self.result = result
        self.requests = []
        self.notices = []
        self.mode = 'gui'
    def request(self, mode, line, timeout):
        self.requests.append((mode, line))
        return self.result
    def close(self):
        pass
    def notice(self, wait_ms, timeout):
        return dict(ok=True, notices=[dict(body_b64=encoded(self.notices.pop(0)))])
    def probe(self, timeout):
        return dict(ok=True, hello=True, ready=dict(mode=self.mode))


def done(body):
    return dict(ok=True, state='done', complete=True, drop=0, body_b64=encoded(body))


def test(path):
    m = load(path)
    def client(lan, factory=None):
        return m.Client(None, 0.1, stdout=io.StringIO(), stderr=io.StringIO(),
                        lan=lan, transport='auto', port_factory=factory)
    lan = Lan(done(b'gs:00000001 pong\ngs:00000001 end 0\n'))
    assert client(lan).request('ping') == (0, ['pong'])
    for body in (b'gs:00000001 pong\n', b'gs:00000001 end 0 extra\n',
                 b'gs:00000001 pong\ngs:00000002 end 0\n'):
        try:
            client(Lan(done(body))).request('ping')
        except ValueError:
            pass
        else:
            raise AssertionError('end or id accepted')
    calls = []
    def serial():
        calls.append(1)
        raise OSError('serial selected')
    try:
        client(Lan(dict(state='serial')), serial).request('ping')
    except OSError:
        pass
    assert calls == [1]
    calls.clear()
    lan = Lan(done(b'gs:00000001 end 0\n'))
    persistent = client(lan, serial)
    persistent.request('ping')
    lan.result = dict(state='serial')
    try:
        persistent.request('ping')
    except ValueError:
        pass
    else:
        raise AssertionError('later fallback accepted')
    assert not calls, 'later request fell back to serial'
    try:
        client(Lan(dict(state='unknown')), serial).request('ping')
    except ValueError:
        pass
    else:
        raise AssertionError('unknown accepted')
    assert not calls, 'submit fell back to serial'
    lan = Lan(done(b'gs:00000001 cui queued\ngs:00000001 end 0\n'))
    lan.notices = [b'gs:! cui done\n']
    lan.mode = 'cui'
    assert client(lan).leave() == 0
    lan = Lan(done(b'> os32gui\naccepted\n\x04'))
    lan.notices = [b'gs:! ready v1\n']
    assert client(lan).enter() == 0
    # Exercise both CLI parsers through the shared target resolver.
    transport = m.CommandTransport
    targets = []
    def connect(**kwargs):
        resolved = transport(**kwargs)
        targets.append((resolved.host, resolved.port))
        return Lan(done(b'gs:00000001 end 0\n'))
    with patch.object(m, 'CommandTransport', side_effect=connect), \
         patch('remote_transport.load_config', return_value={'operator-endpoint': '127.0.0.3:9134'}), \
         patch.object(sys, 'stdout', io.StringIO()):
        for target in (['--endpoint', '127.0.0.2:9133'], ['--instance', 'nic']):
            assert m.main(target + ['--transport', 'lan', 'ping']) == 0
        assert targets == [('127.0.0.2', 9133), ('127.0.0.3', 9134)], 'gserial target ignored'
        try:
            with patch.object(sys, 'stderr', io.StringIO()):
                m.main(['--endpoint', 'host:1', '--instance', 'nic'] + ['--transport', 'lan', 'ping'])
        except SystemExit as exc:
            assert exc.code == 2
        else:
            raise AssertionError('conflicting targets accepted')
    print('PASS remote-g4')


def main():
    path = Path(sys.argv[sys.argv.index('--module') + 1]) if '--module' in sys.argv else ROOT / 'tools/gserial.py'
    test(path)
    if '--mutate' in sys.argv:
        source = path.read_text()
        mutations = [
            ("CommandTransport(endpoint=args.endpoint, instance=args.instance)",
             "CommandTransport()", "gserial target ignored"),
            ('if result.get("state") == "serial" and self.transport == "auto" and not self.lan_submitted:\n                self._fallback()\n            else:\n                self.lan_submitted = True\n                return self._lan_reply(result)',
             'if result.get("state") in ("serial", "unknown") and self.transport == "auto" and not self.lan_submitted:\n                self._fallback()\n            else:\n                self.lan_submitted = True\n                return self._lan_reply(result)', 'submit fell back to serial'),
            ('terminal = re.fullmatch(r"end ([0-4])", payload)\n            print',
             'terminal = re.fullmatch(r"(.*)", payload)\n            print', 'end verification'),
        ]
        with tempfile.TemporaryDirectory() as directory:
            for old, new, name in mutations:
                assert source.count(old) == 1
                mutated = Path(directory) / 'gserial.py'
                mutated.write_text(source.replace(old, new, 1))
                result = subprocess.run([sys.executable, '-B', __file__, '--module', str(mutated)], capture_output=True, text=True)
                assert result.returncode, name + ' survived'
                print('RED', name)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
