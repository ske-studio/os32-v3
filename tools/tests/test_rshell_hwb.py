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
BEGIN = 'HW run=3 seq=1 line=8 phase=begin op=mmiowrite8 target=0:8.0/bar1+0x3C4 addr=0x204003C4 val=0x09 rc=PENDING\r\n'
END = 'HW run=3 seq=1 line=8 phase=end op=mmiowrite8 addr=0x204003C4 val=0x09 rc=OK\r\n'
READ = 'HW run=3 seq=2 line=9 phase=end op=mmioread8 addr=0x204003C5 val=0x01 rc=OK\r\n'
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


def check(condition, message='check failed'):
    if not condition:
        raise AssertionError(message)


def exercise(mod):
    serial.rshell_hwb = mod
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / 'capture'
        args = argparse.Namespace(line=['run', '/test/a.hwb'], out=str(path), timeout=0.01)
        def attempt(body, **kw):
            port = Port(body, **kw)
            rc = serial.run_hwb(args, port, 9600)
            saved = path.read_text()
            check(saved == '> hw run /test/a.hwb\n' + body.replace('\r\n', '\n'))
            check(port.sent.count('hw run /test/a.hwb') == 1)
            return rc, port.sent
        good = BEGIN + END + READ + TERM
        check(attempt(good) == (0, ['hw run /test/a.hwb', 'echo $?']))
        check(attempt(BEGIN + TERM)[0] == 1)
        check(attempt(BEGIN + 'HW run=3 seq=1 line=8 phase=end op=mmiowrite8\r\n' + TERM)[0] == 1)
        check(attempt(good.replace('rc=OK\r\n', 'rc=BAD\r\n', 1))[0] == 1)
        check(attempt(READ + TERM)[0] == 1)
        check(attempt(BEGIN + END + READ.replace('seq=2', 'seq=3') + TERM)[0] == 1)
        check(attempt(BEGIN + END + READ + READ + TERM)[0] == 1)
        check(attempt(BEGIN + END + READ.replace('run=3', 'run=4') + TERM)[0] == 1)
        check(attempt(BEGIN + END + READ)[0] == 1)
        check(attempt(good, eot=False)[0] == 1)
        check(attempt(good, status='5')[0] == 1)
        check(attempt(good.replace('rc=OK lines=', 'rc=TIMEOUT lines='), status='124')[0] == 124)


def fields_exercise(mod):
    # Independently specified hwb_log.c wire spellings (HW design §§3-4/P9).
    operations = [
        ('mmiofill32', 'addr=0x20000000 val=0x03030303 count=4', 'addr=0x20000000 val=0x03030303 count=4 done=4'),
        ('poll', 'addr=0xA0000 val=0x00', 'addr=0xA0000 val=0x01 reads=2 elapsed_us=0x100000002'),
        ('wait', 'requested_us=1000', 'requested_us=1000 elapsed_us=0x400'),
        ('print', '', 'tag="a\\x20b\\\\c\\\"d" val=0x12'),
        ('log', '', 'text="a\\x0Ab"'),
    ]
    for op, begin, end in operations:
        prefix = 'HW run=3 seq=1 line=1 phase='
        first = prefix + 'begin op=' + op + ' ' + begin + ' rc=PENDING\n'
        last = prefix + 'end op=' + op + ' ' + end + ' rc=OK\n'
        check(not mod.inspect(first + last + TERM)[1], op + ' positive')
        for phase, payload in [('begin', begin), ('end', end)]:
            for token in payload.split():
                row = first if phase == 'begin' else last
                broken = row.replace(token, '')
                body = broken + last if phase == 'begin' else first + broken
                check(mod.inspect(body + TERM)[1], op + ' missing ' + token)
                key = token.split('=', 1)[0]
                body = (first + last).replace(token, key + '=invalid')
                check(mod.inspect(body + TERM)[1], op + ' invalid ' + key)
    fill = ('HW run=3 seq=1 line=1 phase=begin op=mmiofill8 addr=0x20000000 val=0x07 count=4 rc=PENDING\n'
            'HW run=3 seq=1 line=1 phase=end op=mmiofill8 addr=0x20000000 val=0x07 count=4 done=4 rc=OK\n')
    for old, new in [('done=4', 'done=3'), ('done=4', 'done=5'),
                     ('count=4', 'count=4294967296'), ('count=4', 'count=0')]:
        check(mod.inspect(fill.replace(old, new) + TERM)[1], 'fill progress')
    check(not mod.inspect(fill.replace('done=4 rc=OK', 'done=2 rc=IO') + TERM.replace('rc=OK', 'rc=IO'))[1])
    check(mod.inspect(fill.replace('count=4 done=4', 'count=5 done=5') + TERM)[1])
    for op in ('print', 'log'):
        key = 'tag' if op == 'print' else 'text'
        extra = ' val=0x0' if op == 'print' else ''
        check(not mod.inspect('HW run=3 seq=1 line=1 phase=end op=' + op +
                              ' ' + key + '=""' + extra + ' rc=OK\n' + TERM)[1])


