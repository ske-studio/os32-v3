"""T4b-9: one fake reader, split terminal/EOT suppression, real monitor."""
CHECK = dict(name='recover-t4b-host', tier=1,
    covers=['tools/accept/recover_t4b_host.py', 'tools/rshell_recover.py', 'tools/tests/test_rshell_recover.py', 'tools/tests/mutpar.py', 'tools/lan_bridge.py', 'tools/lanfs_host.py', 'tools/rshell_peek.py', 'tools/rshell_serial.py', 'tools/serialfs_host.py'],
    needs=[], budget=0.913418)

from contextlib import redirect_stderr
import io
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
import mutpar
from test_rshell_recover import Port, load

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/accept/recover_t4b_host.py'
MUTATIONS = [
    ('terminal_not_required', '        return 1', '        return 0'),
    ('non_ok_terminal_accepted', "if result != 'OK':", 'if False:'),
    ('raw_not_recorded', 'self.raw.write(chunk)', "self.raw.write(b'')"),
]
ECHO = b'> recover virtual end 7\n'
STATE = b'REC sid=7 seq=3 phase=state event=RESTORING version=1 profile_crc=11 plan_hash=12 snapshot_crc=13 cleanup_seconds=1\n'
DIAG = b'REC sid=7 seq=4 phase=diag event=TX fail_seq=0 pos=0 fn=NONE rc=0 where=NONE sent=0 polls=0 bytes=0\n'
FINAL = b'REC sid=7 seq=5 phase=final event=END result=OK target=S0 verified=1 unknown=0 override=0 saved=0 actual=0 trial=0 lost_lines=0 discarded_bytes=0 txd=0\n'
DATA = ECHO + STATE + DIAG + FINAL + b'\x04'


class PartialPort(Port):
    def write(self, data):
        return super().write(data[:3])


def cases(module):
    # Each mutation owns its monitor module; concurrent port patches cannot leak.
    module.recover = load(ROOT / 'tools/rshell_recover.py')
    scenarios = [(split, drop, 'normal') for split in (1, 7, 256)
                 for drop in ('none', 'final', 'eot')]
    scenarios += [(256, 'none', 'paused'), (256, 'eot', 'partial'),
                  (256, 'none', 'bad-json'), (256, 'none', 'bad-sid')]
    for split, drop, scenario in scenarios:
        with tempfile.TemporaryDirectory(prefix='t4b-host-case-') as directory:
            path = Path(directory)
            state, log, raw = [path / x for x in ('state', 'log', 'raw')]
            previous = module.recover.Records(7).data
            previous.update(seq=2, image_crc='1234abcd',
                crc=dict(version=1, profile_crc=11, plan_hash=12, snapshot_crc=13),
                metadata_frozen=True, paused=True)
            state.write_text(json.dumps(previous))
            if scenario == 'bad-json':
                state.write_text('{')
            elif scenario == 'bad-sid':
                previous = json.loads(state.read_text())
                previous['sid'] = 8
                state.write_text(json.dumps(previous))
            data = (ECHO + STATE.replace(b'RESTORING', b'PAUSED').replace(b' cleanup_seconds=1', b'') + b'\x04'
                    if scenario == 'paused' else DATA)
            port = (PartialPort if scenario == 'partial' else Port)(data, split=split)
            closed = []
            port.close = lambda: closed.append(True)
            args = ['--port', 'fake', '--session', '7', '--image-crc', '1234abcd',
                    '--state', str(state), '--log', str(log), '--raw', str(raw),
                    '--drop', drop,
                    '--timeout', '10', '--cleanup-timeout', '6']
            printed = []
            module.print = printed.append  # per module: mutations run in threads
            errors = io.StringIO()
            with patch.object(module.recover, 'open_port', return_value=port) as opened, \
                    redirect_stderr(errors):
                rc = module.main(args, clock=port.clock)
            assert errors.getvalue() == ''
            assert not any('Traceback' in line for line in printed)
            if scenario in ('bad-json', 'bad-sid'):
                assert rc == 1 and len(printed) == 1 and printed[0].startswith('recover: ')
                if scenario == 'bad-sid':
                    assert printed == ['recover: saved session differs']
                opened.assert_not_called()
                continue
            assert rc == (0 if drop == 'none' and scenario != 'paused' else 1), (split, drop, scenario, rc)
            if scenario == 'paused':
                assert 'recover: end did not reach verified terminal' in printed
            assert raw.read_bytes() == data
            expected = data.replace(FINAL, b'') if drop == 'final' else (
                data.replace(b'\x04', b'') if drop == 'eot' else data)
            assert log.read_bytes() == expected
            aborts = int(drop == 'eot')
            writes = [b'recover virtual end 7\n'] + [module.recover.abort_frame(7)] * aborts
            assert b''.join(port.writes) == b''.join(writes)
            if scenario == 'partial':
                assert len(port.writes) > len(writes)
            else:
                assert port.writes == writes
            assert 'abort_frames=%d' % aborts in printed
            assert port.t < 17 and closed == [True]
            opened.assert_called_once_with('fake', 9600)


def one(item):
    name, before, after = item
    source = SOURCE.read_text()
    if before and source.count(before) != 1:
        return name + ': missing/ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='t4b-host-mut-') as directory:
        path = Path(directory) / 'helper.py'
        path.write_text(source.replace(before, after, 1) if before else source)
        try:
            cases(load(path))
        except AssertionError:
            return name + ': RED', 0
        return name + ': GREEN', 1


def main():
    sys.path.insert(0, str(ROOT / 'tools'))
    cases(load(SOURCE))
    print('t4b host: split none/final/eot, raw/log, PAUSED, partial abort count, state errors PASS')
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(one, MUTATIONS, ('control', '', ''))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_recover_t4b_host.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
