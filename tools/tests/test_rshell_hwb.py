"""HW serial collection tests, using independently written §4 records."""
CHECK = dict(name='rshell-hwb-host', tier=1,
             covers=['tools/rshell_hwb.py', 'tools/rshell_serial.py',
                     'tools/rshell_peek.py', 'tools/serialfs_host.py',
                     'tools/lanfs_host.py', 'tools/lan_bridge.py'],
             needs=[], budget=1.0)

import argparse
import importlib.util
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import rshell_serial as serial

SRC = ROOT / 'tools/rshell_hwb.py'
BEGIN = 'HW run=3 seq=12 line=8 phase=begin op=mmiowrite8 target=0:8.0/bar1+0x3C4 addr=0x204003C4 val=0x09 rc=PENDING\r\n'
END = 'HW run=3 seq=12 line=8 phase=end op=mmiowrite8 addr=0x204003C4 val=0x09 rc=OK\r\n'
READ = 'HW run=3 seq=13 line=9 phase=end op=mmioread8 addr=0x204003C5 val=0x01 rc=OK\r\n'
TERM = 'HW run=3 op=end rc=OK lines=10 writes=1\r\n'


def load(path):
    spec = importlib.util.spec_from_file_location('hwb_tested', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Port:
    def __init__(self, body, eot=True, status='0'):
        self.body, self.eot, self.status = body, eot, status
        self.pending = b''
        self.sent = []
        self.in_waiting = 0

    def reset_input_buffer(self):
        self.pending = b''

    def write(self, data):
        line = data.decode().rstrip('\n')
        self.sent.append(line)
        body = self.status + '\r\n' if line == 'echo $?' else self.body
        self.pending = ('> ' + line + '\r\n' + body).encode() + (b'\x04' if self.eot else b'')
        self.in_waiting = len(self.pending)

    def flush(self):
        pass

    def read(self, n):
        data, self.pending = self.pending[:n], self.pending[n:]
        self.in_waiting = len(self.pending)
        return data


def exercise(mod):
    serial.rshell_hwb = mod
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / 'capture'
        args = argparse.Namespace(line=['run', '/test/a.hwb'], out=str(path), timeout=0.01)
        def attempt(body, **kw):
            port = Port(body, **kw)
            rc = serial.run_hwb(args, port, 9600)
            saved = path.read_text()
            assert saved == '> hw run /test/a.hwb\n' + body.replace('\r\n', '\n')
            assert port.sent.count('hw run /test/a.hwb') == 1
            return rc, port.sent
        good = BEGIN + END + READ + TERM
        assert attempt(good) == (0, ['hw run /test/a.hwb', 'echo $?'])
        assert attempt(BEGIN + TERM)[0] == 1
        assert attempt(BEGIN + END + READ.replace('seq=13', 'seq=14') + TERM)[0] == 1
        assert attempt(BEGIN + END + READ + READ + TERM)[0] == 1
        assert attempt(BEGIN + END + READ.replace('run=3', 'run=4') + TERM)[0] == 1
        assert attempt(BEGIN + END + READ)[0] == 1
        assert attempt(good, eot=False)[0] == 1
        assert attempt(good, status='5')[0] == 1
        assert attempt(good.replace('rc=OK lines=', 'rc=TIMEOUT lines='), status='124')[0] == 124


MUTATIONS = [
    ('missing end', 'tools/rshell_hwb.py', 'if pending is not None:\n        errors.append(\'missing end seq=%d line=%d\' % pending[:2])', 'if False:\n        errors.append(\'missing end seq=%d line=%d\' % pending[:2])', 'unfinished begin'),
    ('sequence', 'tools/rshell_hwb.py', 'if last is not None and seq != last + 1:\n                        errors.append', 'if False:\n                        errors.append', 'missing sequence'),
    ('mixed run', 'tools/rshell_hwb.py', 'if run_id != run:', 'if False:', 'mixed run'),
]


def main():
    exercise(load(SRC))
    print('SUMMARY PASS', flush=True)
    if '--mutate' not in sys.argv:
        return 0
    original = SRC.read_text()
    with tempfile.TemporaryDirectory() as tmp:
        for name, filename, before, after, case in MUTATIONS:
            assert original.count(before) == 1, name
            path = pathlib.Path(tmp) / (name.replace(' ', '_') + '.py')
            path.write_text(original.replace(before, after))
            result = subprocess.run([sys.executable, '-B', __file__, '--child', str(path)],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print('MUTATION %s %s (%s)' % (name, 'RED' if result.returncode else 'GREEN', case), flush=True)
            if result.returncode == 0:
                return 1
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', str(ROOT / 'tools/tests/test_rshell_hwb.py')] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    if '--child' in sys.argv:
        exercise(load(pathlib.Path(sys.argv[-1])))
    else:
        sys.exit(main())
