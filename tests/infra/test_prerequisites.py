from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tests.infra.ci import prerequisites


class PrerequisitesTest(unittest.TestCase):
    def test_tools_follow_build_target(self):
        with TemporaryDirectory() as tmp:
            for prefix in ("powerpc-elf", "i386-elf"):
                for name in ("include/stdio.h", "lib/libc.a"):
                    path = Path(tmp) / prefix / name
                    path.parent.mkdir(parents=True)
                    path.write_bytes(b"x")
            missing = {"xorriso"}
            with patch.dict("os.environ", PPC_CROSS_DIR=tmp, CROSS_DIR=tmp), patch(
                "shutil.which", side_effect=lambda tool: tool not in missing
            ), patch("importlib.util.find_spec", return_value=True):
                self.assertEqual(prerequisites("linux-cross", "ppc", "core", ["make:iso"]), set())
                self.assertEqual(prerequisites("linux-cross", "x86", "pc98", ["make:iso"]),
                                 {"missing tool xorriso"})
                missing.update(("genisoimage", "isoinfo"))
                self.assertEqual(prerequisites("linux-ppc-cross", "ppc", "link-sh", ["make:common-compile"]), set())
                self.assertEqual(prerequisites("linux-ppc-cross", "ppc", "core", ["make:iso"]),
                                 {"missing tool genisoimage", "missing tool isoinfo"})
