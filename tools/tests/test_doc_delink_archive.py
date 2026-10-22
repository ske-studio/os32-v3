"""合成の git / 非 git 木でアーカイブ本文を参照しないリンク解除を検査する。

--mutate は正常対照を先に実行し、道具の写しで変異を一つずつ検出する。
"""
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import doc_graph

SRC = Path(__file__).resolve().parents[1] / 'doc_delink_archive.py'


def load(source, graph=None):
    module = types.ModuleType('doc_delink_under_test')
    module.__file__ = str(SRC)
    with patch.dict(sys.modules, {'doc_graph': graph or doc_graph}):
        exec(compile(source, str(SRC), 'exec'), module.__dict__)
    return module


def suite(mod):
    class DelinkTests(unittest.TestCase):
        def setUp(self):
            self.tmp = tempfile.TemporaryDirectory(prefix='doc-delink-')
            self.addCleanup(self.tmp.cleanup)
            self.root = Path(self.tmp.name)
            # 作業木内の TMPDIR でも、偽の .git から親のリポジトリを拾わない。
            isolation = patch.dict(os.environ, {'GIT_CEILING_DIRECTORIES': str(self.root.parent)})
            isolation.start()
            self.addCleanup(isolation.stop)

        def put(self, rel, text=''):
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(text.encode('utf-8'))
            return path

        def read(self, rel):
            return (self.root / rel).read_bytes().decode('utf-8')

        def cli(self, *args):
            output = io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                rc = mod.main(['--root', str(self.root), *args])
            return rc, output.getvalue()

        def git(self, *args):
            return subprocess.run(['git', '-C', str(self.root), *args],
                                  capture_output=True, check=True)

        def test_inline_title_fragment_image_and_paths(self):
            self.put('docs/S.md', '''前 [旧票](archive/v3/X.md) 後
[題](archive/v3/X.md#frag "title") ![絵](archive/pic.png#x)
[X.md](archive/v3/X.md#x) [docs/archive/v3/X.md](archive/v3/X.md)
[`archive/v3/X.md`](archive/v3/X.md) [空白](<archive/space name.md> "title")
[括弧](archive/name(1).md) [dir](archive) [dir](archive/)
''')
            rc, output = self.cli()
            self.assertEqual(rc, 0, output)
            self.assertEqual(self.read('docs/S.md'), '''前 旧票 (アーカイブ: `docs/archive/v3/X.md`) 後
題 (アーカイブ: `docs/archive/v3/X.md`) 絵 (アーカイブ: `docs/archive/pic.png`)
`docs/archive/v3/X.md` `docs/archive/v3/X.md`
`docs/archive/v3/X.md` 空白 (アーカイブ: `docs/archive/space name.md`)
括弧 (アーカイブ: `docs/archive/name(1).md`) dir (アーカイブ: `docs/archive`) dir (アーカイブ: `docs/archive`)
''')
            self.assertIn('リンク 10 件', output)

        def test_relative_resolution_and_unchanged_destinations(self):
            body = '''[旧](../../archive/./v3/../A.md?x=1#f)
[encoded](../../%61rchive/A%20B.md)
[sibling](../../archive-other/A.md) [out](../../archive/../S.md)
[root](/docs/archive/A.md) [web](https://example.org/docs/archive/A.md)
[net](//example.org/docs/archive/A.md) [local](#frag) [up](../../../../docs/archive/A.md)
'''
            self.put('docs/tasks/sub/S.md', body)
            rc, output = self.cli()
            self.assertEqual(rc, 0, output)
            self.assertEqual(self.read('docs/tasks/sub/S.md'), body.replace(
                '[旧](../../archive/./v3/../A.md?x=1#f)', '旧 (アーカイブ: `docs/archive/A.md`)').replace(
                '[encoded](../../%61rchive/A%20B.md)', 'encoded (アーカイブ: `docs/archive/A B.md`)'))

        def test_reference_uses_and_definition_removal(self):
            self.put('docs/S.md', '''[文字][OLD] [old] [old][] ![画像][old] ![old]
[live] [非archive][live] [old][live]
[old]: archive/A.md#frag "title"
[live]: Current.md
''')
            rc, output = self.cli()
            self.assertEqual(rc, 0, output)
            self.assertEqual(self.read('docs/S.md'), '''文字 (アーカイブ: `docs/archive/A.md`) old (アーカイブ: `docs/archive/A.md`) old (アーカイブ: `docs/archive/A.md`) 画像 (アーカイブ: `docs/archive/A.md`) old (アーカイブ: `docs/archive/A.md`)
[live] [非archive][live] [old][live]
[live]: Current.md
''')
            self.assertIn('リンク 5 件、定義削除 1 件', output)

        def test_review_reference_normalization(self):
            # レビュー反例をそのまま保持する。
            body = '[a b] [a  b]\n[a b]: archive/A.md'
            expected = 'a b (アーカイブ: `docs/archive/A.md`) a  b (アーカイブ: `docs/archive/A.md`)\n'
            self.assertEqual(mod.delink('docs/S.md', body)[0], expected)
            body = '[Straße\t A][STRASSE A] [strasse  a][]\n[straße a]: archive/A.md\n'
            self.assertEqual(len(mod.delink('docs/S.md', body)[1]), 3)

        def test_review_first_definition_wins(self):
            body = '[r]\n[r]: Current.md\n[r]: archive/A.md'
            self.assertEqual(mod.delink('docs/S.md', body), (body, []))
            body = '[r]\n[r]: archive/A.md\n[r]: Current.md'
            self.assertEqual(mod.delink('docs/S.md', body)[0],
                             'r (アーカイブ: `docs/archive/A.md`)\n[r]: Current.md')
            # 正規化後に重複する定義も先勝ち。
            body = '[A B]\n[a  b]: Current.md\n[A B]: archive/A.md'
            self.assertEqual(mod.delink('docs/S.md', body), (body, []))

        def test_review_label_boundaries(self):
            cases = {
                '[a `]` b](archive/A.md)': 'a `]` b (アーカイブ: `docs/archive/A.md`)',
                '[a \\] b][r]\n[r]: archive/A.md': 'a \\] b (アーカイブ: `docs/archive/A.md`)\n',
                '[a ``x`]`x`` b](archive/A.md)': 'a ``x`]`x`` b (アーカイブ: `docs/archive/A.md`)',
                '[a \\] b](archive/A.md)': 'a \\] b (アーカイブ: `docs/archive/A.md`)',
            }
            for body, expected in cases.items():
                with self.subTest(body=body):
                    skips = []
                    result, edits = mod.delink('docs/S.md', body, skips)
                    self.assertEqual(result, expected)
                    self.assertTrue(edits)
                    self.assertEqual(skips, [])
                    self.assertEqual(mod.delink('docs/S.md', result), (result, []))

        def test_review_escaped_opening_bracket(self):
            for body in ('\\[r]\n[r]: archive/A.md', '\\[r](archive/A.md)',
                         '\\![r](archive/A.md)'):
                with self.subTest(body=body):
                    # \! は ! だけをエスケープし、後続の [r] は通常リンク。
                    if body.startswith('\\!'):
                        expected = '\\!r (アーカイブ: `docs/archive/A.md`)'
                        self.assertEqual(mod.delink('docs/S.md', body)[0], expected)
                    else:
                        self.assertEqual(mod.delink('docs/S.md', body), (body, []))
            body = '\\\\[r](archive/A.md)'
            self.assertEqual(mod.delink('docs/S.md', body)[0],
                             '\\\\r (アーカイブ: `docs/archive/A.md`)')

        def test_review_comments_in_label_boundaries(self):
            for comment in (']', '[', '`', '[fake](archive/A.md)'):
                label = f'a <!-- {comment} --> b'
                for suffix in ('(archive/A.md)', '[r]\n[r]: archive/A.md'):
                    body = f'[{label}]{suffix}'
                    with self.subTest(body=body):
                        skips = []
                        result, edits = mod.delink('docs/S.md', body, skips)
                        expected = label + ' (アーカイブ: `docs/archive/A.md`)'
                        if suffix.startswith('[r]'):
                            expected += '\n'
                        self.assertEqual(result, expected)
                        self.assertTrue(edits)
                        self.assertEqual(skips, [])
                        self.assertEqual(mod.delink('docs/S.md', result), (result, []))

        def test_review_unsupported_paragraph_is_still_not_edited(self):
            for body in ("[a](archive/A.md)\n[x](Current.md 'title')",
                         "[x](Current.md 'title') [a](archive/A.md)",
                         '[x][unclosed [a](archive/A.md)',
                         '[a](archive/A.md)\n[b\n c][r]\n[r]: archive/A.md'):
                with self.subTest(body=body):
                    skips = []
                    self.assertEqual(mod.delink('docs/S.md', body, skips), (body, []))
                    self.assertTrue(skips)

        def test_review_link_containing_image_is_one_edit(self):
            for inner, outer in (('archive/A.png', 'archive/B.md'),
                                 ('current.png', 'archive/B.md'),
                                 ('archive/A.png', 'Current.md')):
                body = f'[![img]({inner})]({outer})'
                self.put('docs/S.md', body)
                rc, output = self.cli('--dry-run')
                self.assertEqual(rc, 0, output)
                self.assertIn('リンク 1 件', output)
                self.assertEqual(self.read('docs/S.md'), body)
                self.assertEqual(self.cli()[0], 0)
                result = self.read('docs/S.md')
                label = ('img (アーカイブ: `docs/archive/A.png`)' if inner.startswith('archive/')
                         else '![img](current.png)')
                expected = (label + ' (アーカイブ: `docs/archive/B.md`)' if outer.startswith('archive/')
                            else '[' + label + '](Current.md)')
                self.assertEqual(result, expected)
                self.assertEqual(mod.delink('docs/S.md', result), (result, []))

        def test_unsupported_is_reported_in_both_modes_and_keeps_definitions(self):
            body = '[outer [inner](Current.md)][r] [r]\n[r]: archive/A.md\n'
            self.put('docs/S.md', body)
            for args in (('--dry-run',), ()):
                rc, output = self.cli(*args)
                self.assertEqual(rc, 0, output)
                self.assertIn('skip docs/S.md:1 画像以外のリンクを内包するラベルは未対応', output)
                self.assertIn('skip 1 件', output)
                self.assertIn('[outer [inner](Current.md)][r]', self.read('docs/S.md'))
                self.assertIn('[r]: archive/A.md', self.read('docs/S.md'))
            result = self.read('docs/S.md')
            self.assertEqual(mod.delink('docs/S.md', result)[0], result)

        def test_unsupported_destination_is_kept_with_diagnostics(self):
            body = "[a](archive/A.md 'title')\n"
            self.put('docs/S.md', body)
            for args in (('--dry-run',), ()):
                rc, output = self.cli(*args)
                self.assertEqual(rc, 0, output)
                self.assertIn('skip docs/S.md:1 複数行または未対応の宛先', output)
                self.assertEqual(self.read('docs/S.md'), body)

        def test_multiline_candidates_are_kept_with_diagnostics(self):
            for body in ('[a\n b][r]\n[r]: archive/A.md\n',
                         '[a](\narchive/A.md)\n', '[a](archive/A.md\n "title")\n'):
                self.put('docs/S.md', body)
                for args in (('--dry-run',), ()):
                    rc, output = self.cli(*args)
                    self.assertEqual(rc, 0, output)
                    self.assertIn('skip docs/S.md:1 複数行のリンクは未対応', output)
                    self.assertEqual(self.read('docs/S.md'), body)

        def test_literal_brackets_do_not_hide_links(self):
            body = '[0,1) [a](archive/A.md) [literal\n'
            self.assertEqual(mod.delink('docs/S.md', body)[0],
                             '[0,1) a (アーカイブ: `docs/archive/A.md`) [literal\n')

        def test_tables_offsets_crlf_and_eof(self):
            body = '| 前 `code` | [旧](archive/A.md#f) | [A.md](archive/A.md) | 後 |\r\n末尾'
            self.put('docs/S.md', body)
            self.assertEqual(self.cli()[0], 0)
            self.assertEqual(self.read('docs/S.md'),
                             '| 前 `code` | 旧 (アーカイブ: `docs/archive/A.md`) | `docs/archive/A.md` | 後 |\r\n末尾')
            for target in ('archive/a%7Cb.md', 'archive/a`b.md'):
                result, edits = mod.delink('docs/S.md', f'| [旧]({target}) |')
                self.assertEqual(result.count('|'), 2)
                self.assertEqual(len(edits), 1)

        def test_protected_regions_and_archive_are_unchanged(self):
            body = '''`[x](archive/A.md)` ``[x](archive/A.md)``
```md
[x](archive/A.md)
[old]: archive/A.md
```
~~~
![x](archive/A.md)
~~~
<!-- [x](archive/A.md)
[old]: archive/A.md -->
prefix <!-- hidden --> [x](archive/A.md) suffix
[x](archive/A.md) <!-- untouched -->
'''
            self.put('docs/S.md', body)
            archive = '[x](A.md) [x](../archive/A.md)\n'
            self.put('docs/archive/A.md', archive)
            self.assertEqual(self.cli()[0], 0)
            self.assertEqual(self.read('docs/S.md'), body.replace(
                'prefix <!-- hidden --> [x](archive/A.md) suffix',
                'prefix <!-- hidden --> x (アーカイブ: `docs/archive/A.md`) suffix').replace(
                '[x](archive/A.md) <!-- untouched -->',
                'x (アーカイブ: `docs/archive/A.md`) <!-- untouched -->'))
            self.assertEqual(self.read('docs/archive/A.md'), archive)

        def test_archive_is_never_opened(self):
            self.put('docs/S.md', '[x](archive/A.md)\n')
            self.put('docs/archive/A.md', '# synthetic\n')
            (self.root / 'docs/archive/A.md').write_bytes(b'\xff')
            original = Path.open

            def guarded(path, *args, **kwargs):
                if path.is_relative_to(self.root / 'docs/archive'):
                    raise AssertionError('archive を開いた')
                return original(path, *args, **kwargs)

            with patch.object(Path, 'open', guarded):
                self.assertEqual(self.cli()[0], 0)

        def test_prose_mask_preserves_columns_and_fence_semantics(self):
            body = '<!--c-->```md\n[x](archive/A.md)\n```\n<!--last-->'
            self.put('docs/S.md', body)
            self.assertEqual(self.cli()[0], 0)
            self.assertEqual(self.read('docs/S.md'), body)
            self.assertEqual([len(line) for line in doc_graph.prose(body, preserve_columns=True)],
                             [len(line) for line in body.splitlines()])

        def test_git_selection_and_symlink_parents(self):
            self.git('init', '-q')
            self.put('docs/S.md', '[x](archive/A.md)')
            self.put('docs/sub/S.md', '[x](../archive/A.md)')
            self.put('docs/archive/A.md')
            for excluded in doc_graph.EXCLUDED - {'.git'}:
                self.put(excluded + '/S.md', '[x](docs/archive/A.md)')
            (self.root / 'link.md').symlink_to('docs/S.md')
            self.git('add', '-A')
            (self.root / 'docs/sub/S.md').unlink()
            (self.root / 'docs/sub').rmdir()
            (self.root / 'docs/sub').symlink_to(self.root / 'docs', target_is_directory=True)
            self.put('untracked.md', '[x](docs/archive/A.md)')
            self.assertEqual(self.cli()[0], 0)
            self.assertEqual(self.read('docs/S.md'), 'x (アーカイブ: `docs/archive/A.md`)')
            self.assertEqual(self.read('untracked.md'), '[x](docs/archive/A.md)')
            self.assertEqual(self.cli('--paths', 'link.md')[0], 1)
            self.assertEqual(self.cli('--paths', 'docs/sub/S.md')[0], 1)

        def test_non_git_selection_exclusions_and_symlinks(self):
            original = '[x](docs/archive/A.md)'
            for excluded in doc_graph.EXCLUDED:
                self.put(excluded + '/S.md', original)
            # .git がディレクトリでも git ls-files が失敗すれば合成木へ戻る。
            self.put('S.md', original)
            (self.root / 'link.md').symlink_to('S.md')
            (self.root / 'linked').symlink_to(self.root / 'docs', target_is_directory=True)
            self.assertEqual(self.cli()[0], 0)
            for excluded in doc_graph.EXCLUDED:
                self.assertEqual(self.read(excluded + '/S.md'), original)
            self.assertEqual(self.cli('--paths', 'link.md')[0], 1)

        def test_dry_run_paths_idempotence_and_graph(self):
            for git_tree in (False, True):
                with self.subTest(git=git_tree):
                    if git_tree:
                        self.git('init', '-q')
                    source = '[a](archive/A.md#x) [old]\n[old]: archive/A.md\n'
                    self.put('docs/S.md', source)
                    self.put('README.md', '[old](docs/archive/A.md)\n')
                    self.put('docs/archive/A.md', '# synthetic\n')
                    if git_tree:
                        self.git('add', '-A')
                    self.assertEqual(len(doc_graph.analyze(self.root)['current_to_archive']), 3)
                    rc, output = self.cli('--dry-run', '--paths', 'docs/../docs/S.md')
                    self.assertEqual(rc, 0, output)
                    self.assertIn('docs/S.md:1 [a](archive/A.md#x) → a (アーカイブ: `docs/archive/A.md`)', output)
                    self.assertIn('リンク 2 件、定義削除 1 件、文書 1 件', output)
                    self.assertEqual(self.read('docs/S.md'), source)
                    self.assertEqual(self.cli('--paths', 'docs/S.md')[0], 0)
                    self.assertEqual(self.read('README.md'), '[old](docs/archive/A.md)\n')
                    self.assertEqual(self.cli()[0], 0)
                    self.assertEqual(doc_graph.analyze(self.root)['current_to_archive'], [])
                    before = {p: self.read(p) for p in doc_graph.document_paths(self.root)}
                    rc, output = self.cli()
                    self.assertEqual(rc, 0, output)
                    self.assertIn('リンク 0 件、定義削除 0 件、文書 0 件', output)
                    self.assertEqual({p: self.read(p) for p in before}, before)

        def test_errors_and_archive_path_filter(self):
            self.assertEqual(self.cli('--unknown')[0], 1)
            self.assertEqual(self.cli('--paths')[0], 1)
            self.assertEqual(self.cli('--help')[0], 0)
            self.assertEqual(self.cli('--root', str(self.root / 'missing'))[0], 1)
            self.put('S.md')
            self.assertEqual(self.cli('--paths', '../outside.md')[0], 1)
            self.assertEqual(self.cli('--paths', 'missing.md')[0], 1)
            self.put('docs/archive/A.md', '[x](A.md)')
            self.assertIn('文書 0 件', self.cli('--paths', 'docs/archive/A.md')[1])
            (self.root / 'S.md').write_bytes(b'\xff')
            self.assertEqual(self.cli()[0], 1)

    return unittest.defaultTestLoader.loadTestsFromTestCase(DelinkTests)


