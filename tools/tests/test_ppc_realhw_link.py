#!/usr/bin/env python3
"""Real-hardware evidence fixtures; no physical sockets or guest execution.

期待の出所: 設計 ID (PPC_P5_REST_DESIGN §2 realhw_run・§5 の R の行、PM 決定 R1〜R3 2026-11-16)。
nest の試験時計: FINDINGS / realhw_run.py の期限の契約。
"""
CHECK = dict(name='ppc-realhw-link', tier=1, budget=10, needs=['make:all'],
             covers=['exec/exec.c', 'tools/accept/ppc_shell_flow.inc', 'userland/shell/main.c', 'userland/shell/rshell.c', 'tools/ppc/realhw_run.py', 'tools/tests/test_ppc_realhw_link.py',
                     'tools/ppc/sc_run.py', 'tools/ppc/sd_run.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_mutations.py', 'tools/ppc/sb_run.py',
                     'tools/ppc/run.py', 'tools/ppc_link_transport.py',
                     'tools/remote_transport.py', 'tools/ppc_lan_host.py',
                     'tools/ppc_hostfs.py', 'tools/host_agent_config.py',
                     'tools/ppc/*.py', 'tools/host*.py', 'tools/lan*.py',
                     'tools/serialfs_host.py', 'arch/ppc/*.[cSh]',
                     'platform/powermac/*.[ch]', 'platform/powermac/ppc_kernel.c',
                     'fs/serialfs/serialfs_persistent.c', 'kernel/console/con_sink.c',
                     'kernel/console/console.c', 'mm/kmalloc.c',
                     'userland/shell/rshell_lan.c', 'userland/ppc/*',
                     'build/**/*.mk', 'build/out/ppc-user/*'],
             basis=['PPC_P5_REST_DESIGN §2・§5'])
import base64
import copy
import errno
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/ppc'))
import realhw_run as rh

PEER = '52:54:00:12:34:56'
GUEST = bytes.fromhex('525400123456')
HOST = bytes.fromhex('02005e000001')
BOOT = '01'*16
# Existing kboot_run.validate contract, not the implementation's output.
LOG = (b'KBOOT OF-CAPTURE ram=128 admitted=128 tb_hz=1000\n'
       b'KBOOT OF-OFF calls=1\n'
       b'KBOOT MMU-D root=0x1000 hpt=0x2000 bytes=4096 ram_bat=0\n'
       b'KBOOT MEMORY online=1 heap=1 shm=1\n'
       b'KBOOT TRAP dsi=pass isi=pass guard=pass flush=pass\n'
       b'KBOOT MPIC sources=64 priority=0 cpu=0 delivery=pass\n'
       b'KBOOT CLOCK hz=100 tick=200 us=2000000\n'
       b'KBOOT READY stage=pre-user\n'
       b'KBOOT ALIVE tick_delta=200 tb_us=2000000 of_calls_delta=0\n')


def b64(data):
    return base64.b64encode(data).decode()


def frame(op=6, payload=b'', guest=True, sess=7, rid=1, flags=0):
    return ((HOST+GUEST if guest else GUEST+HOST)+b'\x88\xb5' +
            struct.pack('<BBHIIHIH', op, flags, 1, 0, 0, len(payload), rid, sess)+payload)


def fixtures(case):
    probe = dict(ok=True, agent_generation=3, peer=PEER, hello=True)
    logs = [dict(boot=BOOT, seq=i, drop_records=0, drop_bytes=0, text_b64=b64(data))
            for i, data in enumerate((LOG[:100], LOG[100:]))]
    rsh = []
    script = {'capture': [], 'nest': [('/bin/nest.bin',
              b'hello ok\nnest kind=1 code=7 parent=12345\n'*2), ('echo $?', b'5\n')],
              'shell': [('echo x > /tmp/t', b''), ('cat /tmp/t', b'x\n'),
                        ('cat /tmp/none', b'cat: /tmp/none: No such file or directory\n'), ('echo $?', b'1\n')],
              'exact': [('cat /host/f', b'file\n')]}[case]
    for line, output in script:
        rsh.append(dict(line=line, result=dict(ok=True, state='done', complete=True,
                   drop=0, body_b64=b64(b'> '+line.encode()+b'\n'+output+b'\x04'))))
    evidence = dict(probes=[probe, probe.copy()], logs=logs, rsh=rsh, begin=100, end=104)
    frames = [(100, frame(1)), (100.1, frame(1, guest=False)),
              (101, frame(2, b'SFS CALL 16\n')), (101.1, frame(3, struct.pack('<HI', 200, 0), guest=False)),
              (102, frame()), (103.9, frame())]
    return evidence, frames


