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
    host = 'tools/tests/fixture.c'
    product = 'kernel/example.c'
    (root / 'kernel').mkdir()
    (root / product).write_text('int example(void) { return 1; }')
    (root / host).write_text('int main(void) { return 0; }')
    a = Check('sep5-a', 'decl.py', (path, host, product), 1, basis=('B', 'A'))
    b = Check('usb2', 'decl.py', (host, product, path), 1, basis=('A', 'B'))
    c = Check('unrelated', 'decl.py', (), 1)
    checks = {row.name: row for row in (a, b, c)}
    measurements = {}

    def rows():
        return {(kind, name): (status, evidence) for kind, name, status, evidence, *_
                in q.inventory(root, checks, measurements)}

    result = rows()
    assert result['段名', a.name][0] == '候補'
    assert result['段名', b.name][0] == '条件外'
    assert result['同一宣言', c.name][0] == q.UNKNOWN
    assert b.name in result['同一宣言', a.name][1]
    assert result['模型', a.name][0] == result['コンパイルのみ', a.name][0] == q.UNKNOWN
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
    compile_cmd = "subprocess.run(['gcc', '-fsyntax-only', 'probe.c'])\n"
    source.write_text(compile_cmd)
    assert rows()['コンパイルのみ', a.name][0] == '候補'
    build = f"subprocess.run(['gcc', '{host}', '-o', 'probe'])\n"
    execute = "subprocess.run(['probe'])\n"
    for runner in [execute, "host32.run(['probe'])\n", "subprocess.run(['qemu-i386', 'probe'])\n"]:
        source.write_text(compile_cmd + runner)
        assert rows()['コンパイルのみ', a.name][0] == q.UNKNOWN
    source.write_text(build + execute)
    assert rows()['模型', a.name][0] == '候補'
    source.write_text(build + execute + f"Path('{product}').read_text()\n")
    assert rows()['模型', a.name][0] == q.UNKNOWN
    source.write_text(build + execute)
    (root / host).write_text('#include "../../kernel/example.c"')
    assert rows()['模型', a.name][0] == q.UNKNOWN
    for count, expected in [(50, '条件外'), (51, '候補')]:
        measurements[a.name] = count, '45568508091-check.log:10'
        assert rows()['変異50超', a.name][0] == expected
    b = Check(b.name, b.path, b.covers, 1, basis=('B',))
    checks[b.name] = b
    assert rows()['同一宣言', a.name][0] == '条件外'


def main():
    with tempfile.TemporaryDirectory() as tmp:
        verify(Path(tmp))
    if '--mutate' in sys.argv:
        edits = [('if count > 50:', 'if count >= 50:'),
                 ('count += a.value != b.value', 'count += 1'),
                 ('frozenset(c.basis)', 'frozenset()'),
                 ('if compile_only:', 'if syntax:'),
                 ('if not any(fnmatchcase', 'if True or not any(fnmatchcase'),
                 ('if re.search(', 'if False and re.search('),
                 ("if count > 50 else", "if count >= 50 else")]
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