MUTATIONS = [
    ('            skipped.append((index + 1, reason))\n            continue',
     '            skipped.append((index + 1, reason))', '保留中の段落も編集する'),
    ('return dest if in_archive(dest) else None', 'return None', '解決先の判定を壊す'),
    ('visible = prose(text, preserve_columns=True)', 'visible = text.splitlines()', 'フェンス除外を壊す'),
    ("            result[index] = ''", '            pass', '定義行を削除しない'),
    ('if in_archive(rel):', 'if False:', 'archive 内も書き換える'),
    ('paths = set(document_paths(root))', "paths = {p.relative_to(root).as_posix() for p in root.rglob('*.md')}",
     '追跡と symlink 除外を壊す'),
]


# 実装が共有するパーサーへの変異も解除器の反例で検出する。
GRAPH_MUTATIONS = [
    ('            char = visible[cursor]', '            char = line[cursor]',
     'ラベル境界走査のコメントマスクを外す'),
    ("return ' '.join(label.split()).casefold()", 'return label.casefold()', '参照名の空白正規化を外す'),
    ("return ' '.join(label.split()).casefold()", "return ' '.join(label.split())", '参照名の大小文字折畳みを外す'),
    ('definitions.setdefault(key, target)', 'definitions[key] = target', '最後の定義を有効にする'),
    ('return (pos - start) % 2 == 1', 'return False', 'エスケープ判定を外す'),
]


