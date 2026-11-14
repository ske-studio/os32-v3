"""Acceptance predicates and QMP waiting must preserve agent progress."""
CHECK = dict(name='ppc-link-run-host', tier=2, budget=30, needs=[], covers=[
    'tools/ppc/link_run.py', 'tools/ppc/run.py', 'tools/ppc_lan_host.py',
    'tools/host_*.py', 'tools/ppc_link_transport.py', 'net/link_platform.h',
    'platform/powermac/link_platform.c', 'tools/tests/test_ppc_link_run.py',
    'tools/lan_bridge.py', 'tools/lanfs_host.py', 'tools/ppc_hostfs.py',
    'tools/remote_transport.py', 'tools/serialfs_host.py',
    'arch/ppc/entry.S', 'arch/ppc/ofw_note.S', 'platform/powermac/boot.c',
    'platform/powermac/escc.c', 'platform/powermac/escc_tree.c',
    'platform/powermac/ofw.c'])
import os
from pathlib import Path
import subprocess
import sys


def probe(source):
    sys.path[:0] = [str(Path('tools').resolve()), str(Path('tools/ppc').resolve())]
    ns = {'__file__': str(Path('tools/ppc/link_run.py').resolve()), '__name__': 'probe'}
    exec(compile(source, ns['__file__'], 'exec'), ns)
    def rejected(fn, *args):
        try:
            fn(*args)
        except AssertionError:
            return
        raise AssertionError('invalid evidence accepted')
    log = ns['log_ready']
    records = [dict(seq=i, drop_records=0, drop_bytes=0) for i in range(3)]
    body = b'KBOOT OF-CAPTURE\nother boot logs\nLINK HEAP used=123\n'
    assert log(records, body)
    assert not log(records, b'KBOOT OF-CAPTURE\n')
    assert not log(records, b'LINK HEAP used=123\nKBOOT OF-CAPTURE\n')
    assert not log(records, body[:-1])
    rejected(log, records[1:], body)
    rejected(log, [records[0], records[2]], body)
    rejected(log, [records[0], records[0]], body)
    for key in ('drop_records', 'drop_bytes'):
        rejected(log, [dict(records[0], **{key: 1})], body)
    def frame(rid, op=2):
        data = bytearray(60)
        data[12:14] = b'\x88\xb5'
        data[14] = op
        data[28:32] = rid.to_bytes(4, 'little')
        return data.hex()
    duplicate = ns['duplicate_requests']
    assert duplicate([frame(1), frame(2), '> '+frame(1), frame(1, 8)]) == 0
    assert duplicate([frame(1)]*3 + [frame(2)]*2) == 3
    rate = ns['tick_rate']
    first = dict(tick=0xfffffff0, tb=123456789)
    def last(delta):
        return dict(tick=(first['tick']+delta) & 0xffffffff, tb=first['tb']+100000000)
    for delta in (90, 100, 110):
        assert rate(first, last(delta)) == delta
    for delta in (0, 89, 111, 1000):
        rejected(rate, first, last(delta))
    rejected(rate, first, first)
    # A QMP reply arriving only after several pump calls models a slow HMP.
    readfd, writefd = os.pipe()
    calls = []
    q = ns['PumpQMP'].__new__(ns['PumpQMP'])
    from types import SimpleNamespace
    import time
    with os.fdopen(readfd, 'rb', buffering=0) as stream:
        q.process = SimpleNamespace(stdout=stream)
        q.buffer, q.deadline = b'', time.monotonic()+1
        def pump():
            calls.append(1)
            if len(calls) == 3:
                os.write(writefd, b'{"return": "ok"}\n')
        q.pump = pump
        try:
            assert q.read() == {'return': 'ok'} and len(calls) >= 3
        finally:
            os.close(writefd)


def run(ctx):
    source = (ctx.root / 'tools/ppc/link_run.py').read_text()
    mutations = [
        ("[r['seq'] for r in logs] == list(range(len(logs)))", 'True'),
        ("r['drop_records'] == r['drop_bytes'] == 0", 'True'),
        ('sum(n - 1 for n in counts.values())', '0'),
        ('90 <= hz <= 110', 'True'),
    ]
    cases = [('baseline', source)]
    if ctx.mutate:
        for old, new in mutations:
            assert source.count(old) == 1, old
            cases.append((old, source.replace(old, new)))
    for name, text in cases:
        result = subprocess.run([sys.executable, '-B', __file__, '--probe'],
                                input=text, text=True, cwd=ctx.root,
                                capture_output=True, timeout=10)
        assert result.returncode == (0 if name == 'baseline' else 1), (name, result.stderr)
        if name != 'baseline':
            assert 'AssertionError' in result.stderr, result.stderr
        print(('GREEN ' if name == 'baseline' else 'RED runtime ') + name)
    return 0


if __name__ == '__main__':
    probe(sys.stdin.read())
