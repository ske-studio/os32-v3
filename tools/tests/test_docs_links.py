"""Historical source remaps must not hide broken current documentation links."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_docs_links as links


@unittest.skipUnless(links.find_lychee(), 'lychee is not installed')
class DocumentationLinks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='docs-links-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.write('docs/archive/record.md', '[source](../../kernel/paging_app.c)\n')
        self.write('mm/paging_app.c', '/* moved source */\n')
        self.write('docs/current.md', '# Current\n')

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def check(self):
        with patch.object(links, 'PROJ_DIR', str(self.root)), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            result = links.main([])
        return result, output.getvalue()

    def test_archived_source_link_resolves_to_existing_destination(self):
        self.assertEqual(self.check()[0], 0)
        (self.root / 'mm/paging_app.c').unlink()
        result, output = self.check()
        self.assertEqual(result, 1)
        self.assertIn('paging_app.c', output)

    def test_current_source_link_does_not_use_historical_remap(self):
        self.write('docs/current.md', '[stale](../kernel/paging_app.c)\n')
        result, output = self.check()
        self.assertEqual(result, 1)
        self.assertIn('kernel/paging_app.c', output)

    def test_archived_document_anchors_and_unmapped_paths_still_checked(self):
        for target in ('missing.md', 'target.md#missing'):
            self.write('docs/archive/target.md', '# Present\n')
            self.write('docs/archive/record.md', '[broken](' + target + ')\n')
            with self.subTest(target=target):
                self.assertEqual(self.check()[0], 1)


if __name__ == '__main__':
    unittest.main()
