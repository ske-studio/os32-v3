#!/usr/bin/env python3
"""Focused lexical/format controls for the CPL3 argument-window check."""
from pathlib import Path
from contextlib import redirect_stdout
import io
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_kprintf_window as checker


class WindowTests(unittest.TestCase):
    def fixture(self, directory, sources, allow=''):
        root = Path(directory)
        for subdir in ('exec', 'tools', 'userland', 'sdk'):
            (root / subdir).mkdir(parents=True, exist_ok=True)
        # Use the real ABI definition for boundary and allowlist tests.
        (root / 'exec/exec.c').write_text((checker.ROOT / 'exec/exec.c').read_text())
        (root / checker.ALLOW).write_text(allow)
        for file, source in sources.items():
            path = root / file
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
        return root

    def audit(self, root):
        output = io.StringIO()
        with redirect_stdout(output):
            failed = checker.audit(root)
        return failed, output.getvalue()

    def test_conversion_slots(self):
        for fmt, slots in (('%%', 0), ('%%%d', 1), ('%08lx %s %-3d', 3),
                           ('%*.*s', 3), ('%*.3d %. *s'.replace(' ', ''), 4),
                           ('%d\0%d', 1)):
            with self.subTest(fmt=fmt):
                self.assertEqual(checker.count_arguments(fmt), slots)

    def test_invalid_format_fails_closed(self):
        with self.assertRaises(ValueError):
            checker.count_arguments('trailing %')

    def test_calls_comments_scopes_and_nested_arguments(self):
        source = r'''
/* api->kprintf(0, fmt); */
void first(void) {
    char *s = "fake->kprintf(0, fmt)";
    strange_name -> kprintf (pick(1, 2), "%d" /* comma, */ " %*.*s %%",
                            nested(1, 2), 2, 3, (char[]){'a', ',', 0});
}
void second(void) {
#if 0
    (_api)->kprintf(0, FORMAT_MACRO, 1);
#endif
    api->kprintf(0, "\045d\x25s", 1, s);
}
'''
        calls = checker.calls_in(source, 'userland/tests/probe.c')
        self.assertEqual(len(calls), 3)
        self.assertEqual([(c.function, c.supplied) for c in calls],
                         [('first', 4), ('second', 1), ('second', 2)])
        self.assertEqual(checker.count_arguments(calls[0].format), 4)
        self.assertIsNone(calls[1].format)
        self.assertEqual(calls[2].format, '%d%s')
        self.assertEqual(calls[0].line, 5)

    def test_unmatched_quote_in_inactive_branch(self):
        source = ("void probe(void) {\n#if 0\n'\n#endif\n"
                  'api->kprintf(0, "%d", \'a\');\n}')
        calls = checker.calls_in(source, 'userland/probe.c')
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0].format, '%d')
        self.assertEqual(calls[0].supplied, 1)
        self.assertEqual(calls[0].line, 5)

    def test_dot_receivers(self):
        calls = checker.calls_in('void probe(void) { '
                                 '(*api).kprintf(0, "%d", 1); '
                                 'ops.kprintf(0, "%s", s); }', 'userland/probe.c')
        self.assertEqual([(c.function, c.format, c.supplied) for c in calls],
                         [('probe', '%d', 1), ('probe', '%s', 1)])

    def test_c_line_splicing(self):
        calls = checker.calls_in('void probe(void) { api-\\\n>kprintf(0, "%\\\nd", 1); }',
                                 'userland/probe.c')
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0].format, '%d')
        self.assertEqual(calls[0].function, 'probe')

    def test_prefixed_parenthesized_literals_and_macro(self):
        calls = checker.calls_in('void probe(void) { '
                                 'api->kprintf(0, ((u8"%d" /* gap */ "%s")), 1, s); '
                                 'api->kprintf(0, (FORMAT_MACRO "%s"), s); }',
                                 'userland/probe.c')
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0].format, '%d%s')
        self.assertIsNone(calls[1].format)

    def test_limit_comes_from_exec_definition(self):
        with tempfile.TemporaryDirectory(prefix='kprintf-limit-') as directory:
            root = Path(directory)
            (root / 'exec').mkdir()
            (root / 'exec/exec.c').write_text('#define RING3_ARG_WINDOW  80u\n')
            self.assertEqual(checker.window_limit(root), (80, 18))

    def test_all_user_sources_and_sdk_but_not_submodules(self):
        directories = ('userland/shell', 'userland/cmds', 'userland/system',
                       'userland/tests', 'userland/lib/nested', 'sdk/crt', 'sdk/allocator')
        sources = {directory + '/probe.c': 'void probe(void) { custom->kprintf(0, "%s", s); }'
                   for directory in directories}
        sources['userland/system/probe.inc'] = 'void included(void) { ops->kprintf(0, "%d", 1); }'
        sources['sdk/include/probe.h'] = 'void header(void) { api->kprintf(0, "%s", s); }'
        sources.update({directory + '/external.c': 'void bad(void) { api->kprintf(0, fmt); }'
                        for directory in ('apps', 'game', 'userland/rust/target')})
        with tempfile.TemporaryDirectory(prefix='kprintf-scope-') as directory:
            failed, output = self.audit(self.fixture(directory, sources))
        self.assertFalse(failed, output)
        self.assertIn('scanned files=9', output)
        self.assertIn('calls=9', output)
        self.assertIn('excluded apps: C files=1', output)
        self.assertIn('excluded game: C files=1', output)

    def test_allowance_requires_reason_and_is_function_scoped(self):
        file = 'userland/lib/probe.c'
        sources = {file: 'void approved(void) { api->kprintf(0, fmt, 1); }\n'
                         'void other(void) { api->kprintf(0, FORMAT, 1); }'}
        with tempfile.TemporaryDirectory(prefix='kprintf-allow-') as directory:
            root = self.fixture(directory, sources, file + ':approved one audited slot\n')
            failed, output = self.audit(root)
            self.assertTrue(failed)
            self.assertIn('(other): nonliteral format outside allowlist', output)
            self.assertNotIn('(approved): nonliteral', output)
            (root / file).write_text('void approved(void) { api->kprintf(0, "%d", 1); }')
            failed, output = self.audit(root)
            self.assertTrue(failed)
            self.assertIn('stale allowance', output)
            (root / checker.ALLOW).write_text(file + ':approved\n')
            with self.assertRaises(ValueError):
                checker.read_allow(root)

    def test_stars_and_nonliteral_allowance_cannot_exceed_window(self):
        _, limit = checker.window_limit(checker.ROOT)
        # Exactly limit slots from width, precision and conversions.
        good = 'void boundary(void) { api->kprintf(0, "%*.*s' + '%d' * (limit - 3)
        arguments = ', '.join(['1'] * limit)
        good += '%%", ' + arguments + '); }'
        with tempfile.TemporaryDirectory(prefix='kprintf-boundary-') as directory:
            root = self.fixture(directory, {'sdk/crt/probe.c': good})
            failed, output = self.audit(root)
            self.assertFalse(failed, output)
            # Extra conversion in a concatenated literal is an overflow too.
            (root / 'sdk/crt/probe.c').write_text(good.replace('%%",', '%%" "%d",'))
            failed, output = self.audit(root)
            self.assertTrue(failed)
            self.assertIn('window exceeded', output)
            (root / 'sdk/crt/probe.c').write_text('void dynamic(void) { api->kprintf(0, fmt, '
                                                + arguments + ', 1); }')
            (root / checker.ALLOW).write_text('sdk/crt/probe.c:dynamic audited\n')
            failed, output = self.audit(root)
            self.assertTrue(failed)
            self.assertIn('window exceeded', output)


if __name__ == '__main__':
    # Run lexical/boundary controls before the real scan, also under --mutate.
    tests = unittest.main(argv=[sys.argv[0]], exit=False)
    if not tests.result.wasSuccessful():
        sys.exit(1)
    sys.exit(checker.main())
