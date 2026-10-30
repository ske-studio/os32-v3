"""Small deterministic trees for request-pack helpers; normal cases precede mutants."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def load(name, source=None):
    path = ROOT / 'tools' / (name + '.py')
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    if source is None:
        spec.loader.exec_module(mod)
    else:
        exec(compile(source, str(path), 'exec'), mod.__dict__)
    return mod


def write(root, name, text):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


def changes_case(mod, root):
    write(root, 'pack.md', 'tools/tests/listed.py\n')
    changed = ['tools/tests/listed.py', 'tools/tests/new.py', 'userland/tests/new.c', 'tools/checks/a.py', 'kernel/unrelated.c']
    diffs = {'tools/tests/new.py': '+if "--mutate" not in sys.argv:\n+    normal()\n+os.urandom(4)\n+random.randint(1, 3)\n-assert result == 2\n+assert result == 3\n', 'userland/tests/new.c': '+void main(void) { }\n'}
    write(root, 'tools/tests/new.py', 'os.urandom(4)\n')
    write(root, 'userland/tests/new.c', 'void main(void) {}\n')
    before = {'userland/tests/new.c': None}
    warnings = mod.inspect_changes(changed, (root / 'pack.md').read_text(), diffs, before)
    text = '\n'.join(warnings)
    assert '列挙外: tools/tests/new.py' in text and '列挙外: tools/tests/listed.py' not in text, text
    assert all(x in text for x in ('mutate', '乱数', 'int main', '期待数値', 'tools/checks/a.py')), text
    good = mod.inspect_changes(['userland/tests/ok.c'], 'userland/tests/ok.c', {'userland/tests/ok.c': '+int main(void) { return 0; }'}, {'userland/tests/ok.c': None})
    assert good == [], good
    assert mod.numeric_expectation('self.assertEqual(result, 2)', 'self.assertEqual(result, -3)')
    assert mod.numeric_expectation('CHECK_EQ(result, 2u)', 'CHECK_EQ(result, 3u)')
    assert not mod.numeric_expectation('CHECK_EQ(result, 2u)', 'CHECK_EQ(other, 3u)')
    assert mod.inspect_changes(['tools/tests/x.py'], 'tools/tests/x.py.bak', {}, {})
    assert mod.inspect_changes(['tools/tests/x.py'], '/tmp/tree/tools/tests/x.py:12', {}, {}) == []
    patterns = {
        'userland/tests/x.c': '+rand();\n+srand(time(NULL));\n',
        'tools/tests/x.py': '+# random test\n+/* comment\n+random\n+*/\n+if "--mutate" in sys.argv[1:]: rc += mutate()\n',
    }
    for path, delta in patterns.items():
        got = mod.inspect_changes([path], path, {path: delta}, {path: ''})
        assert len(got) == (0 if path.endswith('.py') else 2), got
    # CLI: a real tiny git tree, including untracked tests and strict exit codes.
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    subprocess.run(['git', '-C', str(root), 'add', 'pack.md'], check=True)
    subprocess.run(['git', '-C', str(root), '-c', 'user.name=fixture', '-c', 'user.email=fixture@example.invalid', '-c', 'maintenance.auto=false', '-c', 'gc.auto=0', 'commit', '-qm', 'base'], check=True)
    for strict, expected in ((False, 0), (True, 1)):
        with contextlib.redirect_stdout(io.StringIO()):
            rc = mod.main(['--root', str(root), '--base', 'HEAD', '--pack', str(root / 'pack.md')] + (['--strict'] if strict else []))
        assert rc == expected, (rc, expected)
    for strict in (False, True):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            rc = mod.main(['--root', str(root), '--base', 'missing-revision', '--pack', str(root / 'pack.md')] + (['--strict'] if strict else []))
            assert rc == 2, rc
            rc = mod.main(['--root', str(root / 'missing-tree'), '--base', 'HEAD', '--pack', str(root / 'pack.md')] + (['--strict'] if strict else []))
            assert rc == 2, rc


def records_case(mod, root):
    write(root, 'docs/tasks/DEFERRED_TESTS.md', '| ID | 内容 |\n|---|---|\n| E11-3 | test |\n| X-4 | test |\n| PRIO-1 | test |\n')
    heading = '**e11b1 完了**:\n'
    write(root, 'docs/tasks/a/TASK_X.md', heading + '記録\n' * 10 + '\n## next\n未確認\n')
    try:
        warnings = mod.record_warnings(str(root))
    except ValueError as exc:
        raise AssertionError(str(exc)) from exc
    assert warnings == [], warnings
    write(root, 'docs/tasks/a/TASK_X.md', heading + '| data | value |\n' * 20 + '**仕様**:\n記録\n')
    assert mod.record_warnings(str(root)) == []
    write(root, 'docs/tasks/a/TASK_X.md', heading + '記録\n' * 11 + '**e11b2 完了**:\n未実施 E11-3\n延期 X-4\n未確認 PRIO-1\n## end\n未実施\n')
    try:
        warnings = mod.record_warnings(str(root))
    except ValueError as exc:
        raise AssertionError(str(exc)) from exc
    assert len(warnings) == 1 and '11 行' in warnings[0], warnings
    write(root, 'docs/tasks/a/TASK_X.md', heading + '| 未確認 | E11-3 |\n延期\n未確認 E11-30\n未実施 BAD-1\n')
    warnings = mod.record_warnings(str(root))
    assert len(warnings) == 3 and all('台帳ID' in w for w in warnings), warnings
    write(root, 'docs/POLICY_DEV.md', '<!-- status-vocab:begin -->\n| 語 | 意味 |\n|---|---|\n| 計画 | fixture |\n<!-- status-vocab:end -->\n')
    write(root, 'docs/tasks/a/TASK_X.md', '状態: 計画\n' + heading + '未確認\n')
    saved = sys.argv
    try:
        for strict, expected in ((False, 0), (True, 1)):
            sys.argv = ['check_docs_status', '--root', str(root)] + (['--strict'] if strict else [])
            with contextlib.redirect_stdout(io.StringIO()):
                assert mod.main() == expected
    finally:
        sys.argv = saved
    for stage in ('e11b1', 'd0a', 'T2c', 'f5'):
        write(root, 'docs/tasks/a/TASK_X.md', '**%s 未確認**:\n**未確認**: 延期\n## end\n未実施\n' % stage)
        warnings = mod.record_warnings(str(root))
        assert len(warnings) == 2, warnings
    write(root, 'docs/tasks/a/TASK_X.md', '**section2 完了**:\n未確認\n')
    assert mod.record_warnings(str(root)) == []
    ledger = root / 'docs/tasks/DEFERRED_TESTS.md'
    for content in (None, '| ID | 内容 |\n'):
        if content is None:
            ledger.unlink()
        else:
            ledger.write_text(content)
        write(root, 'docs/tasks/a/TASK_X.md', '状態: 計画\n**f5 完了**:\n未確認\n')
        warnings = mod.record_warnings(str(root))
        assert len(warnings) == 2 and '台帳を読めません' in warnings[0], warnings
        saved = sys.argv
        try:
            for strict, expected in ((False, 0), (True, 1)):
                sys.argv = ['check_docs_status', '--root', str(root)] + (['--strict'] if strict else [])
                with contextlib.redirect_stdout(io.StringIO()):
                    assert mod.main() == expected
            write(root, 'docs/tasks/a/TASK_X.md', '状態: 語彙の外\n')
            sys.argv = ['check_docs_status', '--root', str(root)]
            with contextlib.redirect_stdout(io.StringIO()):
                assert mod.main() == 1
        finally:
            sys.argv = saved


MUTATIONS = {
    'test_changes': [('if not mentioned(path, pack):', 'if False:'), ('if MUTATE_SKIP.search(code) and not mutation_only:', 'if False:'), ('if RANDOM.search(code):', 'if False:'), ('if not MAIN.search(added):', 'if False:'), ('if numeric_expectation(old, line):', 'if False:')],
    'check_docs_status': [('count > 10', 'count > 11'), ('count > 10', 'count > 9'), ('ids.add(match.group(1))', 'pass'), ('if not has_id(line, ids):', 'if False:')],
}
CASES = {'test_changes': changes_case, 'check_docs_status': records_case}


def main(mutate=False):
    bad = 0
    for name, case in CASES.items():
        try:
            with tempfile.TemporaryDirectory(prefix='os32-pack-') as tmp:
                case(load(name), Path(tmp))
            print('GREEN', name)
        except Exception as exc:
            print('FAIL', name, repr(exc)); bad += 1
    if mutate:
        for name, edits in MUTATIONS.items():
            original = (ROOT / 'tools' / (name + '.py')).read_text()
            for old, new in edits:
                if original.count(old) != 1:
                    print('INVALID mutation', name, old); bad += 1; continue
                try:
                    with tempfile.TemporaryDirectory(prefix='os32-pack-mut-') as tmp:
                        CASES[name](load(name, original.replace(old, new)), Path(tmp))
                except AssertionError:
                    print('RED', name, old)
                except Exception as exc:
                    print('INVALID', name, repr(exc)); bad += 1
                else:
                    print('SURVIVED', name, old); bad += 1
    return int(bool(bad))


if __name__ == '__main__':
    sys.exit(main('--mutate' in sys.argv))
