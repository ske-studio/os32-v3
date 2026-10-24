"""Strict gate: real objdump input, named #GP exceptions, and excess hlt."""
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import check_privileged as checker


class PrivilegedTests(unittest.TestCase):
    def test_strict_gate_with_real_hlt_objects(self):
        cross = Path(os.environ['CROSS_DIR']) / 'bin'
        with tempfile.TemporaryDirectory(prefix='privileged-') as tmp:
            source = Path(tmp) / 'probe.s'
            obj = Path(tmp) / 'probe.o'
            for name, instruction, count, expected in (
                    ('userland/tests/faulttest.o', 'hlt', 1, 0),
                    ('userland/tests/h3a.o', 'hlt', 1, 0),
                    ('userland/tests/h3b.o', 'hlt', 1, 0),
                    ('userland/tests/unallowed.o', 'hlt', 1, 1),
                    ('userland/tests/faulttest.o', 'hlt', 2, 1),
                    ('userland/tests/faulttest.o', 'cli', 1, 1),
                    ('userland/tests/ordinary.o', 'nop', 1, 0)):
                with self.subTest(name=name, instruction=instruction, count=count):
                    source.write_text('.text\n' + (instruction + '\n') * count)
                    subprocess.run([str(cross / 'i386-elf-as'), '--32',
                                    str(source), '-o', str(obj)], check=True)
                    hits = checker.scan_object(str(cross / 'i386-elf-objdump'), str(obj))
                    with patch.object(checker, 'find_objects', return_value=[str(ROOT / name)]), \
                         patch.object(checker, 'scan_object', return_value=hits), \
                         patch.object(sys, 'argv', ['check_privileged.py', '--strict']), \
                         contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(checker.main(), expected)


if __name__ == '__main__':
    unittest.main()
