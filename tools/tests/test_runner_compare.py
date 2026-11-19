"""Exercise the stage 3 paired-run acceptance criteria on saved output."""
CHECK = dict(name='runner-compare-host', tier=1, needs=[], budget=2,
             covers=['tools/ci/runner_compare.py', '.github/workflows/selfhost-probe.yml',
                     'tools/tests/test_runner_compare.py'],
             basis=['docs/POLICY_CI.md G9'])

from pathlib import Path
import subprocess
import sys
import tempfile

TOOL = Path(__file__).resolve().parents[1] / 'ci/runner_compare.py'


def cases(tool, root):
    left, right = [root / side for side in ('standard', 'selfhost')]
    for folder in (left, right):
        (folder / 'logs').mkdir(parents=True, exist_ok=True)
        (folder / 'identity').write_text('sha\n4/6\n')
        (folder / 'expected').write_text('a\nb\n')
        (folder / 'check_times.tsv').write_text('a\t1\t0\nb\t2\t0\n')
        (folder / 'logs/a.log').write_text('MUTATION 1 RED\nMUTATION 2 RED\n')
        (folder / 'logs/b.log').write_text('GREEN\n')

    def run():
        return subprocess.run([sys.executable, tool, left, right], capture_output=True, text=True)

    (right / 'logs/a.log').write_text('2 runtime RED\n')
    result = run()
    assert result.returncode == 0 and 'b: 件数の比較の対象外' in result.stdout
    for path, value, message in (
        ('identity', 'other', 'SHA or shard differs'),
        ('check_times.tsv', 'a\t1\t0\n', 'registration mismatch'),
        ('check_times.tsv', 'a\t1\t1\nb\t2\t0\n', 'rc 0/1'),
        ('logs/a.log', '1 runtime RED', 'mutations 2/1'),
        ('logs/a.log', 'GREEN', 'mutations 2/None'),
        ('logs/b.log', 'SKIP tool', 'added SKIP'),
    ):
        file = right / path
        original = file.read_text()
        file.write_text(value)
        result = run()
        assert result.returncode == 1 and message in result.stderr, message
        file.write_text(original)


def run(ctx):
    return subprocess.call([sys.executable, '-B', str(Path(ctx.root) / 'tools/tests/test_runner_compare.py'),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        cases(TOOL, root)
        if '--mutate' in sys.argv:
            source = TOOL.read_text()
            anchor = 'count != other_count'
            assert source.count(anchor) == 1
            mutant = root / 'mutant.py'
            mutant.write_text(source.replace(anchor, 'False'))
            try:
                cases(mutant, root)
            except AssertionError as error:
                assert str(error) == 'mutations 2/1'
                print('MUTATION 1 RED')
            else:
                raise AssertionError('mutant survived')
    print('runner-compare: OK')