def unresolved_exercise(mod):
    # P9 permits the sentinel only in the address of begin/failed end records.
    for op in [space + action + width for space in ('pci', 'io', 'mmio')
               for action in ('read', 'write') for width in ('8', '16', '32')] + ['poll']:
        prefix = 'HW run=3 seq=1 line=1 phase='
        begin = prefix + 'begin op=' + op + ' addr=UNRESOLVED val=0x00 rc=PENDING\n'
        extra = ' reads=0 elapsed_us=0x0' if op == 'poll' else ''
        for rc in mod.STATUS:
            end = prefix + 'end op=' + op + ' addr=UNRESOLVED val=0x00' + extra + ' rc=' + rc + '\n'
            terminal = TERM.replace('rc=OK', 'rc=' + rc)
            check(bool(mod.inspect(begin + end + terminal)[1]) == (rc == 'OK'),
                  op + ' unresolved ' + rc)
            # A resolved end may follow an unresolved begin.
            resolved = end.replace('addr=UNRESOLVED', 'addr=0xA0000')
            check(not mod.inspect(begin + resolved + terminal)[1], op + ' resolved end')
            for token in ('addr=missing', 'addr=unresolved', 'addr='):
                check(mod.inspect((begin + end + terminal).replace('addr=UNRESOLVED', token))[1])
            check(mod.inspect((begin + end + terminal).replace('val=0x00', 'val=UNRESOLVED'))[1])


MUTATIONS = [
    ('fill fields', 'tools/rshell_hwb.py', "elif re.fullmatch(r'mmiofill(?:8|16|32)', op):", "elif False:", 'fill count/done required'),
    ('fill progress', 'tools/rshell_hwb.py', "done > count or (rc == 'OK' and (count == 0 or done != count))", "False", 'fill partial success'),
    ('fill match', 'tools/rshell_hwb.py', "pending_fill != (seq, fields['addr'], fields['val'], count)", "False", 'fill begin/end agree'),
    ('unresolved rejected', 'tools/rshell_hwb.py', "fields[key] == 'UNRESOLVED'", "fields[key] == 'disabled'", 'P9 failed address accepted'),
    ('unresolved success', 'tools/rshell_hwb.py', "phase == 'end' and rc != 'OK'", "phase == 'end'", 'P9 success requires resolved address'),
    ('poll fields', 'tools/rshell_hwb.py', "elif op == 'poll':", "elif op == 'disabled-poll':", 'poll missing address/reads/elapsed'),
    ('wait fields', 'tools/rshell_hwb.py', "elif op == 'wait':", "elif op == 'disabled-wait':", 'wait missing request/elapsed'),
    ('text fields', 'tools/rshell_hwb.py', "rc == 'OK' and op in ('print', 'log'):", "rc == 'OK' and op in ():", 'print/log missing quoted text'),
    ('missing end', 'tools/rshell_hwb.py', 'if pending is not None:\n        errors.append(\'missing end seq=%d line=%d\' % pending[:2])', 'if False:\n        errors.append(\'missing end seq=%d line=%d\' % pending[:2])', 'unfinished begin'),
    ('sequence', 'tools/rshell_hwb.py', 'if last is not None and seq != last + 1:\n                        errors.append', 'if False:\n                        errors.append', 'missing sequence'),
    ('mixed run', 'tools/rshell_hwb.py', 'if run_id != run:', 'if False:', 'mixed run'),
]


def main():
    exercise(load(SRC))
    fields_exercise(load(SRC))
    unresolved_exercise(load(SRC))
    print('SUMMARY PASS', flush=True)
    if '--mutate' in sys.argv:
        return mutate()
    return 0


def mutate():
    original = SRC.read_text()
    with tempfile.TemporaryDirectory() as tmp:
        for name, filename, before, after, case in MUTATIONS:
            check(original.count(before) == 1, name)
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
        fields_exercise(load(pathlib.Path(sys.argv[-1])))
        unresolved_exercise(load(pathlib.Path(sys.argv[-1])))
    else:
        sys.exit(main())