def main():
    source = SRC.read_text(encoding='utf-8')
    result = unittest.TextTestRunner(verbosity=2).run(suite(load(source)))
    bad = not result.wasSuccessful()
    if '--mutate' in sys.argv:
        graph_source = Path(doc_graph.__file__).read_text(encoding='utf-8')
        cases = [(source, item, False) for item in MUTATIONS]
        cases += [(graph_source, item, True) for item in GRAPH_MUTATIONS]
        for original, (old, new, name), shared in cases:
            if original.count(old) != 1:
                raise AssertionError('変異箇所が一意でない: ' + name)
            mutated = original.replace(old, new, 1)
            if shared:
                graph = types.ModuleType('doc_graph_mutant')
                graph.__file__ = doc_graph.__file__
                exec(compile(mutated, graph.__file__, 'exec'), graph.__dict__)
                changed = load(source, graph)
            else:
                changed = load(mutated)
            output = io.StringIO()
            result = unittest.TextTestRunner(stream=output).run(suite(changed))
            # 例外だけでなく、期待した挙動との不一致で RED になったことを確認する。
            killed = bool(result.failures)
            print('MUTATION %s: %s' % (name, 'RED' if killed else 'GREEN (見逃し)'), flush=True)
            bad |= not killed
    return int(bad)


if __name__ == '__main__':
    sys.exit(main())
