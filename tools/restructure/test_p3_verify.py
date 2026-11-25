import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

from p3_verify import verify


class VerifyMove(unittest.TestCase):
    def test_candidate(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            rows = [{'from': 'old', 'to': 'new'}]
            (root / 'new').touch()
            self.assertEqual(verify(root, rows, 'R100\told\tnew'), [])
            self.assertEqual(verify(root, rows, 'D\told\nA\tnew'),
                             ['改名未検出 old -> new'])
            (root / 'old').touch()
            self.assertEqual(verify(root, rows, 'R100\told\tnew\nR090\tx\ty'),
                             ['実体不一致 old -> new', '台帳外改名 x -> y'])

    def test_path_only_fallback_rejects_changed_contents(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            rows = [{'from': 'old.mk', 'to': 'new.mk'},
                    {'from': 'source.c', 'to': 'sys/source.c'}]
            (root / 'sys').mkdir()
            (root / 'sys/source.c').touch()
            new = root / 'new.mk'
            new.write_text('SRC += sys/source.c\n')
            status = 'D\told.mk\nA\tnew.mk\nR100\tsource.c\tsys/source.c'
            with patch('p3_verify.subprocess.check_output', return_value=b'SRC += source.c\n'):
                self.assertEqual(verify(root, rows, status, 'HEAD'), [])
                new.write_text('SRC -= sys/source.c\n')
                self.assertEqual(verify(root, rows, status, 'HEAD'), ['改名未検出 old.mk -> new.mk'])


if __name__ == '__main__':
    unittest.main()