def write_pcap(path, frames):
    class Packet:
        local_mac = HOST
    capture = rh.Capture(Packet(), path)
    for stamp, data in frames:
        with patch.object(rh.time, 'time_ns', return_value=int(stamp*1000000000)):
            capture.record(data)
    capture.file.close()


def assess(tmp, case, evidence, frames):
    path = tmp/'capture.pcap'
    write_pcap(path, frames)
    return rh.assess_link(case, evidence, path, PEER, 'cat /host/f', 'file\n')


def rejected(tmp, label, evidence, frames, reason):
    try:
        assess(tmp, 'nest', evidence, frames)
    except (RuntimeError, AssertionError, KeyError) as exc:
        assert reason in str(exc), (label, str(exc))
        if '--mutate' in sys.argv:
            print('RED '+label+': '+str(exc))
    else:
        raise AssertionError(label+' survived')


def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        for case in ('capture', 'nest', 'shell', 'exact'):
            evidence, frames = fixtures(case)
            result = assess(tmp, case, evidence, frames)
            assert result['pass'] and result['sfs_calls'] == 1
            assert result['sfs_response_statuses'] == [200]
        evidence, frames = fixtures('nest')
        changes = [
            ('generation', lambda e: e['probes'][1].update(agent_generation=4), 'generation'),
            ('peer', lambda e: e['probes'][1].update(peer='00:00:00:00:00:00'), 'peer'),
            ('hello', lambda e: e['probes'][1].update(hello=False), 'HELLO'),
            ('unfinished-out', lambda e: e['rsh'][0]['result'].update(state='pending'), 'incomplete'),
            ('out-drop', lambda e: e['rsh'][0]['result'].update(drop=1), 'incomplete'),
            ('missing-rsh', lambda e: e['rsh'].pop(), 'missing RSH'),
            ('missing-log', lambda e: e['logs'].pop(0), 'sequence'),
            ('tail-log', lambda e: e['logs'].pop(), 'missing milestone'),
            ('missing-drop-report', lambda e: e['logs'][0].pop('drop_records'), 'drop_records'),
            ('log-drop', lambda e: e['logs'][0].update(drop_bytes=1), 'LOG drop'),
            ('boot', lambda e: e['logs'][1].update(boot='02'*16), 'boot changed'),
            ('second-child', lambda e: e['rsh'][0]['result'].update(body_b64=b64(
                b'> /bin/nest.bin\nhello ok\nnest kind=1 code=7 parent=12345\n'
                b'hello ok\nnest kind=0 code=0 parent=12345\n\x04')), 'body mismatch'),
            ('no-eot', lambda e: e['rsh'][0]['result'].update(body_b64=b64(b'> /bin/nest.bin\n')), 'EOT'),
        ]
        for label, change, reason in changes:
            bad = copy.deepcopy(evidence)
            change(bad)
            rejected(tmp, label, bad, frames, reason)
        for label, changed, reason in [
                ('session', frames[:-1]+[(103.9, frame(sess=8))], 'session changed'),
                ('alive-gap', frames[:-2]+[(103.9, frame())], 'ALIVE gap'),
                ('alive-tail', frames[:-2], 'HELLO/ALIVE'),
                ('missing-host-hello', frames[:1]+frames[2:], 'HELLO/ALIVE'),
                ('empty-pcap', [], 'HELLO/ALIVE')]:
            rejected(tmp, label, evidence, changed, reason)
        e, f = fixtures('exact')
        try:
            assess(tmp, 'exact', e, f[:3]+f[4:])
        except RuntimeError as exc:
            assert 'SFS response' in str(exc)
            if '--mutate' in sys.argv:
                print('RED missing-sfs-response: '+str(exc))
        else:
            raise AssertionError('SFS response missing accepted')
        path = tmp/'capture.pcap'
        write_pcap(path, frames)
        path.write_bytes(path.read_bytes()[:-1])
        try:
            list(rh.read_pcap(path))
        except RuntimeError as exc:
            assert 'truncated' in str(exc)
        else:
            raise AssertionError('truncated pcap accepted')
        # Exercise collection and saved artifacts with fake socket boundaries.
        saved, packets = fixtures('nest')
        write_pcap(tmp/'template.pcap', packets)
        template = (tmp/'template.pcap').read_bytes()
        class FakeCapture:
            def __init__(self, packet, path):
                path.write_bytes(template)
                self.file = path.open('ab')
        class FakePacket:
            sock = SimpleNamespace(close=lambda: None)
        class Client:
            def __init__(self):
                self.transport = SimpleNamespace(events=copy.deepcopy(saved['logs']), sock=object())
                self.lines = []
            def probe(self):
                return dict(saved['probes'][0], ready={'mode': 'cui'})
            def subscribe_log(self):
                return {'ok': True}
            def receive(self):
                return dict(self.transport.events.pop(0), payload=b'')
            def send_line(self, line, timeout):
                self.lines.append(line)
                return saved['rsh'][len(self.lines)-1]['result']
            def close(self):
                pass
        image = tmp/'image'
        image.write_bytes(b'fixture deployed image')

        def collect(out):
            client = Client()
            args = SimpleNamespace(iface='fake0', case='nest', out=out, image=image,
                                   machine='fixture OF/PVR', peer=PEER, seconds=.01,
                                   command='', expected='', list_frames=False, camera_frame=[])
            # Allow the deadline setup, four loop checks (two logs and two
            # commands), and two send timeouts before forcing expiry.
            clock_reads = 0

            def monotonic():
                nonlocal clock_reads
                clock_reads += 1
                pending = client.transport.events or len(client.lines) < len(saved['rsh'])
                if pending and clock_reads <= 7:
                    return 0.0
                return 2 * args.seconds

            with patch.object(rh, 'PassivePacket', return_value=FakePacket()), \
                    patch.object(rh, 'Capture', FakeCapture), \
                    patch.object(rh, 'LinkHost', return_value=client), \
                    patch.object(rh.select, 'select', return_value=([], [], [])), \
                    patch.object(rh, 'time', SimpleNamespace(
                        monotonic=monotonic, time=iter((100, 104)).__next__)):
                status = rh.link_main(args)
            assert client.lines == ['/bin/nest.bin', 'echo $?'], client.lines
            assert status == 0
            assert json.loads((out/'result.json').read_text())['pass']
            stored = json.loads((out/'link-evidence.json').read_text())
            assert len(stored['logs']) == 2 and len(stored['rsh']) == 2

        collect(tmp/'run')
        if '--mutate' in sys.argv:
            link_script = rh.link_script
            with patch.object(rh, 'link_script',
                              side_effect=lambda *args: link_script(*args)[:1]):
                try:
                    collect(tmp/'missing-second-command')
                except AssertionError as exc:
                    assert exc.args == (['/bin/nest.bin'],)
                    print('RED missing-second-command: '+str(exc))
                else:
                    raise AssertionError('missing-second-command survived')
        # Opening AF_PACKET is replaced; assert ETH_P_ALL and no send surface.
        class Socket:
            def bind(self, address):
                assert address == ('fake0', 0)
            def getsockname(self):
                return (None, None, None, None, HOST)
            def close(self):
                pass
        with patch.object(rh.socket, 'socket', return_value=Socket()) as factory:
            packet = rh.PassivePacket('fake0')
            factory.assert_called_once_with(rh.socket.AF_PACKET, rh.socket.SOCK_RAW, rh.socket.htons(3))
            assert not hasattr(packet, 'send')
        # CLI fixture denies socket acquisition, exercising SKIP/required failure.
        args = SimpleNamespace(iface='fake0', case='capture')
        with patch.object(rh, 'PassivePacket', side_effect=PermissionError(errno.EPERM, 'fixture')), \
                patch.object(rh, 'LinkHost'):
            with patch.dict(rh.os.environ, {'OS32_REQUIRE_PPC': '0'}):
                assert rh.link_main(args) == 0
            with patch.dict(rh.os.environ, {'OS32_REQUIRE_PPC': '1'}):
                assert rh.link_main(args) == 1
    print('PASS realhw link evidence fixtures (no sockets)')


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_ppc_realhw_link.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
