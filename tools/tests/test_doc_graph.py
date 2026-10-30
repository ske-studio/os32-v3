"""合成 Markdown の木で実物の分類・規則・基準値比較を検査する。

--mutate は正常対照の後、検査器の写しから判定を一つずつ外す。
本物のリポジトリと基準値は書き換えない。
"""
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

SRC = Path(__file__).resolve().parents[1] / 'doc_graph.py'


def load(source):
    module = types.ModuleType('doc_graph_under_test')
    module.__file__ = str(SRC)
    exec(compile(source, str(SRC), 'exec'), module.__dict__)
    return module


def suite(mod):
    class GraphTests(unittest.TestCase):
        def setUp(self):
            self.tmp = tempfile.TemporaryDirectory(prefix='doc-graph-')
            self.addCleanup(self.tmp.cleanup)
            self.root = Path(self.tmp.name)
            # 作業木内の TMPDIR でも、偽の .git から親のリポジトリを拾わない。
            isolation = patch.dict(os.environ, {'GIT_CEILING_DIRECTORIES': str(self.root.parent)})
            isolation.start()
            self.addCleanup(isolation.stop)

        def put(self, path, text=''):
            dest = self.root / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(text, encoding='utf-8')

        def counts(self):
            return {k: len(v) for k, v in mod.analyze(self.root).items()}

        def only(self, kind, count):
            self.assertEqual(self.counts(), {k: count if k == kind else 0 for k in mod.KINDS})

        def test_git_tree_counts_tracked_only(self):
            import subprocess
            git = lambda *a: subprocess.run(['git', '-C', str(self.root), *a], check=True, capture_output=True)
            git('init', '-q')
            self.put('docs/02_x.md', '[t](tasks/TASK_A.md)\n')
            self.put('docs/tasks/TASK_A.md', '# A\n')
            self.put('docs/sub/B.md', '[t](../tasks/TASK_A.md)\n')
            git('add', '-A')
            git('-c', 'user.email=t@t', '-c', 'user.name=t', 'commit', '-q', '-m', 'x')
            self.put('docs/03_untracked.md', '[t](tasks/TASK_A.md)\n')
            outside = tempfile.TemporaryDirectory(prefix='doc-graph-out-')
            self.addCleanup(outside.cleanup)
            Path(outside.name, 'B.md').write_text('[t](../tasks/TASK_A.md)\n[u](../tasks/TASK_A.md)\n', encoding='utf-8')
            import shutil
            shutil.rmtree(self.root / 'docs/sub')
            (self.root / 'docs/sub').symlink_to(outside.name, target_is_directory=True)
            self.only('spec_to_ticket', 1)

        def cli(self, *args):
            out = io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                rc = mod.main(['--root', str(self.root), *args])
            return rc, out.getvalue()

        def test_layers(self):
            cases = {
                'README.md': 0, 'AGENTS.md': 0, 'CLAUDE.md': 0, 'INSTALL.md': 0,
                'docs/INDEX.md': 0, 'docs/CONSTRAINTS.md': 1,
                'docs/POLICY_DEV.md': 1, '.claude/skills/a/SKILL.md': 1,
                'docs/tasks/agents/ROLES.md': 1, 'docs/08_build.md': 2,
                'sdk/README.md': 2, 'docs/decisions/D.md': 3,
                'docs/tasks/a/TASK_A.md': 4, 'docs/ROADMAP.md': 4,
                'docs/tasks/v3/V3_PLAN.md': 4, 'docs/tasks/agents/HANDOVER.md': 5,
                'docs/tasks/agents/REVIEW_A.md': 5, 'docs/tasks/DEFERRED_TESTS.md': 5,
                'tools/tests/a_tdd.md': 5, 'docs/archive/OLD.md': 9,
            }
            for path, layer in cases.items():
                self.assertEqual(mod.classify(path, []), layer, path)
            for value in ('状態: 確定', '> 状態: **確定 (2026-10-22)**',
                          '> 発行: PM / 状態: 確定。'):
                self.assertEqual(mod.classify('docs/tasks/a/design/A.md', [value]), 3)
            for lines in (['状態: 未確定'], ['状態: 確定前'],
                          ['状態: 計画', '状態: 確定'], [''] * 12 + ['状態: 確定']):
                self.assertEqual(mod.classify('docs/tasks/A.md', lines), 4)

        def test_spec_occurrences_and_listing(self):
            self.put('docs/S.md', '[a](tasks/T.md#章) [b](tasks/T.md)\n')
            self.put('docs/tasks/T.md')
            self.only('spec_to_ticket', 2)
            self.assertIn('docs/S.md:1 → docs/tasks/T.md', mod.analyze(self.root)['spec_to_ticket'])

        def test_decision_to_ticket(self):
            self.put('docs/tasks/realhw/TASK_HWDEV.md')
            cases = {
                'docs/decisions/D.md': '',
                'docs/tasks/realhw/design/HWDEV_CORE_API.md': '> 状態: **確定**\n',
            }
            for path, header in cases.items():
                with self.subTest(path=path):
                    target = '../tasks/realhw/TASK_HWDEV.md' if path.startswith('docs/decisions/') else '../TASK_HWDEV.md'
                    self.put(path, header + '[ticket](' + target + ')\n')
                    self.only('decision_to_ticket', 1)
                    self.put(path, header + '> 索引: 子の一覧\n[ticket](' + target + ')\n')
                    self.only('decision_to_ticket', 0)
                    self.put(path, '')

        def test_ticket_parent_and_successor(self):
            self.put('docs/tasks/A.md', '[p](P.md) [n](N.md) [x](X.md)')
            for name in ('P', 'N', 'X'):
                self.put('docs/tasks/' + name + '.md')
            self.only('ticket_crosslink', 3)
            self.put('docs/tasks/A.md', '> 上位: [p](P.md)\n> 後続: [n](N.md)\n[p](P.md) [n](N.md) [x](X.md)')
            self.only('ticket_crosslink', 1)
            self.put('docs/tasks/A.md', '\n' * 12 + '> 上位: [p](P.md)\n')
            self.only('ticket_crosslink', 1)

        def test_mutual_pairs_and_parent(self):
            self.put('docs/tasks/P.md', '[c](C.md) [again](C.md#x)')
            self.put('docs/tasks/C.md', '[p](P.md)')
            self.assertEqual(self.counts()['mutual_non_parent'], 1)
            self.put('docs/tasks/P.md', '> 索引: 子の一覧\n[c](C.md)')
            self.assertEqual(self.counts()['mutual_non_parent'], 1)
            self.put('docs/tasks/C.md', '> 上位: [p](P.md)\n[p](P.md)')
            self.assertEqual(sum(self.counts().values()), 0)
            self.put('docs/tasks/P.md', '[c](C.md)')
            self.assertEqual(self.counts()['mutual_non_parent'], 1)

        def test_parent_declaration_is_ticket_metadata(self):
            self.put('docs/P.md', '> 索引: 子\n[c](C.md)')
            self.put('docs/C.md', '> 上位: [p](P.md)')
            self.only('mutual_non_parent', 1)

        def test_indexes_require_head_declaration(self):
            self.put('docs/tasks/T.md')
            for body in ('[x](tasks/T.md)', '\n' * 12 + '> 索引: 一覧\n[x](tasks/T.md)',
                         '索引: 一覧\n[x](tasks/T.md)'):
                self.put('docs/DEVELOPMENT.md', body)
                self.only('spec_to_ticket', 1)
            for declaration in ('> 索引: 一覧', '> 索引:'):
                self.put('docs/DEVELOPMENT.md', '# 題\n\n' + declaration + '\n[x](tasks/T.md)')
                self.only('spec_to_ticket', 0)

        def test_archive_history_is_forbidden(self):
            self.put('docs/S.md', '[x](archive/A.md)\n経緯\n[x](archive/A.md)\n経緯: [x](archive/A.md)\n`経緯`: [x](archive/A.md)')
            self.put('docs/archive/A.md', '[back](../S.md) [t](../tasks/T.md)')
            self.put('docs/tasks/T.md')
            self.only('current_to_archive', 4)

        def test_archive_forbidden_from_every_current_layer_and_index(self):
            self.put('docs/archive/A.md', '[other](README.md) [current](../S.md)')
            self.put('docs/archive/README.md', '[old](A.md) [current](../S.md)')
            cases = {
                'docs/INDEX.md': 'archive/A.md',
                'docs/POLICY_DEV.md': 'archive/A.md',
                'docs/S.md': 'archive/A.md',
                'docs/decisions/D.md': '../archive/A.md',
                'docs/tasks/T.md': '../archive/A.md',
                'docs/tasks/agents/HANDOVER.md': '../../archive/A.md',
                'docs/tasks/DEFERRED_TESTS.md': '../archive/A.md',
            }
            for path, target in cases.items():
                for header in ('', '> 索引: 一覧\n'):
                    with self.subTest(path=path, header=header):
                        self.put(path, header + '経緯: [old](' + target + ')')
                        self.only('current_to_archive', 1)
                        self.put(path, '')

        def test_record_and_deferred_exception(self):
            self.put('docs/tasks/DEFERRED_TESTS.md', '[t](Other.md)')
            self.put('docs/tasks/Other.md')
            self.put('docs/tasks/agents/HANDOVER.md')
            self.put('docs/tasks/T.md', '[ok](DEFERRED_TESTS.md) [bad](agents/HANDOVER.md)')
            self.put('docs/S.md', '[bad](tasks/DEFERRED_TESTS.md)')
            self.put('docs/POLICY_DEV.md', '[bad](tasks/agents/HANDOVER.md)')
            self.put('docs/decisions/D.md', '[bad](../tasks/agents/HANDOVER.md)')
            self.only('canonical_to_record', 4)

        def test_allowed_canonical_links(self):
            self.put('docs/S.md', '[rule](POLICY_DEV.md) [decision](decisions/D.md) [self](S.md#x)')
            self.put('docs/POLICY_DEV.md')
            self.put('docs/decisions/D.md', '[spec](../Other.md)')
            self.put('docs/Other.md')
            self.put('docs/tasks/T.md', '[spec](../S.md) [dec](../decisions/D.md) [self](T.md)')
            self.assertEqual(sum(self.counts().values()), 0)

        def test_link_syntax_and_non_links(self):
            self.put('docs/tasks/T space.md')
            self.put('docs/tasks/T.md')
            self.put('docs/S.md', '''[a](tasks/T.md#章) [b](<tasks/T space.md>)
[c](tasks/T%20space.md "title") [ref][ticket] [ticket][] [ticket]
[ticket]: tasks/T.md#ref
[abs](/docs/tasks/T.md) [web](https://example.org/tasks/T.md)
[missing](tasks/NO.md) [outside](../../outside.md)
`[code](tasks/T.md)`
```md
> 索引: example
[code](tasks/T.md)
```
<!-- [comment](tasks/T.md) -->
[local](#章)
''')
            self.only('spec_to_ticket', 6)

        def test_review_reference_normalization_duplicates_and_escapes(self):
            paths = {'docs/archive/A.md', 'docs/Current.md'}
            cases = [
                ('[a b] [a  b]\n[a b]: archive/A.md', [(1, 'docs/archive/A.md')] * 2),
                ('[r]\n[r]: Current.md\n[r]: archive/A.md', [(1, 'docs/Current.md')]),
                ('[r]\n[r]: archive/A.md\n[r]: Current.md', [(1, 'docs/archive/A.md')]),
                ('[Straße\t A][STRASSE A]\n[straße a]: archive/A.md', [(1, 'docs/archive/A.md')]),
                ('\\[r]\n[r]: archive/A.md', []),
                ('\\[r](archive/A.md)', []),
                ('[0,1) [a](archive/A.md) [literal', [(1, 'docs/archive/A.md')]),
                ('[a\n b][r]\n[r]: archive/A.md', [(1, 'docs/archive/A.md')]),
                ('\\\\[r](archive/A.md)', [(1, 'docs/archive/A.md')]),
                ('[a `]` b](archive/A.md)', [(1, 'docs/archive/A.md')]),
                ('[a \\] b][r]\n[r]: archive/A.md', [(1, 'docs/archive/A.md')]),
                ('[![img](archive/A.png)](archive/A.md)', [(1, 'docs/archive/A.md')]),
                ('[![img](archive/A.md)](Current.md)',
                 [(1, 'docs/Current.md'), (1, 'docs/archive/A.md')]),
            ]
            for body, expected in cases:
                with self.subTest(body=body):
                    self.assertEqual(list(mod.links('docs/S.md', body.splitlines(), paths)), expected)

        def test_unsupported_edit_does_not_hide_readable_targets(self):
            paths = {'docs/archive/A.md', 'docs/Current.md'}
            cases = [
                ("[a](archive/A.md)\n[x](Current.md 'title')",
                 [(1, 'docs/archive/A.md'), (2, 'docs/Current.md')]),
                ("[x](Current.md 'title') [a](archive/A.md)",
                 [(1, 'docs/Current.md'), (1, 'docs/archive/A.md')]),
                ("[x](<archive/A.md> 'title') [a](archive/A.md)",
                 [(1, 'docs/archive/A.md')] * 2),
                ('[x](??? [a](archive/A.md)', [(1, 'docs/archive/A.md')]),
                ('[x][unclosed [a](archive/A.md)', [(1, 'docs/archive/A.md')]),
                ('[outer [inner](archive/A.md)](Current.md)',
                 [(1, 'docs/Current.md'), (1, 'docs/archive/A.md')]),
                ('[a](\narchive/A.md)\n[b](archive/A.md)',
                 [(1, 'docs/archive/A.md'), (3, 'docs/archive/A.md')]),
                ('[a\n b][r]\n[c](archive/A.md)\n[r]: archive/A.md',
                 [(1, 'docs/archive/A.md'), (3, 'docs/archive/A.md')]),
            ]
            for body, expected in cases:
                with self.subTest(body=body):
                    self.assertEqual(list(mod.links('docs/S.md', body.splitlines(), paths)), expected)

        def test_comments_do_not_change_label_boundaries(self):
            paths = {'docs/archive/A.md'}
            for comment in (']', '[', '`', '[fake](archive/A.md)'):
                for suffix in ('(archive/A.md)', '[r]\n[r]: archive/A.md'):
                    body = f'[a <!-- {comment} --> b]{suffix}'
                    with self.subTest(body=body):
                        self.assertEqual(list(mod.links('docs/S.md', body.splitlines(), paths)),
                                         [(1, 'docs/archive/A.md')])

        def test_exclusions_and_symlinks(self):
            self.put('docs/S.md', '[excluded](hw/T.md) [built](../build/T.md)')
            for name in ('docs/hw', 'build/', 'apps', 'game', '.git'):
                self.put(name.rstrip('/') + '/T.md', '[x](../docs/tasks/T.md)')
            self.put('docs/tasks/T.md')
            (self.root / 'docs/link.md').symlink_to(self.root / 'docs/tasks/T.md')
            (self.root / 'linked').symlink_to(self.root / 'docs', target_is_directory=True)
            self.assertEqual(set(mod.documents(self.root)), {'docs/S.md', 'docs/tasks/T.md'})
            self.assertEqual(sum(self.counts().values()), 0)

        def test_baseline_increase_decrease_update(self):
            self.put('tools/doc_tree_baseline.json', json.dumps(dict.fromkeys(mod.KINDS, 0)))
            self.put('docs/S.md', '[x](tasks/T.md)')
            self.put('docs/tasks/T.md')
            self.assertEqual(self.cli()[0], 1)
            self.assertEqual(self.cli('--update-baseline')[0], 0)
            baseline = self.root / 'tools/doc_tree_baseline.json'
            original = baseline.read_bytes()
            self.assertEqual(json.loads(original)['spec_to_ticket'], 1)
            self.assertEqual(self.cli()[0], 0)
            self.put('docs/S.md', '')
            rc, output = self.cli()
            self.assertEqual(rc, 0)
            self.assertIn('基準値を下げてよい', output)
            self.assertEqual(baseline.read_bytes(), original)
            self.assertEqual(self.cli('--update-baseline')[0], 0)
            self.assertEqual(json.loads(baseline.read_bytes())['spec_to_ticket'], 0)

        def test_invalid_or_missing_baseline(self):
            self.assertEqual(self.cli()[0], 1)
            for data in ({}, dict.fromkeys(mod.KINDS, -1), dict.fromkeys(mod.KINDS, True)):
                self.put('tools/doc_tree_baseline.json', json.dumps(data))
                self.assertEqual(self.cli()[0], 1)
    return unittest.defaultTestLoader.loadTestsFromTestCase(GraphTests)


