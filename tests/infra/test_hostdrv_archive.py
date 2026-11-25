from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import tarfile
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tests.support.media import main, verify_archive


class ArchiveTest(unittest.TestCase):
    def test_archive_diagnostics(self):
        with TemporaryDirectory() as tmp:
            archive = Path(tmp) / "hostdrv.tar.gz"
            manifest = Path(tmp) / "manifest.txt"
            manifest.write_text("test")
            for rc, diagnostic in [(1, "missing hostdrv/.deploy/manifest.txt"),
                                   (2, f"cannot read {archive}")]:
                with self.subTest(rc=rc):
                    if rc == 1:
                        with tarfile.open(archive, "w:gz"):
                            pass
                    else:
                        archive.write_bytes(b"broken")
                    output = io.StringIO()
                    with patch.object(sys, "argv", ["check", "pc98", "verify"]), redirect_stdout(output):
                        with self.assertRaises(SystemExit) as result:
                            main(None, lambda config: verify_archive(archive, manifest))
                    self.assertEqual(result.exception.code, rc)
                    self.assertIn(f"pc98: {diagnostic}", output.getvalue())
