"""Exercise P0-7 evidence rules on a synthetic registration tree."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import quality_candidates as q
from run import Check


def verify(root):
    source = root / 'tools/tests/fixture.py'
    source.parent.mkdir(parents=True)
    table = [('before' + str(i), 'after') for i in range(50)]
    source.write_text('# pure model; compile-only\n')
    decl = root / 'decl.py'
    decl.write_text("def run(ctx): return helper('tools/tests/fixture.py')\n")
    path = 'tools/tests/fixture.py'
    a = Check('sep5-a', 'decl.py', (path, 'other'), 1, basis=('B', 'A'))
    b = Check('usb2', 'decl.py', ('other', path), 1, basis=('A', 'B'))
    c = Check('unrelated', 'decl.py', (), 1)
    checks = {row.name: row for row in (a, b, c)}
    def rows():
        return {(kind, name): (status, evidence) for kind, name, status, evidence
                in q.inventory(root, checks)}

    result = rows()
    assert result['段名', a.name][0] == '候補'
    assert result['段名', b.name][0] == '条件外'
    assert result['同一宣言', c.name][0] == q.UNKNOWN
    assert b.name in result['同一宣言', a.name][1]
    assert result['模型', a.name][0] == result['コンパイルのみ', a.name][0] == '候補'
    for extra, expected in [([], q.UNKNOWN), ([('same', 'same')], q.UNKNOWN),
                            ([('added', 'different')], '候補')]:
        source.write_text(f'CASES = {table + extra!r}\nMUTANTS = {table * 2!r}\n'
                          'for before, after in CASES:\n    text.replace(before, after)\n')
        result = rows()
        assert result['変異50超', a.name][0] == expected
        assert result['模型', a.name][0] == q.UNKNOWN
    source.write_text(f'cases = {table + [("extra", "row")]!r}\n'
                      'for case in cases:\n    failures = execute(build(case))\n'
                      '    assert failures\n    print("RED runtime", case)\n')
    assert rows()['変異50超', a.name][0] == '候補'
    source.write_text(source.read_text().replace('build(case)', 'build(control)'))
    assert rows()['変異50超', a.name][0] == q.UNKNOWN
    source.write_text("argv = ['gcc', '-fsyntax-only', 'probe.c']\n")
    assert rows()['コンパイルのみ', a.name][0] == '候補'
    b = Check(b.name, b.path, b.covers, 1, basis=('B',))
    checks[b.name] = b
    assert rows()['同一宣言', a.name][0] == '条件外'


def main():
    with tempfile.TemporaryDirectory() as tmp:
        verify(Path(tmp))
    if '--mutate' in sys.argv:
        edits = [('if count > 50:', 'if count >= 50:'),
                 ('count += a.value != b.value', 'count += 1'),
                 ('frozenset(c.basis)', 'frozenset()')]
        for old, new in edits:
            with tempfile.TemporaryDirectory() as tmp:
                work = Path(tmp)
                script = Path(__file__).resolve()
                shutil.copy(script, work)
                code = Path(q.__file__).read_text()
                assert code.count(old) == 1
                (work / 'quality_candidates.py').write_text(code.replace(old, new))
                proc = subprocess.run([sys.executable, '-B', str(work / script.name)],
                                      env=dict(os.environ, PYTHONPATH=str(q.ROOT / 'tools/checks')),
                                      capture_output=True, text=True)
                assert proc.returncode == 1 and proc.stderr.rstrip().endswith('AssertionError'), proc.stderr
                print('RED', old)
    print('PASS quality_candidates')


if __name__ == '__main__':
    main()
