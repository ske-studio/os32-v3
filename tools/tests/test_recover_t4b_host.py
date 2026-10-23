"""T4b-9: one fake reader, split terminal/EOT suppression, real monitor."""
from contextlib import redirect_stdout
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
    ('terminal_not_required', '            rc = 1', '            rc = 0'),
    ('raw_not_recorded', 'self.raw.write(chunk)', "self.raw.write(b'')"),
]
ECHO = b'> recover virtual end 7\n'
STATE = b'REC sid=7 seq=3 phase=state event=RESTORING version=1 profile_crc=11 plan_hash=12 snapshot_crc=13\n'
FINAL = b'REC sid=7 seq=4 phase=final event=END result=OK target=S0 verified=1 unknown=0 override=0 trial=0 lost_lines=0 discarded_bytes=0\n'
DATA = ECHO + STATE + FINAL + b'\x04'


def cases(module):
    # Each mutation owns its monitor module; concurrent port patches cannot leak.
    module.recover = load(ROOT / 'tools/rshell_recover.py')
    for split in (1, 7, 256):
        for drop in ('none', 'final', 'eot'):
            with tempfile.TemporaryDirectory(prefix='t4b-host-case-') as directory:
                path = Path(directory)
                state, log, raw, stats = [path / x for x in ('state', 'log', 'raw', 'stats')]
                state.write_text(json.dumps(dict(sid=7, seq=2, image_crc='1234abcd',
                    crc=dict(version=1, profile_crc=11, plan_hash=12, snapshot_crc=13),
                    metadata_frozen=True, last_begin=None)))
                port = Port(DATA, split=split)
                closed = []
                port.close = lambda: closed.append(True)
                args = ['--port', 'fake', '--session', '7', '--image-crc', '1234abcd',
                        '--state', str(state), '--log', str(log), '--raw', str(raw),
                        '--stats', str(stats), '--drop', drop,
                        '--timeout', '10', '--cleanup-timeout', '1']
                with patch.object(module.recover, 'open_port', return_value=port) as opened, \
                        redirect_stdout(io.StringIO()):
                    rc = module.main(args, clock=port.clock)
                assert rc == (0 if drop == 'none' else 1), (split, drop, rc)
                assert raw.read_bytes() == DATA
                expected = DATA.replace(FINAL, b'') if drop == 'final' else (
                    DATA.replace(b'\x04', b'') if drop == 'eot' else DATA)
                assert log.read_bytes() == expected
                aborts = int(drop == 'eot')
                assert port.writes == [b'recover virtual end 7\n'] + [module.recover.abort_frame(7)] * aborts
                assert json.loads(stats.read_text()) == dict(abort_frames=aborts, abort_writes=aborts)
                assert port.t < 12 and closed == [True]
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
    print('t4b host: split none/final/eot, raw/log, abort count PASS')
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(one, MUTATIONS, ('control', '', ''))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