MUTATIONS = [
    ('        nodes = scan_links(raw, mask, definitions)\n',
     '        nodes = scan_links(raw, mask, definitions)\n'
     '        if any(node.reason for node in nodes):\n            continue\n',
     '編集保留と段落の計数除外を再結合する'),
    ('            char = visible[cursor]', '            char = line[cursor]',
     'ラベル境界走査のコメントマスクを外す'),
    ('                    stop, target = prefix.end(), prefix[1]', '                    stop = prefix.end()',
     '未対応構文の読める宛先を捨てる'),
    ("return ' '.join(label.split()).casefold()", 'return label.casefold()', '参照名の空白正規化を外す'),
    ("return ' '.join(label.split()).casefold()", "return ' '.join(label.split())", '参照名の大小文字折畳みを外す'),
    ('definitions.setdefault(key, target)', 'definitions[key] = target', '最後の定義を有効にする'),
    ('return (pos - start) % 2 == 1', 'return False', 'エスケープ判定を外す'),
    ('if a == 2 and b == 4:', 'if False:', '仕様→票を数えない'),
    ('if a == 3 and b == 4:', 'if False:', '決定→票を数えない'),
    ('if a == 4 and b == 4 and dest not in parents[source] | successors[source]:', 'if False:', '横の票を数えない'),
    ('if not parent_child and pair not in violations', 'if False and pair not in violations', '相互リンクを数えない'),
    ('if b == 9:', 'if False:', 'アーカイブを数えない'),
    ('if b == 9:', "if b == 9 and '経緯' not in raw_docs[source][no - 1]:", '経緯の免除を復活'),
    ('if b == 9:', 'if b == 9 and source not in indexes:', '索引の免除を復活'),
    ('if b == 9:', "if b == 9 and source != 'docs/INDEX.md':", 'INDEX の免除を復活'),
    ('if b == 9:', 'if b == 9 and a != 5:', 'L5 の免除を復活'),
    ('if a in (1, 2, 3, 4) and b == 5 and not (', 'if False and not (', '記録参照を数えない'),
    ('if source not in indexes:', 'if True:', '索引を無視'),
    ('dest not in parents[source] | successors[source]', 'True', '上位・後続を無視'),
    ("a == 4 and dest == 'docs/tasks/DEFERRED_TESTS.md'", 'False', '延期の例外を無視'),
    ('if not parent_child and pair', 'if pair', '親子の例外を無視'),
    ('if args.update_baseline:', 'if True:', '基準値を無断で更新'),
    ('counts[k] > baseline[k]', 'False', '増加しても通す'),
    ("if path.startswith(('docs/decisions/', 'docs/design/')) or re.match", 'if False and re.match', '決定層を無視'),
    ('if dest in paths and dest != source:', 'if dest in paths:', '自己参照も数える'),
    ('    tracked = git_tracked_md(root)\n', '    tracked = None\n', 'git の管理外も数える'),
    (' and not parents_link and path.is_file()', ' and path.is_file()', '親の symlink を辿る'),
]


def main():
    source = SRC.read_text(encoding='utf-8')
    result = unittest.TextTestRunner(verbosity=2).run(suite(load(source)))
    bad = not result.wasSuccessful()
    if '--mutate' in sys.argv:
        for old, new, name in MUTATIONS:
            if source.count(old) != 1:
                raise AssertionError('変異箇所が一意でない: ' + name)
            changed = source.replace(old, new, 1)
            # 構文エラーは検出成功として数えない。
            module = load(changed)
            output = io.StringIO()
            result = unittest.TextTestRunner(stream=output).run(suite(module))
            killed = not result.wasSuccessful()
            print('MUTATION %s: %s' % (name, 'RED' if killed else 'GREEN (見逃し)'), flush=True)
            bad |= not killed
    return int(bad)


if __name__ == '__main__':
    sys.exit(main())
